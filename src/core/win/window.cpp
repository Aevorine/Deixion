#include "core/win/window.hpp"

#include <dwmapi.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <unordered_map>

#include "core/base/clock.hpp"
#include "core/base/text.hpp"

namespace dx::win {
namespace {
struct ExeCache {
  std::mutex mu;
  std::unordered_map<u32, std::pair<std::string, u64>> m;
};
ExeCache g_exe;

std::string wtext(HWND h, bool cls) {
  wchar_t buf[512];
  const int n = cls ? GetClassNameW(h, buf, 512) : GetWindowTextW(h, buf, 512);
  return n > 0 ? text::narrow(std::wstring_view(buf, static_cast<size_t>(n))) : std::string();
}

geo::RectI from_rect(const RECT& r) { return {r.left, r.top, r.right - r.left, r.bottom - r.top}; }

bool cloaked(HWND h) {
  DWORD c = 0;
  return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &c, sizeof c)) && c != 0;
}
}  // namespace

HWND to_hwnd(u64 v) { return reinterpret_cast<HWND>(static_cast<uintptr_t>(v)); }

std::string exe_name_of_pid(u32 pid) {
  const u64 now = unix_ms();
  {
    std::lock_guard lk(g_exe.mu);
    auto it = g_exe.m.find(pid);
    if (it != g_exe.m.end() && now - it->second.second < 5000) return it->second.first;
  }
  std::string name;
  if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = static_cast<DWORD>(std::size(buf));
    if (QueryFullProcessImageNameW(p, 0, buf, &n)) {
      std::wstring_view v(buf, n);
      const size_t s = v.find_last_of(L"\\/");
      name = text::narrow(s == std::wstring_view::npos ? v : v.substr(s + 1));
    }
    CloseHandle(p);
  }
  std::lock_guard lk(g_exe.mu);
  g_exe.m[pid] = {name, now};
  return name;
}

geo::RectI virtual_screen() {
  return {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), GetSystemMetrics(SM_CXVIRTUALSCREEN),
          GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}

u32 dpi_of(HWND h) {
  const UINT d = h ? GetDpiForWindow(h) : 0;
  return d ? d : 96;
}

geo::Frame screen_frame() {
  geo::Frame f;
  f.r = virtual_screen();
  f.dpi = GetDpiForSystem();
  return f;
}

geo::Frame client_frame(HWND h) {
  geo::Frame f;
  RECT rc{};
  GetClientRect(h, &rc);
  POINT p{0, 0};
  ClientToScreen(h, &p);
  f.r = {p.x, p.y, rc.right - rc.left, rc.bottom - rc.top};
  f.dpi = dpi_of(h);
  f.owner = reinterpret_cast<uintptr_t>(h);
  return f;
}

geo::Frame window_frame(HWND h) {
  geo::Frame f;
  RECT rc{};
  if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof rc))) GetWindowRect(h, &rc);
  f.r = from_rect(rc);
  f.dpi = dpi_of(h);
  f.owner = reinterpret_cast<uintptr_t>(h);
  return f;
}

bool is_own_process_window(HWND h) {
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  return pid == GetCurrentProcessId();
}

