#include "core/capture/capture.hpp"

#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

#include "core/base/clock.hpp"
#include "core/base/hash.hpp"
#include "core/base/text.hpp"
#include "core/win/com.hpp"
#include "core/win/window.hpp"

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x2
#endif
#ifndef PW_CLIENTONLY
#define PW_CLIENTONLY 0x1
#endif

namespace dx::cap {

const char* method_name(Method m) { return m == Method::PrintWindow ? "print_window" : "screen_blt"; }

std::unique_ptr<Image> Image::create(int w, int h) {
  if (w <= 0 || h <= 0 || w > 32768 || h > 32768) return nullptr;
  std::unique_ptr<Image> im(new Image());
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  im->bmp_ = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!im->bmp_) return nullptr;
  im->dc_ = CreateCompatibleDC(nullptr);
  im->old_ = SelectObject(im->dc_, im->bmp_);
  im->bits = static_cast<u8*>(bits);
  im->w = w;
  im->h = h;
  return im;
}

Image::~Image() {
  if (dc_) {
    SelectObject(dc_, old_);
    DeleteDC(dc_);
  }
  if (bmp_) DeleteObject(bmp_);
}

bool looks_blank(const Image& im) {
  const int step = std::max(1, std::min(im.w, im.h) / 12);
  for (int y = 0; y < im.h; y += step)
    for (int x = 0; x < im.w; x += step) {
      const u8* p = im.bits + static_cast<size_t>(y) * im.stride() + static_cast<size_t>(x) * 4;
      if (p[0] | p[1] | p[2]) return false;
    }
  return true;
}

Res<Shot> shoot_screen(const geo::RectI& r) {
  Stopwatch sw;
  auto im = Image::create(r.w, r.h);
  if (!im) return fail(E_BAD_ARG, "bad capture size");
  HDC sdc = GetDC(nullptr);
  const BOOL ok = BitBlt(im->dc(), 0, 0, r.w, r.h, sdc, r.x, r.y, SRCCOPY | CAPTUREBLT);
  ReleaseDC(nullptr, sdc);
  if (!ok) return fail(E_WIN32, "screen capture failed");
  Shot s;
  s.frame.r = r;
  s.frame.dpi = GetDpiForSystem();
  s.img = std::move(im);
  s.method = Method::ScreenBlt;
  s.us = sw.ns() / 1000;
  return s;
}

namespace {
// 窗口客户区 5×5 采样点都落在本窗口（或其子窗口）上，说明没被遮挡，可以直接从屏幕拷贝，省掉 PrintWindow 等合成一帧的 ~16 ms。
bool unoccluded(HWND h, const geo::Frame& fr) {
  if (fr.r.w < 8 || fr.r.h < 8 || !IsWindowVisible(h) || IsIconic(h)) return false;
  const geo::RectI vs = win::virtual_screen();
  if (!vs.contains(fr.r.x, fr.r.y) || !vs.contains(fr.r.right() - 1, fr.r.bottom() - 1)) return false;
  HWND root = GetAncestor(h, GA_ROOT);
  for (int j = 0; j < 5; ++j)
    for (int i = 0; i < 5; ++i) {
      POINT p{fr.r.x + (fr.r.w - 1) * i / 4, fr.r.y + (fr.r.h - 1) * j / 4};
      HWND at = WindowFromPoint(p);
      if (!at || GetAncestor(at, GA_ROOT) != root) return false;
    }
  return true;
}
}  // namespace

