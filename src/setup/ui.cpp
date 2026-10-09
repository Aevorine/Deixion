#include "ui.hpp"

#include <commctrl.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <uxtheme.h>

#include <algorithm>
#include <atomic>
#include <thread>

#include "sysops.hpp"

#ifndef DX_VERSION
#define DX_VERSION "0"
#endif
#define DX_W2(x) L##x
#define DX_W(x) DX_W2(x)

namespace dxsetup {
namespace {

enum Id { IdEdit = 100, IdBrowse, IdChk0, IdChk1, IdChk2, IdChk3, IdPrimary, IdSecondary, IdProgress, IdStatus };
constexpr UINT kMsgProgress = WM_APP + 1;
constexpr UINT kMsgDone = WM_APP + 2;

struct Palette {
  COLORREF bg, surface, surface2, surface3, line, text, text2, accent, on_accent, ok, danger;
};
constexpr Palette kLight{RGB(0xed, 0xf0, 0xf3), RGB(0xf7, 0xf9, 0xfa), RGB(0xe4, 0xe9, 0xed), RGB(0xd9, 0xe0, 0xe6), RGB(0xcf, 0xd7, 0xde),
                         RGB(0x1d, 0x25, 0x2c), RGB(0x46, 0x52, 0x5e), RGB(0x1d, 0x65, 0x85), RGB(0xf4, 0xfa, 0xfc), RGB(0x25, 0x70, 0x4a), RGB(0xa8, 0x36, 0x3d)};
constexpr Palette kDark{RGB(0x14, 0x18, 0x1c), RGB(0x1a, 0x1f, 0x24), RGB(0x23, 0x2a, 0x31), RGB(0x2c, 0x35, 0x3d), RGB(0x2f, 0x39, 0x42),
                        RGB(0xdb, 0xe2, 0xe8), RGB(0xaa, 0xb6, 0xc1), RGB(0x7f, 0xc3, 0xd8), RGB(0x0b, 0x1a, 0x20), RGB(0x66, 0xc5, 0x8e), RGB(0xef, 0x8d, 0x8a)};

struct Strings {
  const wchar_t *where, *browse, *start, *desktop, *autostart, *claude, *install, *update, *uninstall, *cancel, *finish, *retry, *close, *launch, *purge;
  const wchar_t* stage[7];
  const wchar_t *done, *e_dir, *e_payload, *e_write, *e_busy, *w_wv2, *w_claude;
};
constexpr Strings kZh{L"安装位置", L"浏览", L"开始菜单", L"桌面快捷方式", L"开机自启", L"接入 Claude Code", L"安装", L"更新", L"卸载", L"取消", L"完成", L"重试", L"关闭", L"启动 Deixion", L"同时删除用户数据",
                      {L"准备", L"关闭运行中的 Deixion", L"解压", L"写入", L"注册", L"接入 Claude Code", L"完成"},
                      L"完成", L"无法写入该位置", L"安装包已损坏", L"写入失败，已回滚", L"Deixion 无法退出", L"缺少 WebView2 运行库", L"未能接入 Claude Code"};
constexpr Strings kEn{L"Install location", L"Browse", L"Start menu", L"Desktop shortcut", L"Launch at sign-in", L"Connect Claude Code", L"Install", L"Update", L"Uninstall", L"Cancel", L"Finish", L"Retry", L"Close", L"Launch Deixion", L"Also delete user data",
                      {L"Preparing", L"Closing Deixion", L"Unpacking", L"Writing", L"Registering", L"Connecting Claude Code", L"Done"},
                      L"Done", L"Cannot write to this location", L"Package is damaged", L"Write failed, rolled back", L"Deixion would not quit", L"WebView2 runtime missing", L"Claude Code not connected"};

struct FontSet {
  HFONT lat{}, cjk{};
  int asc{}, desc{};
};

bool is_cjk(wchar_t c) { return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFFEF); }

struct Ui {
  HINSTANCE inst{};
  Mode mode{Mode::Install};
  const Payload* pl{};
  Options opt;
  bool installed_before{false};
  bool zh{true}, dark{false};
  const Strings* S{};
  Palette P{};
  UINT dpi{96};
  FontSet f_body, f_meta, f_title;
  HWND hwnd{}, edit{}, browse{}, chk[4]{}, primary{}, secondary{}, prog{}, status{};
  HBRUSH br_bg{}, br_surface{};
  HICON icon_big{};
  enum class Page { Options, Working, Done } page{Page::Options};
  bool ok{false};
  std::wstring status_text;
  COLORREF status_color{};
  std::atomic<bool> busy{false};
  int exit_code{1};