std::optional<WinInfo> info(HWND h) {
  if (!h || !IsWindow(h)) return std::nullopt;
  WinInfo w;
  w.hwnd = reinterpret_cast<uintptr_t>(h);
  DWORD pid = 0;
  w.tid = GetWindowThreadProcessId(h, &pid);
  w.pid = pid;
  w.title = wtext(h, false);
  w.cls = wtext(h, true);
  w.exe = exe_name_of_pid(pid);
  w.frame = window_frame(h).r;
  w.client = client_frame(h).r;
  w.dpi = dpi_of(h);
  w.visible = IsWindowVisible(h);
  w.minimized = IsIconic(h);
  w.maximized = IsZoomed(h);
  w.cloaked = cloaked(h);
  w.topmost = (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
  w.foreground = GetForegroundWindow() == h;
  return w;
}

std::vector<WinInfo> list_windows(const ListOpts& o) {
  struct Ctx {
    std::vector<HWND> hs;
  } ctx;
  EnumWindows(
      [](HWND h, LPARAM lp) -> BOOL {
        reinterpret_cast<Ctx*>(lp)->hs.push_back(h);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  std::vector<WinInfo> out;
  int z = 0;
  for (HWND h : ctx.hs) {
    ++z;
    if (!o.include_hidden && (!IsWindowVisible(h) || cloaked(h))) continue;
    const LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (!o.include_hidden && (ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW)) continue;
    if (!o.include_hidden && (ex & WS_EX_NOACTIVATE) && (ex & WS_EX_TRANSPARENT)) continue;
    auto wi = info(h);
    if (!wi) continue;
    wi->z = z;
    if (!o.include_untitled && wi->title.empty()) continue;
    if (!o.include_hidden && wi->frame.w <= 1 && wi->frame.h <= 1) continue;
    if (!o.filter.empty() && !text::icontains(wi->title, o.filter) && !text::icontains(wi->exe, o.filter) &&
        !text::icontains(wi->cls, o.filter))
      continue;
    out.push_back(std::move(*wi));
  }
  return out;
}

Json to_json(const WinInfo& w) {
  auto rect = [](const geo::RectI& r) {
    Json j = Json::object();
    j.set("x", r.x).set("y", r.y).set("w", r.w).set("h", r.h);
    return j;
  };
  char hb[24];
  std::snprintf(hb, sizeof hb, "0x%llX", static_cast<unsigned long long>(w.hwnd));
  Json j = Json::object();
  j.set("hwnd", hb).set("pid", w.pid).set("title", w.title).set("class", w.cls).set("exe", w.exe);
  j.set("frame", rect(w.frame)).set("client", rect(w.client)).set("dpi", w.dpi);
  j.set("visible", w.visible).set("minimized", w.minimized).set("maximized", w.maximized);
  j.set("foreground", w.foreground).set("topmost", w.topmost).set("z", w.z);
  return j;
}

HWND resolve_or_screen(std::string_view spec, bool& is_screen) {
  is_screen = false;
  const std::string s = text::lower(spec);
  if (s.empty() || s == "screen" || s == "desktop" || s == "all") {
    is_screen = true;
    return nullptr;
  }
  auto r = resolve(spec);
  return r ? *r : nullptr;
}

namespace {
struct Cand {
  HWND h;
  u32 pid;
  int z;
  std::string title, exe, cls;
};

std::vector<Cand> light_list(bool want_class) {
  struct Ctx {
    std::vector<HWND> hs;
  } ctx;
  EnumWindows(
      [](HWND h, LPARAM lp) -> BOOL {
        reinterpret_cast<Ctx*>(lp)->hs.push_back(h);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  std::vector<Cand> out;
  int z = 0;
  for (HWND h : ctx.hs) {
    ++z;
    if (!IsWindowVisible(h) || cloaked(h)) continue;
    const LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW)) continue;
    wchar_t buf[256];
    const int n = GetWindowTextW(h, buf, 256);
    if (n <= 0) continue;
    Cand c;
    c.h = h;
    c.z = z;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    c.pid = pid;
    c.title = text::narrow(std::wstring_view(buf, static_cast<size_t>(n)));
    c.exe = exe_name_of_pid(pid);
    if (want_class) c.cls = wtext(h, true);
    out.push_back(std::move(c));
  }
  return out;
}

struct ResolveCache {
  std::mutex mu;
  std::unordered_map<std::string, std::pair<HWND, u64>> m;
};
ResolveCache g_rc;
}  // namespace

Res<HWND> resolve(std::string_view spec) {
  std::string s(spec);
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
  const std::string l = text::lower(s);
  if (l == "active" || l == "foreground" || l == "front") {
    HWND h = GetForegroundWindow();
    if (!h) return fail(E_NOT_FOUND, "no foreground window");
    return GetAncestor(h, GA_ROOT);
  }
  auto hex_to = [](const std::string& v) -> std::optional<u64> {
    try {
      size_t used = 0;
      const u64 x = std::stoull(v, &used, 0);
      return used == v.size() ? std::optional<u64>(x) : std::nullopt;
    } catch (...) {
      return std::nullopt;
    }
  };
  std::string key, val;
  if (const size_t c = s.find(':'); c != std::string::npos && c > 0 && c < 8) {
    key = text::lower(s.substr(0, c));
    val = s.substr(c + 1);
  }
  if (key == "hwnd" || (key.empty() && l.starts_with("0x"))) {
    const auto v = hex_to(key.empty() ? s : val);
    if (!v) return fail(E_BAD_ARG, "bad hwnd: " + s);
    HWND h = to_hwnd(*v);
    if (!IsWindow(h)) return fail(E_NOT_FOUND, "window handle is gone: " + s);
    return h;
  }
  const u64 nowm = unix_ms();
  const std::string ckey = text::lower(s);
  {
    std::lock_guard lk(g_rc.mu);
    auto it = g_rc.m.find(ckey);
    if (it != g_rc.m.end() && nowm - it->second.second < 2500) {
      HWND h = it->second.first;
      if (IsWindow(h) && IsWindowVisible(h)) return h;
    }
  }
  const auto wins = light_list(key == "class");
  auto best = [&](auto&& score) -> Res<HWND> {
    double bs = 0;
    const Cand* bw = nullptr;
    for (const auto& w : wins) {
      const double sc = score(w);
      if (sc > bs + 1e-9) {
        bs = sc;
        bw = &w;
      }
    }
    if (!bw || bs < 0.3) return fail(E_NOT_FOUND, "no window matches: " + s);
    std::lock_guard lk(g_rc.mu);
    if (g_rc.m.size() > 64) g_rc.m.clear();
    g_rc.m[ckey] = {bw->h, nowm};
    return bw->h;
  };
  if (key == "pid") {
    const auto v = hex_to(val);
    return best([&](const Cand& w) { return v && w.pid == *v ? 1.0 : 0.0; });
  }
  if (key == "exe") return best([&](const Cand& w) { return text::similarity(val, w.exe); });
  if (key == "class") return best([&](const Cand& w) { return text::iequals(val, w.cls) ? 1.0 : text::icontains(w.cls, val) ? 0.7 : 0.0; });
  if (key == "title") return best([&](const Cand& w) { return text::similarity(val, w.title); });
  return best([&](const Cand& w) { return std::max(text::similarity(s, w.title), 0.95 * text::similarity(s, w.exe)); });
}

HWND deepest_child_at(HWND top, geo::PointI sp) {
  HWND cur = top;
  for (int guard = 0; guard < 16; ++guard) {
    POINT p{sp.x, sp.y};
    ScreenToClient(cur, &p);
    HWND c = ChildWindowFromPointEx(cur, p, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
    if (!c || c == cur) break;
    cur = c;
  }
  return cur;
}

}  // namespace dx::win