Res<Shot> shoot_window(HWND h) {
  Stopwatch sw;
  if (!h || !IsWindow(h)) return fail(E_NOT_FOUND, "window is gone");
  if (IsIconic(h)) return fail(E_UNSUPPORTED, "window is minimized; restore it first");
  const geo::Frame fr = win::client_frame(h);
  auto im = Image::create(fr.r.w, fr.r.h);
  if (!im) return fail(E_BAD_ARG, "window has no client area");
  Shot s;
  s.frame = fr;
  s.method = Method::PrintWindow;
  if (unoccluded(h, fr)) {
    HDC sdc = GetDC(nullptr);
    const BOOL ok0 = BitBlt(im->dc(), 0, 0, fr.r.w, fr.r.h, sdc, fr.r.x, fr.r.y, SRCCOPY | CAPTUREBLT);
    ReleaseDC(nullptr, sdc);
    if (ok0 && !looks_blank(*im)) {
      s.method = Method::ScreenBlt;
      s.img = std::move(im);
      s.us = sw.ns() / 1000;
      return s;
    }
  }
  const BOOL ok = PrintWindow(h, im->dc(), PW_CLIENTONLY | PW_RENDERFULLCONTENT);
  if (!ok || looks_blank(*im)) {
      if (!unoccluded(h, fr)) return fail(E_UNSUPPORTED, "target cannot render a background screenshot; screen fallback would capture another window");
    HDC sdc = GetDC(nullptr);
    const BOOL ok2 = BitBlt(im->dc(), 0, 0, fr.r.w, fr.r.h, sdc, fr.r.x, fr.r.y, SRCCOPY | CAPTUREBLT);
    ReleaseDC(nullptr, sdc);
    if (!ok2) return fail(E_WIN32, "window capture failed");
    s.method = Method::ScreenBlt;
  }
  s.img = std::move(im);
  s.us = sw.ns() / 1000;
  return s;
}

Res<std::unique_ptr<Image>> crop(const Image& src, geo::RectI r) {
  r = r.intersect({0, 0, src.w, src.h});
  if (r.empty()) return fail(E_BAD_ARG, "crop region is outside the image");
  auto out = Image::create(r.w, r.h);
  if (!out) return fail(E_BAD_ARG, "bad crop size");
  for (int y = 0; y < r.h; ++y)
    std::memcpy(out->bits + static_cast<size_t>(y) * out->stride(), src.bits + static_cast<size_t>(r.y + y) * src.stride() + static_cast<size_t>(r.x) * 4,
                static_cast<size_t>(r.w) * 4);
  return out;
}

std::unique_ptr<Image> scaled(const Image& src, int max_dim, double* factor) {
  const int big = std::max(src.w, src.h);
  if (factor) *factor = 1.0;
  if (max_dim <= 0 || big <= max_dim) return nullptr;
  const double f = static_cast<double>(max_dim) / big;
  const int nw = std::max(1, static_cast<int>(std::lround(src.w * f))), nh = std::max(1, static_cast<int>(std::lround(src.h * f)));
  auto out = Image::create(nw, nh);
  if (!out) return nullptr;
  SetStretchBltMode(out->dc(), HALFTONE);
  SetBrushOrgEx(out->dc(), 0, 0, nullptr);
  StretchBlt(out->dc(), 0, 0, nw, nh, src.dc(), 0, 0, src.w, src.h, SRCCOPY);
  if (factor) *factor = f;
  return out;
}