  int px(int v) const { return MulDiv(v, static_cast<int>(dpi), 96); }
};
Ui g;

bool system_dark() {
  DWORD v = 1, n = sizeof v;
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &n);
  return v == 0;
}

HFONT make_font(const wchar_t* face, double pt, bool bold) {
  return CreateFontW(-MulDiv(static_cast<int>(pt * 10), static_cast<int>(g.dpi), 720), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_ROMAN, face);
}

void build_fonts() {
  for (auto* f : {&g.f_body, &g.f_meta, &g.f_title}) {
    if (f->lat) DeleteObject(f->lat);
    if (f->cjk) DeleteObject(f->cjk);
  }
  auto fill = [](FontSet& f, double pt, bool bold) {
    f.lat = make_font(L"Times New Roman", pt, bold);
    f.cjk = make_font(L"SimSun", pt, bold);
    HDC dc = GetDC(nullptr);
    TEXTMETRICW a{}, b{};
    HGDIOBJ old = SelectObject(dc, f.lat);
    GetTextMetricsW(dc, &a);
    SelectObject(dc, f.cjk);
    GetTextMetricsW(dc, &b);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    f.asc = std::max(a.tmAscent, b.tmAscent);
    f.desc = std::max(a.tmDescent, b.tmDescent);
  };
  fill(g.f_body, 12, false);
  fill(g.f_meta, 10.5, false);
  fill(g.f_title, 14, true);
}

int text_width(HDC dc, const FontSet& f, const std::wstring& s) {
  int total = 0;
  for (size_t i = 0; i < s.size();) {
    const bool c = is_cjk(s[i]);
    size_t j = i;
    while (j < s.size() && is_cjk(s[j]) == c) ++j;
    SIZE sz{};
    HGDIOBJ old = SelectObject(dc, c ? f.cjk : f.lat);
    GetTextExtentPoint32W(dc, s.c_str() + i, static_cast<int>(j - i), &sz);
    SelectObject(dc, old);
    total += sz.cx;
    i = j;
  }
  return total;
}

// 汉字用宋体、其余用 Times New Roman，按基线对齐地逐段绘制。
void draw_text(HDC dc, const FontSet& f, const std::wstring& s, int x, const RECT& box, COLORREF color, bool center = false) {
  if (center) x = box.left + (box.right - box.left - text_width(dc, f, s)) / 2;
  const int base = box.top + (box.bottom - box.top - (f.asc + f.desc)) / 2 + f.asc;
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, color);
  SetTextAlign(dc, TA_LEFT | TA_BASELINE);
  for (size_t i = 0; i < s.size();) {
    const bool c = is_cjk(s[i]);
    size_t j = i;
    while (j < s.size() && is_cjk(s[j]) == c) ++j;
    HGDIOBJ old = SelectObject(dc, c ? f.cjk : f.lat);
    TextOutW(dc, x, base, s.c_str() + i, static_cast<int>(j - i));
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str() + i, static_cast<int>(j - i), &sz);
    SelectObject(dc, old);
    x += sz.cx;
    i = j;
  }
}

