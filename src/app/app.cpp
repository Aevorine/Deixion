#include "app/app.hpp"

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <fstream>

#include "app/sys.hpp"
#include "app/updater.hpp"
#include "core/base/clock.hpp"
#include "core/base/i18n.hpp"
#include "core/base/log.hpp"
#include "core/base/paths.hpp"
#include "core/base/text.hpp"
#include "core/win/window.hpp"

namespace dx::app {
namespace {
constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_RUN_UI = WM_APP + 2;
constexpr UINT TIMER_LOG = 1;
constexpr UINT TIMER_UPDATE = 2;

enum Cmd : int {
  ID_SHOW = 1001,
  ID_MODE_BG = 1010,
  ID_MODE_FG,
  ID_HOP,
  ID_TRACE,
  ID_PAUSE = 1020,
  ID_UNDO,
  ID_SHOT,
  ID_STOP,
  ID_CLAUDE = 1030,
  ID_UPDATE,
  ID_AUTOSTART,
  ID_DATA,
  ID_QUIT = 1099,
};

std::mutex g_log_mu;
std::vector<Json> g_pending_logs;
}  // namespace

i18n::Lang App::ui_lang() { return i18n::resolve(eng::SettingsStore::get().snapshot().language); }

App& App::get() {
  static App a;
  return a;
}

void App::post_ui(std::function<void()> fn) {
  if (!hwnd_) return;
  auto* p = new std::function<void()>(std::move(fn));
  if (!PostMessageW(hwnd_, WM_RUN_UI, 0, reinterpret_cast<LPARAM>(p))) delete p;
}

void App::run_bg(std::function<void()> fn) {
  {
    std::lock_guard lk(q_mu_);
    q_.push_back(std::move(fn));
  }
  q_cv_.notify_one();
}

void App::push_event(const std::string& ev, const Json& data) {
  Json m = Json::object();
  m.set("ev", ev).set("d", data);
  std::string s = m.dump();
  post_ui([this, s = std::move(s)] {
    if (wv_.ready()) wv_.post(s);
  });
}

void App::toast(const std::string& text, const char* kind, bool balloon) {
  Json d = Json::object();
  d.set("text", text).set("kind", kind);
  push_event("toast", d);
  if (balloon) post_ui([this, text] { refresh_tray(text.c_str()); });
}

LRESULT CALLBACK App::wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  App* self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  if (m == WM_NCCREATE) {
    self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hwnd_ = h;
  }
  return self ? self->proc(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

void App::restore_placement(int* x, int* y, int* w, int* h, bool* max) {
  std::ifstream f(paths::data_dir() / L"window.json", std::ios::binary);
  std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  auto j = Json::parse(s);
  *max = false;
  if (!j) return;
  const RECT r{static_cast<LONG>((*j)["x"].as_int()), static_cast<LONG>((*j)["y"].as_int()), static_cast<LONG>((*j)["x"].as_int() + (*j)["w"].as_int()),
               static_cast<LONG>((*j)["y"].as_int() + (*j)["h"].as_int())};
  if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL) || (*j)["w"].as_int() < 400 || (*j)["h"].as_int() < 300) return;
  *x = r.left;
  *y = r.top;
  *w = r.right - r.left;
  *h = r.bottom - r.top;
  *max = (*j)["max"].as_bool();
}