namespace {
void blend_px(u8* p, u8 r, u8 g, u8 b, int a256) {
  p[0] = static_cast<u8>((p[0] * (256 - a256) + b * a256) >> 8);
  p[1] = static_cast<u8>((p[1] * (256 - a256) + g * a256) >> 8);
  p[2] = static_cast<u8>((p[2] * (256 - a256) + r * a256) >> 8);
}

void vline(Image& im, int x, int y0, int y1, u8 r, u8 g, u8 b, int a, int dash) {
  if (x < 0 || x >= im.w) return;
  y0 = std::max(0, y0);
  y1 = std::min(im.h, y1);
  for (int y = y0; y < y1; ++y) {
    if (dash && ((y / dash) & 1)) continue;
    blend_px(im.bits + static_cast<size_t>(y) * im.stride() + static_cast<size_t>(x) * 4, r, g, b, a);
  }
}
void hline(Image& im, int y, int x0, int x1, u8 r, u8 g, u8 b, int a, int dash) {
  if (y < 0 || y >= im.h) return;
  x0 = std::max(0, x0);
  x1 = std::min(im.w, x1);
  u8* row = im.bits + static_cast<size_t>(y) * im.stride();
  for (int x = x0; x < x1; ++x) {
    if (dash && ((x / dash) & 1)) continue;
    blend_px(row + static_cast<size_t>(x) * 4, r, g, b, a);
  }
}

struct Font {
  HFONT f{nullptr};
  int px{0};
  ~Font() {
    if (f) DeleteObject(f);
  }
  void ensure(int size) {
    if (f && px == size) return;
    if (f) DeleteObject(f);
    px = size;
    f = CreateFontW(-size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                    DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
  }
};

void text_halo(HDC dc, int x, int y, const std::wstring& s) {
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(12, 12, 16));
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
      if (dx || dy) TextOutW(dc, x + dx, y + dy, s.c_str(), static_cast<int>(s.size()));
  SetTextColor(dc, RGB(255, 236, 120));
  TextOutW(dc, x, y, s.c_str(), static_cast<int>(s.size()));
}

std::string num(double v) {
  char b[24];
  std::snprintf(b, sizeof b, "%.3f", v);
  std::string s = b;
  while (s.size() > 3 && s.back() == '0') s.pop_back();
  return s;
}
}  // namespace

void draw_grid(Image& im, const GridSpec& g) {
  const int cols = std::clamp(g.cols, 1, 40), rows = std::clamp(g.rows, 1, 40);
  const u8 R = 255, G = 62, B = 165;
  for (int i = 1; i < cols; ++i) {
    const int x = static_cast<int>(std::lround(static_cast<double>(im.w) * i / cols));
    vline(im, x, 0, im.h, R, G, B, 118, 6);
  }
  for (int j = 1; j < rows; ++j) {
    const int y = static_cast<int>(std::lround(static_cast<double>(im.h) * j / rows));
    hline(im, y, 0, im.w, R, G, B, 118, 6);
  }
  // 边缘刻度：每格再分 5 段，便于读数插值。
  for (int i = 0; i <= cols * 5; ++i) {
    const int x = static_cast<int>(std::lround(static_cast<double>(im.w - 1) * i / (cols * 5)));
    const int len = i % 5 == 0 ? 9 : 4;
    vline(im, x, 0, len, 255, 236, 120, 230, 0);
    vline(im, x, im.h - len, im.h, 255, 236, 120, 230, 0);
  }
  for (int j = 0; j <= rows * 5; ++j) {
    const int y = static_cast<int>(std::lround(static_cast<double>(im.h - 1) * j / (rows * 5)));
    const int len = j % 5 == 0 ? 9 : 4;
    hline(im, y, 0, len, 255, 236, 120, 230, 0);
    hline(im, y, im.w - len, im.w, 255, 236, 120, 230, 0);
  }
  if (!g.labels) return;
  static thread_local Font font;
  const int fs = std::clamp(im.h / 62, 11, 17);
  font.ensure(fs);
  HGDIOBJ old = SelectObject(im.dc(), font.f);
  for (int i = 1; i < cols; ++i) {
    const int x = static_cast<int>(std::lround(static_cast<double>(im.w) * i / cols));
    const double lam = geo::compose(g.region, {static_cast<double>(i) / cols, 0}).lam;
    text_halo(im.dc(), x + 3, 10, text::widen(num(lam)));
  }
  for (int j = 1; j < rows; ++j) {
    const int y = static_cast<int>(std::lround(static_cast<double>(im.h) * j / rows));
    const double phi = geo::compose(g.region, {0, static_cast<double>(j) / rows}).phi;
    text_halo(im.dc(), 12, y - fs - 1, text::widen(num(phi)));
  }
  text_halo(im.dc(), 4, 1, L"λ→");
  text_halo(im.dc(), 4, im.h - fs - 3, L"φ↓");
  SelectObject(im.dc(), old);
}