void fill_round(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF line) {
  HBRUSH b = CreateSolidBrush(fill);
  HPEN p = CreatePen(PS_SOLID, 1, line);
  HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
  RoundRect(dc, r.left, r.top, r.right, r.bottom, radius * 2, radius * 2);
  SelectObject(dc, ob);
  SelectObject(dc, op);
  DeleteObject(b);
  DeleteObject(p);
}

std::wstring label_of(HWND h) {
  wchar_t b[256]{};
  GetWindowTextW(h, b, 256);
  return b;
}

LRESULT on_button_draw(NMCUSTOMDRAW* d) {
  if (d->dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
  const HWND h = d->hdr.hwndFrom;
  const bool is_check = h == g.chk[0] || h == g.chk[1] || h == g.chk[2] || h == g.chk[3];
  const bool hot = d->uItemState & CDIS_HOT, down = d->uItemState & CDIS_SELECTED, off = d->uItemState & CDIS_DISABLED;
  RECT r = d->rc;
  HDC dc = d->hdc;
  FillRect(dc, &r, g.br_bg);
  const std::wstring txt = label_of(h);
  if (is_check) {
    const bool on = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const int s = g.px(18);
    RECT b{r.left + g.px(2), r.top + (r.bottom - r.top - s) / 2, r.left + g.px(2) + s, r.top + (r.bottom - r.top - s) / 2 + s};
    fill_round(dc, b, g.px(4), on ? g.P.accent : g.P.surface, hot || on ? g.P.accent : g.P.text2);
    if (on) {
      HPEN p = CreatePen(PS_SOLID, std::max(2, g.px(2)), g.P.on_accent);
      HGDIOBJ op = SelectObject(dc, p);
      MoveToEx(dc, b.left + s * 22 / 100, b.top + s * 52 / 100, nullptr);
      LineTo(dc, b.left + s * 42 / 100, b.top + s * 72 / 100);
      LineTo(dc, b.left + s * 80 / 100, b.top + s * 28 / 100);
      SelectObject(dc, op);
      DeleteObject(p);
    }
    RECT t{b.right + g.px(10), r.top, r.right, r.bottom};
    draw_text(dc, g.f_body, txt, t.left, t, off ? g.P.text2 : g.P.text);
    if (d->uItemState & CDIS_FOCUS) {
      RECT fr{t.left - g.px(4), r.top + g.px(2), t.left + text_width(dc, g.f_body, txt) + g.px(4), r.bottom - g.px(2)};
      fill_round(dc, fr, g.px(4), g.P.bg, g.P.accent);
      draw_text(dc, g.f_body, txt, t.left, t, g.P.text);
    }
  } else {
    const bool prim = h == g.primary;
    COLORREF fill = prim ? g.P.accent : g.P.surface2;
    if (down) fill = prim ? g.P.text2 : g.P.surface3;
    else if (hot) fill = prim ? RGB(GetRValue(g.P.accent) * 9 / 10, GetGValue(g.P.accent) * 9 / 10, GetBValue(g.P.accent) * 9 / 10) : g.P.surface3;
    if (g.dark && prim && hot) fill = RGB(std::min(255, GetRValue(g.P.accent) * 11 / 10), std::min(255, GetGValue(g.P.accent) * 11 / 10), std::min(255, GetBValue(g.P.accent) * 11 / 10));
    fill_round(dc, r, g.px(6), off ? g.P.surface2 : fill, prim ? fill : g.P.line);
    draw_text(dc, g.f_body, txt, 0, r, off ? g.P.text2 : (prim ? g.P.on_accent : g.P.text), true);
    if (d->uItemState & CDIS_FOCUS) {
      RECT fr = r;
      InflateRect(&fr, -g.px(3), -g.px(3));
      HPEN p = CreatePen(PS_DOT, 1, prim ? g.P.on_accent : g.P.accent);
      HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
      Rectangle(dc, fr.left, fr.top, fr.right, fr.bottom);
      SelectObject(dc, op);
      SelectObject(dc, ob);
      DeleteObject(p);
    }
  }
  return CDRF_SKIPDEFAULT;
}

void set_text(HWND h, const std::wstring& s) { SetWindowTextW(h, s.c_str()); }

void layout() {
  RECT c;
  GetClientRect(g.hwnd, &c);
  const int W = c.right, m = g.px(28), bh = g.px(34), ch = g.px(26);
  const bool opts = g.page == Ui::Page::Options, work = g.page == Ui::Page::Working, done = g.page == Ui::Page::Done;
  const bool un = g.mode == Mode::Uninstall;
  auto place = [&](HWND h, int x, int y, int w, int hh, bool show) {
    SetWindowPos(h, nullptr, x, y, w, hh, SWP_NOZORDER | SWP_NOACTIVATE | (show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
  };
  const int bw = g.px(96), gap = g.px(12);
  place(g.edit, m + g.px(8), g.px(138), W - 2 * m - g.px(8) - g.px(88) - gap - g.px(8), g.px(22), opts && !un);
  place(g.browse, W - m - g.px(88), g.px(132), g.px(88), g.px(34), opts && !un);
  int y = un ? g.px(120) : g.px(184);
  for (int i = 0; i < 4; ++i) {
    const bool show = (opts && (un ? i == 0 : true)) || (done && i == 0 && g.ok && !un);
    place(g.chk[i], m, y, W - 2 * m, ch, show);
    if (show) y += g.px(32);
  }
  place(g.status, m, g.px(150), W - 2 * m, g.px(30), work || done);
  place(g.prog, m, g.px(196), W - 2 * m, g.px(6), work);
  const int by = c.bottom - m - bh + g.px(6);
  place(g.primary, W - m - bw, by, bw, bh, !work);
  place(g.secondary, W - m - bw * 2 - gap, by, bw, bh, opts || (done && !g.ok));
}

void apply_texts() {
  const Strings& S = *g.S;
  const bool un = g.mode == Mode::Uninstall;
  if (g.page == Ui::Page::Options) {
    if (un) {
      set_text(g.chk[0], S.purge);
    } else {
      set_text(g.chk[0], S.start);
      set_text(g.chk[1], S.desktop);
      set_text(g.chk[2], S.autostart);
      set_text(g.chk[3], S.claude);
    }
    set_text(g.primary, un ? S.uninstall : (g.installed_before ? S.update : S.install));
    set_text(g.secondary, S.cancel);
  } else if (g.page == Ui::Page::Done) {
    if (g.ok && !un) {
      set_text(g.chk[0], S.launch);
      SendMessageW(g.chk[0], BM_SETCHECK, BST_CHECKED, 0);
    }
    set_text(g.primary, g.ok ? S.finish : S.close);
    set_text(g.secondary, S.retry);
  }
}

void go(Ui::Page p) {
  g.page = p;
  apply_texts();
  layout();
  InvalidateRect(g.hwnd, nullptr, TRUE);
}

void set_status(const std::wstring& s, COLORREF c) {
  g.status_text = s;
  g.status_color = c;
  set_text(g.status, s);
  InvalidateRect(g.status, nullptr, TRUE);
}

std::wstring browse_folder() {
  std::wstring out;
  IFileOpenDialog* d = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, reinterpret_cast<void**>(&d)))) {
    DWORD fl = 0;
    d->GetOptions(&fl);
    d->SetOptions(fl | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (SUCCEEDED(d->Show(g.hwnd))) {
      IShellItem* it = nullptr;
      if (SUCCEEDED(d->GetResult(&it))) {
        PWSTR p = nullptr;
        if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
          out = p;
          CoTaskMemFree(p);
        }
        it->Release();
      }
    }
    d->Release();
  }
  return out;
}

