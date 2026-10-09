#include "core/engine/overlay.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/base/clock.hpp"
#include "core/base/text.hpp"
#include "core/win/window.hpp"

namespace dx::eng {
namespace {
constexpr UINT WM_DX_PUSH = WM_APP + 41;
constexpr wchar_t kClass[] = L"DeixionOverlay";
}  // namespace

Overlay& Overlay::get() {
  static Overlay o;
  return o;
}

void Overlay::start() {
  if (running_.exchange(true)) return;
  th_ = std::thread([this] { run(); });
  const u64 end = now_us() + 200000;
  while (!tid_.load() && now_us() < end) sleep_us(300);
}

void Overlay::stop() {
  if (!running_.exchange(false)) return;
  if (const DWORD t = tid_.load()) PostThreadMessageW(t, WM_QUIT, 0, 0);
  if (th_.joinable()) th_.join();
  tid_ = 0;
}

void Overlay::push(Item it) {
  it.born_ms = unix_ms();
  {
    std::lock_guard lk(mu_);
    items_.push_back(std::move(it));
    if (items_.size() > 24) items_.erase(items_.begin());
  }
  if (const DWORD t = tid_.load()) PostThreadMessageW(t, WM_DX_PUSH, 0, 0);
}

void Overlay::ripple(geo::PointI p, const std::string& label) {
  if (!running_) return;
  Item it;
  it.kind = 0;
  it.r = {p.x, p.y, 0, 0};
  it.label = label;
  it.life_ms = 650;
  push(std::move(it));
}

void Overlay::highlight(geo::RectI r, const std::string& label) {
  if (!running_) return;
  Item it;
  it.kind = 1;
  it.r = r;
  it.label = label;
  it.life_ms = 900;
  push(std::move(it));
}

LRESULT CALLBACK Overlay::wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCHITTEST) return HTTRANSPARENT;
  return DefWindowProcW(h, m, w, l);
}

void Overlay::run() {
  tid_ = GetCurrentThreadId();
  WNDCLASSEXW wc{sizeof wc};
  wc.lpfnWndProc = wndproc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kClass;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassExW(&wc);
  vs_ = win::virtual_screen();
  hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP, vs_.x, vs_.y, vs_.w, vs_.h,
                          nullptr, nullptr, wc.hInstance, nullptr);
  MSG m;
  UINT_PTR timer = 0;
  while (running_ && GetMessageW(&m, nullptr, 0, 0) > 0) {
    if (m.message == WM_DX_PUSH) {
      if (!timer) timer = SetTimer(nullptr, 0, 16, nullptr);
      draw();
      continue;
    }
    if (m.message == WM_TIMER && m.hwnd == nullptr) {
      draw();
      std::lock_guard lk(mu_);
      if (items_.empty() && timer) {
        KillTimer(nullptr, timer);
        timer = 0;
      }
      continue;
    }
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  if (timer) KillTimer(nullptr, timer);
  if (hwnd_) DestroyWindow(hwnd_);
  hwnd_ = nullptr;
  UnregisterClassW(kClass, wc.hInstance);
}