void draw_marker(Image& im, geo::PointI p, const std::string& label) {
  HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 60, 60));
  HGDIOBJ op = SelectObject(im.dc(), pen);
  HGDIOBJ ob = SelectObject(im.dc(), GetStockObject(NULL_BRUSH));
  Ellipse(im.dc(), p.x - 12, p.y - 12, p.x + 13, p.y + 13);
  MoveToEx(im.dc(), p.x - 20, p.y, nullptr);
  LineTo(im.dc(), p.x + 21, p.y);
  MoveToEx(im.dc(), p.x, p.y - 20, nullptr);
  LineTo(im.dc(), p.x, p.y + 21);
  SelectObject(im.dc(), ob);
  SelectObject(im.dc(), op);
  DeleteObject(pen);
  if (!label.empty()) {
    static thread_local Font font;
    font.ensure(std::clamp(im.h / 56, 12, 18));
    HGDIOBJ old = SelectObject(im.dc(), font.f);
    text_halo(im.dc(), p.x + 16, p.y + 14, text::widen(label));
    SelectObject(im.dc(), old);
  }
}

void draw_box(Image& im, geo::RectI r, const std::string& label) {
  HPEN pen = CreatePen(PS_SOLID, 2, RGB(70, 220, 255));
  HGDIOBJ op = SelectObject(im.dc(), pen);
  HGDIOBJ ob = SelectObject(im.dc(), GetStockObject(NULL_BRUSH));
  Rectangle(im.dc(), r.x, r.y, r.right(), r.bottom());
  SelectObject(im.dc(), ob);
  SelectObject(im.dc(), op);
  DeleteObject(pen);
  if (!label.empty()) {
    static thread_local Font font;
    font.ensure(std::clamp(im.h / 62, 11, 16));
    HGDIOBJ old = SelectObject(im.dc(), font.f);
    text_halo(im.dc(), r.x + 3, std::max(0, r.y - font.px - 3), text::widen(label));
    SelectObject(im.dc(), old);
  }
}

namespace {
IWICImagingFactory* wic() {
  static IWICImagingFactory* f = [] {
    com_init_thread();
    IWICImagingFactory* p = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, reinterpret_cast<void**>(&p));
    return p;
  }();
  return f;
}
}  // namespace

Res<std::vector<u8>> encode(const Image& im, Fmt fmt, int quality) {
  com_init_thread();
  IWICImagingFactory* f = wic();
  if (!f) return fail(E_COM, "WIC is unavailable");
  ComPtr<IStream> stream;
  if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.put()))) return fail(E_COM, "cannot create memory stream");
  ComPtr<IWICBitmapEncoder> enc;
  if (FAILED(f->CreateEncoder(fmt == Fmt::Jpeg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng, nullptr, enc.put()))) return fail(E_COM, "cannot create encoder");
  if (FAILED(enc->Initialize(stream.get(), WICBitmapEncoderNoCache))) return fail(E_COM, "encoder init failed");
  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> props;
  if (FAILED(enc->CreateNewFrame(frame.put(), props.put()))) return fail(E_COM, "cannot create frame");
  if (fmt == Fmt::Jpeg && props) {
    PROPBAG2 opt{};
    wchar_t name[] = L"ImageQuality";
    opt.pstrName = name;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_R4;
    v.fltVal = static_cast<float>(std::clamp(quality, 10, 100)) / 100.0f;
    props->Write(1, &opt, &v);
  }
  if (FAILED(frame->Initialize(props.get()))) return fail(E_COM, "frame init failed");
  frame->SetSize(static_cast<UINT>(im.w), static_cast<UINT>(im.h));
  std::vector<u8> buf;
  if (fmt == Fmt::Jpeg) {
    WICPixelFormatGUID pf = GUID_WICPixelFormat24bppBGR;
    frame->SetPixelFormat(&pf);
    const size_t stride = (static_cast<size_t>(im.w) * 3 + 3) & ~size_t(3);
    buf.resize(stride * static_cast<size_t>(im.h));
    for (int y = 0; y < im.h; ++y) {
      const u8* s = im.bits + static_cast<size_t>(y) * im.stride();
      u8* d = buf.data() + static_cast<size_t>(y) * stride;
      for (int x = 0; x < im.w; ++x) {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        s += 4;
        d += 3;
      }
    }
    if (FAILED(frame->WritePixels(static_cast<UINT>(im.h), static_cast<UINT>(stride), static_cast<UINT>(buf.size()), buf.data()))) return fail(E_COM, "jpeg write failed");
  } else {
    WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&pf);
    buf.assign(im.bits, im.bits + im.stride() * static_cast<size_t>(im.h));
    for (size_t i = 3; i < buf.size(); i += 4) buf[i] = 0xFF;
    if (FAILED(frame->WritePixels(static_cast<UINT>(im.h), static_cast<UINT>(im.stride()), static_cast<UINT>(buf.size()), buf.data()))) return fail(E_COM, "png write failed");
  }
  if (FAILED(frame->Commit()) || FAILED(enc->Commit())) return fail(E_COM, "encode commit failed");
  HGLOBAL hg = nullptr;
  if (FAILED(GetHGlobalFromStream(stream.get(), &hg)) || !hg) return fail(E_COM, "cannot read encoded data");
  STATSTG st{};
  stream->Stat(&st, STATFLAG_NONAME);
  const size_t n = static_cast<size_t>(st.cbSize.QuadPart);
  void* p = GlobalLock(hg);
  std::vector<u8> out(static_cast<u8*>(p), static_cast<u8*>(p) + n);
  GlobalUnlock(hg);
  return out;
}