bool valid_dir(const fs::path& p) {
  const std::wstring s = p.wstring();
  return s.size() >= 4 && s.size() < 200 && p.is_absolute() && s.find_first_of(L"<>\"|?*") == std::wstring::npos;
}

void start_work() {
  Options o = g.opt;
  if (g.mode == Mode::Install) {
    wchar_t b[1024]{};
    GetWindowTextW(g.edit, b, 1024);
    o.dir = sys::normalize_dir(fs::path(b));
    if (!valid_dir(o.dir)) {
      set_status(g.S->e_dir, g.P.danger);
      g.ok = false;
      go(Ui::Page::Done);
      return;
    }
    o.start_menu = SendMessageW(g.chk[0], BM_GETCHECK, 0, 0) == BST_CHECKED;
    o.desktop = SendMessageW(g.chk[1], BM_GETCHECK, 0, 0) == BST_CHECKED;
    o.autostart = SendMessageW(g.chk[2], BM_GETCHECK, 0, 0) == BST_CHECKED;
    o.claude = SendMessageW(g.chk[3], BM_GETCHECK, 0, 0) == BST_CHECKED;
  } else {
    o.purge = SendMessageW(g.chk[0], BM_GETCHECK, 0, 0) == BST_CHECKED;
  }
  g.opt = o;
  go(Ui::Page::Working);
  set_status(g.S->stage[StPrepare], g.P.text2);
  SendMessageW(g.prog, PBM_SETPOS, 0, 0);
  g.busy = true;
  HWND hw = g.hwnd;
  const Mode mode = g.mode;
  const Payload* pl = g.pl;
  std::thread([hw, mode, pl, o] {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto prog = [hw](int pct, int st) { PostMessageW(hw, kMsgProgress, static_cast<WPARAM>(pct), static_cast<LPARAM>(st)); };
    Result r = mode == Mode::Install ? install(*pl, o, prog) : uninstall(o, prog);
    auto* heap = new Result(std::move(r));
    PostMessageW(hw, kMsgDone, 0, reinterpret_cast<LPARAM>(heap));
    if (SUCCEEDED(co)) CoUninitialize();
  }).detach();
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CREATE: {
      g.hwnd = h;
      auto mk = [&](const wchar_t* cls, DWORD style, int id, const FontSet& f) {
        HWND c = CreateWindowExW(0, cls, L"", WS_CHILD | style, 0, 0, 10, 10, h, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.inst, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(f.lat), TRUE);
        return c;
      };
      g.edit = mk(L"EDIT", WS_TABSTOP | ES_AUTOHSCROLL, IdEdit, g.f_body);
      SetWindowTheme(g.edit, L"", L"");
      g.browse = mk(L"BUTTON", WS_TABSTOP | BS_PUSHBUTTON, IdBrowse, g.f_body);
      for (int i = 0; i < 4; ++i) g.chk[i] = mk(L"BUTTON", WS_TABSTOP | BS_AUTOCHECKBOX, IdChk0 + i, g.f_body);
      g.primary = mk(L"BUTTON", WS_TABSTOP | BS_DEFPUSHBUTTON, IdPrimary, g.f_body);
      g.secondary = mk(L"BUTTON", WS_TABSTOP | BS_PUSHBUTTON, IdSecondary, g.f_body);
      g.prog = mk(PROGRESS_CLASSW, PBS_SMOOTH, IdProgress, g.f_body);
      SetWindowTheme(g.prog, L"", L"");
      SendMessageW(g.prog, PBM_SETRANGE32, 0, 100);
      SendMessageW(g.prog, PBM_SETBARCOLOR, 0, g.P.accent);
      SendMessageW(g.prog, PBM_SETBKCOLOR, 0, g.P.surface3);
      g.status = mk(L"STATIC", SS_OWNERDRAW, IdStatus, g.f_body);
      set_text(g.browse, g.S->browse);
      set_text(g.edit, g.opt.dir.wstring());
      SendMessageW(g.chk[0], BM_SETCHECK, g.opt.start_menu ? BST_CHECKED : BST_UNCHECKED, 0);
      SendMessageW(g.chk[1], BM_SETCHECK, g.opt.desktop ? BST_CHECKED : BST_UNCHECKED, 0);
      SendMessageW(g.chk[2], BM_SETCHECK, g.opt.autostart ? BST_CHECKED : BST_UNCHECKED, 0);
      SendMessageW(g.chk[3], BM_SETCHECK, g.opt.claude ? BST_CHECKED : BST_UNCHECKED, 0);
      apply_texts();
      layout();
      return 0;
    }
    case WM_SIZE:
      if (g.hwnd) layout();
      return 0;
    case WM_ERASEBKGND: {
      RECT r;
      GetClientRect(h, &r);
      FillRect(reinterpret_cast<HDC>(w), &r, g.br_bg);
      return 1;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      RECT c;
      GetClientRect(h, &c);
      const int mx = g.px(28);
      if (g.icon_big) DrawIconEx(dc, mx, g.px(24), g.icon_big, g.px(48), g.px(48), 0, nullptr, DI_NORMAL);
      RECT tr{mx + g.px(64), g.px(24), c.right, g.px(48)};
      draw_text(dc, g.f_title, L"Deixion", tr.left, tr, g.P.text);
      RECT vr{mx + g.px(64), g.px(48), c.right, g.px(72)};
      draw_text(dc, g.f_meta, DX_W(DX_VERSION), vr.left, vr, g.P.text2);
      HPEN pen = CreatePen(PS_SOLID, 1, g.P.line);
      HGDIOBJ op = SelectObject(dc, pen);
      MoveToEx(dc, mx, g.px(92), nullptr);
      LineTo(dc, c.right - mx, g.px(92));
      SelectObject(dc, op);
      DeleteObject(pen);
      if (g.page == Ui::Page::Options && g.mode == Mode::Install) {
        RECT lr{mx, g.px(104), c.right, g.px(126)};
        draw_text(dc, g.f_meta, g.S->where, lr.left, lr, g.P.text2);
        RECT fr{mx, g.px(130), c.right - mx - g.px(88) - g.px(12), g.px(166)};
        fill_round(dc, fr, g.px(6), g.P.surface, g.P.line);
      }
      EndPaint(h, &ps);
      return 0;
    }
    case WM_DRAWITEM: {
      auto* d = reinterpret_cast<DRAWITEMSTRUCT*>(l);
      if (d->CtlID == IdStatus) {
        FillRect(d->hDC, &d->rcItem, g.br_bg);
        draw_text(d->hDC, g.f_body, g.status_text, d->rcItem.left, d->rcItem, g.status_color);
        return TRUE;
      }
      break;
    }
    case WM_NOTIFY: {
      auto* nh = reinterpret_cast<NMHDR*>(l);
      if (nh->code == NM_CUSTOMDRAW && nh->idFrom >= IdBrowse && nh->idFrom <= IdSecondary) return on_button_draw(reinterpret_cast<NMCUSTOMDRAW*>(l));
      break;
    }
    case WM_CTLCOLOREDIT: {
      HDC dc = reinterpret_cast<HDC>(w);
      SetTextColor(dc, g.P.text);
      SetBkColor(dc, g.P.surface);
      return reinterpret_cast<LRESULT>(g.br_surface);
    }
    case WM_COMMAND: {
      const int id = LOWORD(w);
      if (id == IdBrowse) {
        const std::wstring p = browse_folder();
        if (!p.empty()) set_text(g.edit, sys::normalize_dir(fs::path(p)).wstring());
      } else if (id == IdPrimary || id == IDOK) {
        if (g.busy) return 0;
        if (g.page == Ui::Page::Options) start_work();
        else {
          if (g.page == Ui::Page::Done && g.ok && g.mode == Mode::Install && SendMessageW(g.chk[0], BM_GETCHECK, 0, 0) == BST_CHECKED)
            sys::spawn(L"\"" + (g.opt.dir / L"Deixion.exe").wstring() + L"\"", g.opt.dir);
          g.exit_code = g.ok ? 0 : 1;
          DestroyWindow(h);
        }
      } else if (id == IdSecondary || id == IDCANCEL) {
        if (g.busy) return 0;
        if (g.page == Ui::Page::Done && !g.ok) go(Ui::Page::Options);
        else {
          g.exit_code = g.page == Ui::Page::Done ? (g.ok ? 0 : 1) : 2;
          DestroyWindow(h);
        }
      }
      return 0;
    }
    case kMsgProgress:
      SendMessageW(g.prog, PBM_SETPOS, w, 0);
      set_status(g.S->stage[std::clamp<int>(static_cast<int>(l), 0, 6)], g.P.text2);
      return 0;
    case kMsgDone: {
      std::unique_ptr<Result> r(reinterpret_cast<Result*>(l));
      g.busy = false;
      g.ok = r->ok;
      std::wstring msg = g.S->done;
      COLORREF col = g.P.ok;
      if (!r->ok) {
        col = g.P.danger;
        switch (r->why) {
          case Fail::Dir: msg = g.S->e_dir; break;
          case Fail::Payload: msg = g.S->e_payload; break;
          case Fail::Busy: msg = g.S->e_busy; break;
          default: msg = g.S->e_write; break;
        }
      } else if (!r->note.empty()) {
        msg += L" · ";
        msg += r->note == L"claude" ? g.S->w_claude : g.S->w_wv2;
      }
      set_status(msg, col);
      go(Ui::Page::Done);
      return 0;
    }
    case WM_DPICHANGED: {
      g.dpi = HIWORD(w);
      build_fonts();
      for (HWND c : {g.edit, g.browse, g.chk[0], g.chk[1], g.chk[2], g.chk[3], g.primary, g.secondary, g.status}) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g.f_body.lat), TRUE);
      const RECT* r = reinterpret_cast<RECT*>(l);
      SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_CLOSE:
      if (g.busy) return 0;
      DestroyWindow(h);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(g.exit_code);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

}  // namespace