void App::save_placement() {
  if (!hwnd_) return;
  WINDOWPLACEMENT wp{sizeof wp};
  GetWindowPlacement(hwnd_, &wp);
  const RECT& r = wp.rcNormalPosition;
  Json j = Json::object();
  j.set("x", r.left).set("y", r.top).set("w", r.right - r.left).set("h", r.bottom - r.top).set("max", wp.showCmd == SW_SHOWMAXIMIZED);
  std::ofstream f(paths::data_dir() / L"window.json", std::ios::binary | std::ios::trunc);
  const std::string s = j.dump();
  f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

bool App::create_window(bool start_hidden) {
  start_hidden_ = start_hidden;
  WNDCLASSEXW wc{sizeof wc};
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = wndproc;
  wc.hInstance = hi_;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kHostClass;
  wc.hIcon = static_cast<HICON>(LoadImageW(hi_, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
  wc.hIconSm = static_cast<HICON>(LoadImageW(hi_, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
  wc.hbrBackground = CreateSolidBrush(RGB((bg_ >> 16) & 0xFF, (bg_ >> 8) & 0xFF, bg_ & 0xFF));
  RegisterClassExW(&wc);
  const UINT dpi = GetDpiForSystem();
  int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = MulDiv(1280, static_cast<int>(dpi), 96), h = MulDiv(800, static_cast<int>(dpi), 96);
  bool max = false;
  restore_placement(&x, &y, &w, &h, &max);
  hwnd_ = CreateWindowExW(0, kHostClass, L"Deixion", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, h, nullptr, nullptr, hi_, this);
  if (!hwnd_) return false;
  sys::style_caption(hwnd_, dark_, bg_, fg_);
  want_max_ = max;
  return true;
}

void App::layout() {
  if (!hwnd_) return;
  RECT rc;
  GetClientRect(hwnd_, &rc);
  wv_.resize(rc);
}

void App::init_webview() {
  if (!pack_.load()) {
    wv_error_ = "UI files are missing";
    LOGE("app", "ui pack missing");
    return;
  }
  wv_.on_resource([this](const std::string& p, std::string& mime, const void*& d, size_t& n) { return pack_.get(p, mime, d, n); });
  wv_.on_message([this](std::string_view s) { handle_js(s); });
  wv_.on_navigated([this](bool ok) {
    if (!ok) LOGW("app", "navigation failed");
    if (shown_once_) return;
    shown_once_ = true;
    if (!start_hidden_) show_window();
  });
  const std::wstring udd = (paths::data_dir() / L"webview").wstring();
  wv_.create(hwnd_, udd, [this](bool ok, std::string info) {
    wv_ok_ = ok;
    if (!ok) {
      wv_error_ = info;
      LOGE("app", "WebView2 unavailable: {}", info);
      return;
    }
    wv_version_ = info;
    wv_.set_background((bg_ >> 16) & 0xFF, (bg_ >> 8) & 0xFF, bg_ & 0xFF);
    layout();
    wv_.navigate(L"https://app.deixion/index.html");
  });
}

void App::show_window() {
  if (!hwnd_) return;
  if (!wv_ok_) {
    if (wv_error_.empty()) return;  // 还在创建中
    if (MessageBoxW(hwnd_,
                    i18n::pick(ui_lang(), L"界面需要微软 WebView2 运行时。\n是否打开下载页面？\n\n（没有界面时，托盘菜单、快捷键和 Claude Code 调用仍可使用。）",
                               L"The window needs the Microsoft WebView2 runtime.\nOpen the download page?\n\n(Without it, the tray menu, hotkeys and Claude Code calls keep working.)"),
                    L"Deixion", MB_YESNO | MB_ICONINFORMATION) == IDYES)
      sys::open_url("https://go.microsoft.com/fwlink/p/?LinkId=2124703");
    return;
  }
  if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
  else ShowWindow(hwnd_, want_max_ ? SW_SHOWMAXIMIZED : SW_SHOW);
  want_max_ = false;
  SetForegroundWindow(hwnd_);
  wv_.set_visible(true);
  wv_.focus();
  push_event("shown", Json::object());
}

void App::minimize_window() {
  if (hwnd_) ShowWindow(hwnd_, SW_MINIMIZE);
}

void App::toggle_window() {
  if (!hwnd_) return;
  const bool visible = IsWindowVisible(hwnd_) && !IsIconic(hwnd_);
  HWND fg = GetForegroundWindow();
  bool front = fg == hwnd_;
  if (!front && visible && GetTickCount64() - lost_ts_ < 2000) {
    // 点托盘图标时焦点先落到任务栏：刚从本窗口让出且前台是任务栏，仍算“窗口在前面”。
    wchar_t cls[64]{};
    GetClassNameW(fg, cls, 64);
    front = !lstrcmpW(cls, L"Shell_TrayWnd") || !lstrcmpW(cls, L"NotifyIconOverflowWindow") || !lstrcmpW(cls, L"TopLevelWindowForOverflowXamlIsland");
  }
  if (visible && front) minimize_window();
  else show_window();
}

void App::on_activate(bool active) {
  if (!active) lost_ts_ = GetTickCount64();
  else lost_ts_ = 0;
}

void App::quit() {
  quitting_ = true;
  save_placement();
  DestroyWindow(hwnd_);
}

void App::apply_theme(bool dark, u32 bg, u32 fg) {
  dark_ = dark;
  bg_ = bg;
  fg_ = fg;
  if (hwnd_) sys::style_caption(hwnd_, dark, bg, fg);
  wv_.set_background((bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF);
}

void App::on_settings(const eng::Settings& s) {
  post_ui([this, s] {
    if (hwnd_) {
      const auto st = hotkeys_.apply(hwnd_, s.hotkeys);
      for (const auto& h : st)
        if (!h.ok && !h.chord.empty()) LOGW("app", "hotkey {} ({}) is taken by another program", h.name, h.chord);
    }
    (void)sys::set_autostart(s.autostart);
    refresh_tray();
    push_event("settings", s.to_json());
    push_event("hotkeys", hotkeys_.status_json());
  });
}

// —— 托盘 ——

void App::add_tray() {
  NOTIFYICONDATAW nid{sizeof nid};
  nid.hWnd = hwnd_;
  nid.uID = 1;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
  nid.uCallbackMessage = WM_TRAY;
  HICON ic = nullptr;
  if (FAILED(LoadIconMetric(hi_, MAKEINTRESOURCEW(1), LIM_SMALL, &ic))) ic = LoadIconW(hi_, MAKEINTRESOURCEW(1));
  nid.hIcon = ic;
  wcscpy_s(nid.szTip, L"Deixion");
  tray_added_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
  nid.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &nid);
  refresh_tray();
}

void App::remove_tray() {
  if (!tray_added_) return;
  NOTIFYICONDATAW nid{sizeof nid};
  nid.hWnd = hwnd_;
  nid.uID = 1;
  Shell_NotifyIconW(NIM_DELETE, &nid);
  tray_added_ = false;
}

void App::refresh_tray(const char* balloon) {
  if (!tray_added_) return;
  const auto st = eng::SettingsStore::get().snapshot();
  NOTIFYICONDATAW nid{sizeof nid};
  nid.hWnd = hwnd_;
  nid.uID = 1;
  nid.uFlags = NIF_TIP | NIF_SHOWTIP;
  std::wstring tip = L"Deixion · ";
  const i18n::Lang lg = ui_lang();
  tip += st.paused ? i18n::pick(lg, L"已暂停", L"Paused") : (st.foreground() ? i18n::pick(lg, L"前台模式", L"Foreground mode") : i18n::pick(lg, L"后台模式", L"Background mode"));
  wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
  if (balloon) {
    nid.uFlags |= NIF_INFO | NIF_REALTIME;
    wcsncpy_s(nid.szInfoTitle, L"Deixion", _TRUNCATE);
    wcsncpy_s(nid.szInfo, text::widen(balloon).c_str(), _TRUNCATE);
    nid.dwInfoFlags = NIIF_NONE | NIIF_NOSOUND;
  }
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::tray_menu(POINT at) {
  const auto st = eng::SettingsStore::get().snapshot();
  HMENU m = CreatePopupMenu();
  auto add = [&](UINT id, const wchar_t* t, bool checked = false, bool bold = false) {
    MENUITEMINFOW mi{sizeof mi};
    mi.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
    mi.wID = id;
    mi.dwTypeData = const_cast<wchar_t*>(t);
    mi.fState = (checked ? MFS_CHECKED : 0) | (bold ? MFS_DEFAULT : 0);
    InsertMenuItemW(m, GetMenuItemCount(m), TRUE, &mi);
  };
  auto sep = [&] { AppendMenuW(m, MF_SEPARATOR, 0, nullptr); };
  const bool visible = IsWindowVisible(hwnd_) && !IsIconic(hwnd_);
  const i18n::Lang lg = ui_lang();
  auto T = [lg](const wchar_t* zh, const wchar_t* en) { return i18n::pick(lg, zh, en); };
  add(ID_SHOW, visible ? T(L"最小化窗口", L"Minimize window") : T(L"显示窗口", L"Show window"), false, true);
  sep();
  add(ID_MODE_BG, T(L"后台模式（不打扰你）", L"Background mode (won't disturb you)"), !st.foreground());
  add(ID_MODE_FG, T(L"前台模式（显示操作过程）", L"Foreground mode (show the actions)"), st.foreground());
  add(ID_HOP, T(L"允许短暂切前台兜底", L"Allow a brief foreground fallback"), st.allow_hop);
  add(ID_TRACE, T(L"前台操作显示轨迹", L"Show the trail in foreground mode"), st.overlay);
  sep();
  add(ID_PAUSE, st.paused ? T(L"继续接收操作", L"Resume accepting actions") : T(L"暂停接收操作", L"Pause accepting actions"));
  add(ID_UNDO, T(L"撤销上一步操作", L"Undo the last action"));
  add(ID_SHOT, T(L"截图（含经纬网格）到剪贴板", L"Screenshot with Meridian grid to clipboard"));
  add(ID_STOP, T(L"紧急停止当前批处理", L"Emergency stop the current batch"));
  sep();
  add(ID_CLAUDE, T(L"接入 Claude Code…", L"Connect Claude Code…"));
  add(ID_UPDATE, T(L"检查更新", L"Check for updates"));
  add(ID_AUTOSTART, T(L"开机自启", L"Start at sign-in"), st.autostart);
  add(ID_DATA, T(L"打开数据目录", L"Open data folder"));
  sep();
  add(ID_QUIT, T(L"退出 Deixion", L"Quit Deixion"));
  SetForegroundWindow(hwnd_);
  const int cmd = TrackPopupMenuEx(m, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_BOTTOMALIGN | TPM_LEFTALIGN, at.x, at.y, hwnd_, nullptr);
  DestroyMenu(m);
  PostMessageW(hwnd_, WM_NULL, 0, 0);
  if (cmd) do_menu_command(cmd);
}

void App::do_menu_command(int id) {
  auto& store = eng::SettingsStore::get();
  const auto st = store.snapshot();
  switch (id) {
    case ID_SHOW: toggle_window(); break;
    case ID_MODE_BG: (void)store.update(Json::object().set("mode", "background")); break;
    case ID_MODE_FG: (void)store.update(Json::object().set("mode", "foreground")); break;
    case ID_HOP: (void)store.update(Json::object().set("allow_hop", !st.allow_hop)); break;
    case ID_TRACE: (void)store.update(Json::object().set("overlay", !st.overlay)); break;
    case ID_PAUSE: cmd_toggle_pause(); break;
    case ID_UNDO: cmd_undo(); break;
    case ID_SHOT: cmd_shot(); break;
    case ID_STOP: cmd_stop(); break;
    case ID_CLAUDE:
      show_window();
      push_event("navigate", Json::object().set("page", "claude"));
      break;
    case ID_UPDATE:
      show_window();
      push_event("navigate", Json::object().set("page", "settings"));
      Updater::get().check(true);
      break;
    case ID_AUTOSTART: (void)store.update(Json::object().set("autostart", !st.autostart)); break;
    case ID_DATA: sys::open_path(paths::data_dir().wstring()); break;
    case ID_QUIT: quit(); break;
    default: break;
  }
}

// —— 快捷键与菜单共用的命令 ——

void App::cmd_toggle_mode() {
  auto& store = eng::SettingsStore::get();
  const bool fg = store.snapshot().foreground();
  (void)store.update(Json::object().set("mode", fg ? "background" : "foreground"));
  const i18n::Lang lg = ui_lang();
  toast(fg ? i18n::pick(lg, "已切换到后台模式", "Switched to background mode") : i18n::pick(lg, "已切换到前台模式", "Switched to foreground mode"), "info",
        !IsWindowVisible(hwnd_) || IsIconic(hwnd_));
}

void App::cmd_toggle_pause() {
  auto& store = eng::SettingsStore::get();
  const bool p = store.snapshot().paused;
  (void)store.update(Json::object().set("paused", !p));
  const i18n::Lang lg = ui_lang();
  toast(p ? i18n::pick(lg, "已继续接收操作", "Accepting actions again") : i18n::pick(lg, "已暂停接收操作", "Paused: not accepting actions"), p ? "info" : "warn",
        !IsWindowVisible(hwnd_) || IsIconic(hwnd_));
}

void App::cmd_undo() {
  run_bg([this] {
    auto r = eng::Engine::get().call("rollback", Json::object().set("count", 1));
    const i18n::Lang lg = ui_lang();
    toast(r ? std::string(i18n::pick(lg, "已撤销上一步", "Last action undone")) : std::string(i18n::pick(lg, "无法撤销：", "Cannot undo: ")) + r.error().msg, r ? "info" : "warn", true);
  });
}

void App::cmd_shot() {
  run_bg([this] {
    auto s = cap::shoot_screen(win::virtual_screen());
    if (!s) {
      toast(std::string(i18n::pick(ui_lang(), "截图失败：", "Screenshot failed: ")) + s.error().msg, "warn", true);
      return;
    }
    double f = 1;
    auto small = cap::scaled(*s->img, 2400, &f);
    cap::Image* im = small ? small.get() : s->img.get();
    cap::draw_grid(*im, cap::GridSpec{});
    post_ui([this, a = std::shared_ptr<cap::Image>(small ? std::move(small) : std::move(s->img))] {
      auto r = sys::copy_image(hwnd_, *a);
      const i18n::Lang lg = ui_lang();
      toast(r ? i18n::pick(lg, "截图已复制到剪贴板", "Screenshot copied to the clipboard") : i18n::pick(lg, "复制失败", "Copy failed"), r ? "info" : "warn", true);
    });
  });
}

void App::cmd_stop() {
  eng::Engine::get().request_stop();
  toast(i18n::pick(ui_lang(), "已发出紧急停止", "Emergency stop sent"), "warn", true);
}

// —— 窗口过程 ——

LRESULT App::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == taskbar_created_ && taskbar_created_) {
    tray_added_ = false;
    add_tray();
    return 0;
  }
  if (m == msg_show_ && msg_show_) {
    show_window();
    return 0;
  }
  if (m == msg_quit_ && msg_quit_) {
    quit();
    return 0;
  }
  switch (m) {
    case WM_SIZE:
      // 最小化 / 隐藏时让网页停止渲染，托盘常驻时 CPU 接近 0。
      if (w == SIZE_MINIMIZED) {
        wv_.set_visible(false);
      } else {
        wv_.set_visible(IsWindowVisible(h) != 0);
        layout();
      }
      return 0;
    case WM_GETMINMAXINFO: {
      auto* mm = reinterpret_cast<MINMAXINFO*>(l);
      const UINT dpi = GetDpiForWindow(h);
      mm->ptMinTrackSize.x = MulDiv(960, static_cast<int>(dpi), 96);
      mm->ptMinTrackSize.y = MulDiv(620, static_cast<int>(dpi), 96);
      return 0;
    }
    case WM_DPICHANGED: {
      const RECT* r = reinterpret_cast<RECT*>(l);
      SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_ACTIVATE:
      on_activate(LOWORD(w) != WA_INACTIVE);
      return DefWindowProcW(h, m, w, l);
    case WM_SETTINGCHANGE:
      if (l && lstrcmpW(reinterpret_cast<LPCWSTR>(l), L"ImmersiveColorSet") == 0) push_event("system_theme", Json::object().set("dark", sys::system_dark()));
      return 0;
    case WM_CLOSE:
      if (quitting_ || !eng::SettingsStore::get().snapshot().close_to_tray) {
        quit();
        return 0;
      }
      save_placement();
      ShowWindow(h, SW_HIDE);
      wv_.set_visible(false);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_HOTKEY:
      switch (static_cast<int>(w)) {
        case HK_TOGGLE: toggle_window(); break;
        case HK_MODE: cmd_toggle_mode(); break;
        case HK_PAUSE: cmd_toggle_pause(); break;
        case HK_UNDO: cmd_undo(); break;
        case HK_SHOT: cmd_shot(); break;
        case HK_STOP: cmd_stop(); break;
      }
      return 0;
    case WM_TRAY: {
      const UINT ev = LOWORD(l);
      if (ev == NIN_SELECT || ev == NIN_KEYSELECT) toggle_window();
      else if (ev == WM_CONTEXTMENU) tray_menu(POINT{GET_X_LPARAM(w), GET_Y_LPARAM(w)});
      return 0;
    }
    case WM_RUN_UI: {
      std::unique_ptr<std::function<void()>> fn(reinterpret_cast<std::function<void()>*>(l));
      (*fn)();
      return 0;
    }
    case WM_TIMER:
      if (w == TIMER_LOG) {
        std::vector<Json> batch;
        {
          std::lock_guard lk(g_log_mu);
          batch.swap(g_pending_logs);
        }
        if (!batch.empty() && wv_.ready()) {
          Json arr = Json::array();
          for (auto& j : batch) arr.push(std::move(j));
          push_event("log", Json::object().set("records", std::move(arr)));
        }
      } else if (w == TIMER_UPDATE) {
        KillTimer(h, TIMER_UPDATE);
        if (eng::SettingsStore::get().snapshot().check_updates) Updater::get().check(false);
      }
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

int App::run(HINSTANCE hi, bool start_hidden) {
  hi_ = hi;
  OleInitialize(nullptr);
  INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);
  sys::allow_dark_menus();
  dark_ = sys::system_dark();
  if (!dark_) {
    bg_ = 0xeef0f4;
    fg_ = 0x1e2530;
  }
  taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");
  msg_show_ = RegisterWindowMessageW(L"Deixion.Show");
  msg_quit_ = RegisterWindowMessageW(L"Deixion.Quit");

  if (auto r = eng::Engine::get().start(); !r) {
    MessageBoxW(nullptr, text::widen(std::string(i18n::pick(ui_lang(), "引擎启动失败：", "The engine failed to start: ")) + r.error().msg).c_str(), L"Deixion", MB_ICONERROR);
    return 1;
  }
  const unsigned n = 4;
  for (unsigned i = 0; i < n; ++i)
    pool_.emplace_back([this] {
      com_init_thread();
      for (;;) {
        std::function<void()> job;
        {
          std::unique_lock lk(q_mu_);
          q_cv_.wait(lk, [&] { return pool_stop_ || !q_.empty(); });
          if (pool_stop_ && q_.empty()) return;
          job = std::move(q_.front());
          q_.pop_front();
        }
        // 工作线程里逃出的异常会直接 std::terminate 整个应用：一个任务出错不该带走托盘与全部连接。
        try {
          job();
        } catch (const std::exception& ex) {
          LOGE("app", "background job failed: {}", ex.what());
        } catch (...) {
          LOGE("app", "background job failed");
        }
      }
    });

  if (!create_window(start_hidden)) return 1;
  if (auto r = ipc_.start([](std::string_view m, const Json& p) { return eng::Engine::get().call(m, p); }); !r) LOGW("app", "IPC server: {}", r.error().msg);

  settings_sub_ = eng::SettingsStore::get().subscribe([this](const eng::Settings& s) { on_settings(s); });
  action_sub_ = eng::Engine::get().subscribe_actions([this](const Json& ev) { push_event("action", ev); });
  log_sub_ = Log::get().subscribe([](const LogRec& r) {
    Json o = Json::object();
    o.set("id", r.id).set("t", r.ts_ms).set("lv", lv_name(r.lv)).set("cat", r.cat).set("msg", r.msg);
    std::lock_guard lk(g_log_mu);
    if (g_pending_logs.size() < 500) g_pending_logs.push_back(std::move(o));
  });
  Updater::get().set_notify([this](const Json& s) { push_event("update", s); });
  SetTimer(hwnd_, TIMER_LOG, 150, nullptr);
  SetTimer(hwnd_, TIMER_UPDATE, 20000, nullptr);

  add_tray();
  on_settings(eng::SettingsStore::get().snapshot());
  init_webview();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  KillTimer(hwnd_, TIMER_LOG);
  hotkeys_.clear(hwnd_);
  remove_tray();
  eng::SettingsStore::get().unsubscribe(settings_sub_);
  eng::Engine::get().unsubscribe_actions(action_sub_);
  Log::get().unsubscribe(log_sub_);
  ipc_.stop();
  Updater::get().shutdown();  // 汇合更新器的工作线程：否则静态析构时 std::thread 仍可汇合，进程以 0xc0000409 崩溃退出
  {
    std::lock_guard lk(q_mu_);
    pool_stop_ = true;
  }
  q_cv_.notify_all();
  for (auto& t : pool_) t.join();
  wv_.close();
  eng::Engine::get().shutdown();
  OleUninitialize();
  return static_cast<int>(msg.wParam);
}

}  // namespace dx::app