TileMap tiles(const Image& im, int tile) {
  TileMap m;
  m.tile = tile;
  m.cols = (im.w + tile - 1) / tile;
  m.rows = (im.h + tile - 1) / tile;
  m.h.assign(static_cast<size_t>(m.cols) * static_cast<size_t>(m.rows), 0);
  for (int ty = 0; ty < m.rows; ++ty) {
    const int y0 = ty * tile, y1 = std::min(im.h, y0 + tile);
    for (int tx = 0; tx < m.cols; ++tx) {
      const int x0 = tx * tile, x1 = std::min(im.w, x0 + tile);
      u64 acc = 0x9E3779B97F4A7C15ull;
      for (int y = y0; y < y1; ++y)
        acc = hash_combine(acc, hash64(im.bits + static_cast<size_t>(y) * im.stride() + static_cast<size_t>(x0) * 4, static_cast<size_t>(x1 - x0) * 4, acc));
      m.h[static_cast<size_t>(ty) * static_cast<size_t>(m.cols) + static_cast<size_t>(tx)] = acc;
    }
  }
  return m;
}

Diff diff(const TileMap& a, const TileMap& b, int w, int h) {
  Diff d;
  d.total_tiles = a.cols * a.rows;
  if (a.cols != b.cols || a.rows != b.rows || a.tile != b.tile) {
    d.changed_tiles = d.total_tiles;
    d.fraction = 1.0;
    d.bbox = {0, 0, w, h};
    return d;
  }
  int minx = INT32_MAX, miny = INT32_MAX, maxx = -1, maxy = -1;
  for (int ty = 0; ty < a.rows; ++ty)
    for (int tx = 0; tx < a.cols; ++tx) {
      const size_t i = static_cast<size_t>(ty) * static_cast<size_t>(a.cols) + static_cast<size_t>(tx);
      if (a.h[i] == b.h[i]) continue;
      ++d.changed_tiles;
      minx = std::min(minx, tx * a.tile);
      miny = std::min(miny, ty * a.tile);
      maxx = std::max(maxx, std::min(w, (tx + 1) * a.tile));
      maxy = std::max(maxy, std::min(h, (ty + 1) * a.tile));
    }
  d.fraction = d.total_tiles ? static_cast<double>(d.changed_tiles) / d.total_tiles : 0;
  if (d.changed_tiles) d.bbox = {minx, miny, maxx - minx, maxy - miny};
  return d;
}

}  // namespace dx::cap