bool use_chinese() { return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE; }

int run_ui(HINSTANCE inst, Mode mode, const Payload* pl, Options opt, bool installed_before) {
  INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
  InitCommonControlsEx(&icc);
  g.inst = inst;
  g.mode = mode;
  g.pl = pl;
  g.opt = std::move(opt);
  g.installed_before = installed_before;
  g.zh = use_chinese();
  g.S = g.zh ? &kZh : &kEn;
  g.dark = system_dark();
  g.P = g.dark ? kDark : kLight;
  g.status_color = g.P.text2;
  g.dpi = GetDpiForSystem();
  build_fonts();
  g.br_bg = CreateSolidBrush(g.P.bg);
  g.br_surface = CreateSolidBrush(g.P.surface);
  g.icon_big = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, g.px(48), g.px(48), LR_DEFAULTCOLOR));

  WNDCLASSEXW wc{sizeof wc};
  wc.lpfnWndProc = proc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
  wc.hIconSm = wc.hIcon;
  wc.hbrBackground = g.br_bg;
  wc.lpszClassName = L"DeixionSetup";
  RegisterClassExW(&wc);

  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  RECT r{0, 0, g.px(520), g.px(380)};
  AdjustWindowRectExForDpi(&r, style, FALSE, 0, g.dpi);
  const int w = r.right - r.left, h = r.bottom - r.top;
  const int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
  HWND hw = CreateWindowExW(0, wc.lpszClassName, mode == Mode::Install ? L"Deixion" : L"Deixion", style, (sx - w) / 2, (sy - h) / 2, w, h, nullptr, nullptr, inst, nullptr);
  if (!hw) return 1;
  const BOOL dark = g.dark;
  DwmSetWindowAttribute(hw, 20, &dark, sizeof dark);
  ShowWindow(hw, SW_SHOW);
  UpdateWindow(hw);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(hw, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  return g.exit_code;
}

}  // namespace dxsetup
