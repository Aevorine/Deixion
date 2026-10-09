#include "app/sys.hpp"

#include <dwmapi.h>
#include <shellapi.h>
#include <windows.h>

#include <cstring>

#include "core/base/paths.hpp"
#include "core/base/text.hpp"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#define DWMWA_CAPTION_COLOR 35
#define DWMWA_TEXT_COLOR 36
#endif

namespace dx::app::sys {
namespace {
constexpr wchar_t kRun[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
}

Res<void> set_autostart(bool on) {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRun, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return fail(E_WIN32, "cannot open the Run key");
  Defer d([&] { RegCloseKey(k); });
  if (!on) {
    RegDeleteValueW(k, L"Deixion");
    return {};
  }
  const std::wstring v = L"\"" + paths::exe_path().wstring() + L"\" --tray";
  if (RegSetValueExW(k, L"Deixion", 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()), static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t))) != ERROR_SUCCESS) return fail(E_WIN32, "cannot write the Run key");
  return {};
}

bool autostart_enabled() {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRun, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
  const bool r = RegQueryValueExW(k, L"Deixion", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
  RegCloseKey(k);
  return r;
}

Res<void> copy_text(HWND owner, const std::string& s) {
  const std::wstring w = text::widen(s);
  if (!OpenClipboard(owner)) return fail(E_BUSY, "clipboard is busy");
  Defer d([] { CloseClipboard(); });
  EmptyClipboard();
  HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
  if (!g) return fail(E_INTERNAL, "out of memory");
  std::memcpy(GlobalLock(g), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
  GlobalUnlock(g);
  SetClipboardData(CF_UNICODETEXT, g);
  return {};
}

Res<void> copy_image(HWND owner, const cap::Image& im) {
  const size_t row = static_cast<size_t>(im.w) * 4, bytes = row * static_cast<size_t>(im.h);
  HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + bytes);
  if (!g) return fail(E_INTERNAL, "out of memory");
  auto* p = static_cast<u8*>(GlobalLock(g));
  BITMAPINFOHEADER bi{};
  bi.biSize = sizeof bi;
  bi.biWidth = im.w;
  bi.biHeight = im.h;
  bi.biPlanes = 1;
  bi.biBitCount = 32;
  bi.biCompression = BI_RGB;
  std::memcpy(p, &bi, sizeof bi);
  for (int y = 0; y < im.h; ++y) std::memcpy(p + sizeof bi + static_cast<size_t>(y) * row, im.bits + static_cast<size_t>(im.h - 1 - y) * row, row);
  GlobalUnlock(g);
  if (!OpenClipboard(owner)) {
    GlobalFree(g);
    return fail(E_BUSY, "clipboard is busy");
  }
  EmptyClipboard();
  SetClipboardData(CF_DIB, g);
  CloseClipboard();
  return {};
}

void open_path(const std::wstring& path) { ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

void open_url(const std::string& url) {
  if (!url.starts_with("https://")) return;
  ShellExecuteW(nullptr, L"open", text::widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool system_dark() {
  DWORD v = 1, n = sizeof v;
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &n);
  return v == 0;
}

void style_caption(HWND hwnd, bool dark, u32 bg, u32 fg) {
  const BOOL d = dark ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &d, sizeof d);
  const COLORREF cbg = RGB((bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF), cfg = RGB((fg >> 16) & 0xFF, (fg >> 8) & 0xFF, fg & 0xFF);
  DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &cbg, sizeof cbg);
  DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &cbg, sizeof cbg);
  DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &cfg, sizeof cfg);
}

void allow_dark_menus() {
  if (HMODULE ux = LoadLibraryW(L"uxtheme.dll")) {
    using SetMode = int(WINAPI*)(int);
    using Flush = void(WINAPI*)();
    if (auto f = reinterpret_cast<SetMode>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(135))))) f(1);
    if (auto f = reinterpret_cast<Flush>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136))))) f();
  }
}

}  // namespace dx::app::sys