void Overlay::draw() {
  if (!hwnd_) return;
  std::vector<Item> items;
  const u64 now = unix_ms();
  {
    std::lock_guard lk(mu_);
    std::erase_if(items_, [&](const Item& i) { return now - i.born_ms >= i.life_ms; });
    items = items_;
  }
  if (items.empty()) {
    ShowWindow(hwnd_, SW_HIDE);
    return;
  }
  // 只重绘包含所有元素的包围盒，避免整块虚拟屏幕（多显示器时很大）每帧都刷。
  geo::RectI box{INT32_MAX, INT32_MAX, 0, 0};
  i32 rx = INT32_MIN, by = INT32_MIN;
  for (const auto& it : items) {
    geo::RectI r = it.kind == 0 ? geo::RectI{it.r.x - 80, it.r.y - 80, 560, 160} : geo::RectI{it.r.x - 8, it.r.y - 34, it.r.w + 400, it.r.h + 50};
    box.x = std::min(box.x, r.x);
    box.y = std::min(box.y, r.y);
    rx = std::max(rx, r.right());
    by = std::max(by, r.bottom());
  }
  box.w = rx - box.x;
  box.h = by - box.y;
  box = box.intersect(vs_);
  if (box.empty()) return;

  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = box.w;
  bi.bmiHeader.biHeight = -box.h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HDC sdc = GetDC(nullptr);
  HDC mdc = CreateCompatibleDC(sdc);
  HBITMAP bmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ old = SelectObject(mdc, bmp);
  std::memset(bits, 0, static_cast<size_t>(box.w) * static_cast<size_t>(box.h) * 4);
  auto* px = static_cast<u32*>(bits);
  auto put = [&](int x, int y, u8 r, u8 g, u8 b, double a) {
    if (x < 0 || y < 0 || x >= box.w || y >= box.h || a <= 0) return;
    a = std::min(a, 1.0);
    const u8 A = static_cast<u8>(a * 255);
    u32& d = px[static_cast<size_t>(y) * static_cast<size_t>(box.w) + static_cast<size_t>(x)];
    const u8 dA = static_cast<u8>(d >> 24);
    if (A < dA) return;
    d = (static_cast<u32>(A) << 24) | (static_cast<u32>(static_cast<u8>(r * a)) << 16) | (static_cast<u32>(static_cast<u8>(g * a)) << 8) | static_cast<u32>(static_cast<u8>(b * a));
  };
  for (const auto& it : items) {
    const double t = std::clamp(static_cast<double>(now - it.born_ms) / it.life_ms, 0.0, 1.0);
    const double fade = 1.0 - t;
    if (it.kind == 0) {
      const double cx = it.r.x - box.x, cy = it.r.y - box.y;
      const double rad = 8 + 34 * (1 - std::pow(1 - t, 3));
      const int R = static_cast<int>(rad) + 3;
      for (int y = -R; y <= R; ++y)
        for (int x = -R; x <= R; ++x) {
          const double d = std::sqrt(static_cast<double>(x * x + y * y));
          const double ring = 1.0 - std::min(1.0, std::fabs(d - rad) / 2.2);
          if (ring > 0) put(static_cast<int>(cx) + x, static_cast<int>(cy) + y, 255, 154, 46, ring * fade * 0.95);
          if (d < 4) put(static_cast<int>(cx) + x, static_cast<int>(cy) + y, 255, 229, 92, fade);
        }
    } else {
      const geo::RectI r{it.r.x - box.x, it.r.y - box.y, it.r.w, it.r.h};
      for (int k = 0; k < 2; ++k) {
        for (int x = r.x; x < r.x + r.w; ++x) {
          put(x, r.y + k, 61, 107, 255, fade * 0.95);
          put(x, r.y + r.h - 1 - k, 61, 107, 255, fade * 0.95);
        }
        for (int y = r.y; y < r.y + r.h; ++y) {
          put(r.x + k, y, 61, 107, 255, fade * 0.95);
          put(r.x + r.w - 1 - k, y, 61, 107, 255, fade * 0.95);
        }
      }
    }
  }
  // 标签用 GDI 画在 DIB 上，再把 alpha 补成不透明（GDI 不写 alpha）。
  HFONT font = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  HGDIOBJ oldf = SelectObject(mdc, font);
  SetBkMode(mdc, TRANSPARENT);
  for (const auto& it : items) {
    if (it.label.empty()) continue;
    const double fade = 1.0 - std::clamp(static_cast<double>(now - it.born_ms) / it.life_ms, 0.0, 1.0);
    const std::wstring w = text::widen(it.label);
    SIZE sz{};
    GetTextExtentPoint32W(mdc, w.c_str(), static_cast<int>(w.size()), &sz);
    const int x = (it.kind == 0 ? it.r.x + 18 : it.r.x) - box.x, y = (it.kind == 0 ? it.r.y + 16 : it.r.y - sz.cy - 8) - box.y;
    const u8 a = static_cast<u8>(std::clamp(fade * 235.0, 0.0, 235.0));
    for (int yy = y - 3; yy < y + sz.cy + 3; ++yy)
      for (int xx = x - 6; xx < x + sz.cx + 6; ++xx)
        if (xx >= 0 && yy >= 0 && xx < box.w && yy < box.h) {
          const u32 col = (static_cast<u32>(a) << 24) | (static_cast<u32>(20 * a / 255) << 16) | (static_cast<u32>(24 * a / 255) << 8) | static_cast<u32>(34 * a / 255);
          px[static_cast<size_t>(yy) * static_cast<size_t>(box.w) + static_cast<size_t>(xx)] = col;
        }
    SetTextColor(mdc, RGB(236, 240, 248));
    TextOutW(mdc, x, y, w.c_str(), static_cast<int>(w.size()));
    for (int yy = y; yy < y + sz.cy; ++yy)
      for (int xx = x; xx < x + sz.cx; ++xx)
        if (xx >= 0 && yy >= 0 && xx < box.w && yy < box.h) {
          u32& d = px[static_cast<size_t>(yy) * static_cast<size_t>(box.w) + static_cast<size_t>(xx)];
          if ((d >> 24) == 0 || ((d & 0xFFFFFF) != 0 && (d >> 24) < a)) d = (d & 0xFFFFFF) | (static_cast<u32>(a) << 24);
        }
  }
  SelectObject(mdc, oldf);
  DeleteObject(font);

  POINT pt{box.x, box.y};
  SIZE sz{box.w, box.h};
  POINT src{0, 0};
  BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  SetWindowPos(hwnd_, HWND_TOPMOST, box.x, box.y, box.w, box.h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  UpdateLayeredWindow(hwnd_, sdc, &pt, &sz, mdc, &src, 0, &bf, ULW_ALPHA);
  SelectObject(mdc, old);
  DeleteObject(bmp);
  DeleteDC(mdc);
  ReleaseDC(nullptr, sdc);
}

}  // namespace dx::eng
