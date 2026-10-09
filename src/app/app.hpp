#pragma once
#include <windows.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include "app/hotkeys.hpp"
#include "app/ui_pack.hpp"
#include "app/wv.hpp"
#include "core/base/i18n.hpp"
#include "core/engine/engine.hpp"
#include "core/ipc/pipe.hpp"

namespace dx::app {

inline constexpr wchar_t kHostClass[] = L"DeixionHost";

class App {
 public:
  static App& get();
  int run(HINSTANCE hi, bool start_hidden);

  HWND hwnd() const { return hwnd_; }
  // 线程安全：把任务排到界面线程 / 工作线程。
  void post_ui(std::function<void()> fn);
  void run_bg(std::function<void()> fn);
  void push_event(const std::string& ev, const Json& data);
  // 当前界面语言：设置里的 language，auto 时跟随系统。托盘菜单、提示、原生对话框都用它。
  static i18n::Lang ui_lang();

 private:
  App() = default;
  static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
  LRESULT proc(HWND, UINT, WPARAM, LPARAM);

  bool create_window(bool start_hidden);
  void init_webview();
  void layout();
  void save_placement();
  void restore_placement(int* x, int* y, int* w, int* h, bool* max);

  void toggle_window();
  void on_activate(bool active);
  void show_window();
  void minimize_window();
  void quit();
  void on_settings(const eng::Settings& s);
  void apply_theme(bool dark, u32 bg, u32 fg);

  void add_tray();
  void remove_tray();
  void refresh_tray(const char* balloon = nullptr);
  void tray_menu(POINT at);
  void do_menu_command(int id);

  void cmd_toggle_mode();
  void cmd_toggle_pause();
  void cmd_undo();
  void cmd_shot();
  void cmd_stop();
  void toast(const std::string& text, const char* kind = "info", bool balloon = false);

  void handle_js(std::string_view json);
  Res<Json> call_app(const std::string& method, const Json& p);
  void reply(const Json& id, Res<Json> r);

  HINSTANCE hi_{nullptr};
  HWND hwnd_{nullptr};
  bool start_hidden_{false};
  bool wv_ok_{false};
  bool quitting_{false};
  bool shown_once_{false};
  bool tray_added_{false};
  bool want_max_{false};
  u64 lost_ts_{0};
  UINT taskbar_created_{0};
  UINT msg_show_{0}, msg_quit_{0};
  bool dark_{true};
  u32 bg_{0x15181d}, fg_{0xdde2ea};
  std::string wv_version_;
  std::string wv_error_;

  WebView wv_;
  UiPack pack_;
  Hotkeys hotkeys_;
  ipc::Server ipc_;
  u64 settings_sub_{0}, action_sub_{0}, log_sub_{0};

  std::mutex q_mu_;
  std::condition_variable q_cv_;
  std::deque<std::function<void()>> q_;
  std::vector<std::thread> pool_;
  bool pool_stop_{false};
};

}  // namespace dx::app
