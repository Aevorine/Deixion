// 网页 ⇄ 原生桥：请求 {id,m,p} → 响应 {id,ok,r|e}。app.* 在界面线程，其余（引擎、更新、Claude 接入）进工作线程。
#include <windows.h>
#include <shellapi.h>

#include "app/app.hpp"
#include "app/claude.hpp"
#include "app/sys.hpp"
#include "app/updater.hpp"
#include "core/base/log.hpp"
#include "core/base/paths.hpp"
#include "core/base/text.hpp"

#ifndef DX_VERSION
#define DX_VERSION "0.0.0"
#endif

namespace dx::app {
namespace {

u32 parse_rgb(const std::string& s, u32 d) {
  if (s.size() != 7 || s[0] != '#') return d;
  u32 v = 0;
  for (size_t i = 1; i < 7; ++i) {
    const char c = s[i];
    const int x = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (x < 0) return d;
    v = v << 4 | static_cast<u32>(x);
  }
  return v;
}

bool is_app_method(std::string_view m) { return m.starts_with("app."); }
bool is_side_method(std::string_view m) { return m.starts_with("update.") || m.starts_with("claude."); }

}  // namespace

void App::reply(const Json& id, Res<Json> r) {
  Json m = Json::object();
  m.set("id", id);
  if (r) {
    m.set("ok", true).set("r", std::move(*r));
  } else {
    m.set("ok", false).set("e", Json::object().set("code", err_name(r.error().code)).set("msg", r.error().msg));
  }
  std::string s = m.dump();
  post_ui([this, s = std::move(s)] {
    if (wv_.ready()) wv_.post(s);
  });
}

void App::handle_js(std::string_view text) {
  auto j = Json::parse(text);
  if (!j || !j->is_obj()) return;
  const Json id = (*j)["id"];
  const std::string m = (*j)["m"].str_or("");
  Json p = (*j)["p"];
  if (!p.is_obj()) p = Json::object();
  if (m.empty()) {
    reply(id, fail(E_BAD_ARG, "missing method"));
    return;
  }
  if (is_app_method(m)) {
    // 界面线程直接处理（句柄与托盘只能在这里碰）。
    reply(id, call_app(m, p));
    return;
  }
  run_bg([this, id, m, p = std::move(p)] {
    if (is_side_method(m)) {
      reply(id, call_app(m, p));
      return;
    }
    reply(id, eng::Engine::get().call(m, p));
  });
}

Res<Json> App::call_app(const std::string& m, const Json& p) {
  auto& updater = Updater::get();
  if (m == "app.info") {
    Json j = Json::object();
    std::string wvv;
    WebView::runtime_installed(&wvv);
    j.set("version", DX_VERSION).set("webview", wv_version_.empty() ? wvv : wv_version_).set("portable", paths::portable());
    j.set("exe", text::narrow(paths::exe_path().wstring())).set("data_dir", text::narrow(paths::data_dir().wstring()));
    j.set("hotkeys", hotkeys_.status_json()).set("ui_from_disk", pack_.from_disk()).set("clients", ipc_.clients());
    j.set("autostart_registered", sys::autostart_enabled()).set("system_dark", sys::system_dark()).set("update", updater.state());
    j.set("maximized", hwnd_ && IsZoomed(hwnd_) != 0);
    return j;
  }
  if (m == "app.window") {
    const std::string op = p["op"].as_str();
    if (op == "minimize") minimize_window();
    else if (op == "maximize") ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
    else if (op == "hide") {
      save_placement();
      ShowWindow(hwnd_, SW_HIDE);
      wv_.set_visible(false);
    } else if (op == "toggle") toggle_window();
    else if (op == "close") PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    else if (op == "quit") PostMessageW(hwnd_, msg_quit_, 0, 0);
    else return fail(E_BAD_ARG, "unknown window op");
    return Json::object();
  }
  if (m == "app.theme") {
    apply_theme(p["dark"].as_bool(true), parse_rgb(p["bg"].as_str(), bg_), parse_rgb(p["fg"].as_str(), fg_));
    return Json::object();
  }
  if (m == "app.cmd") {
    const std::string n = p["name"].as_str();
    if (n == "toggle_mode") cmd_toggle_mode();
    else if (n == "toggle_pause") cmd_toggle_pause();
    else if (n == "undo") cmd_undo();
    else if (n == "shot") cmd_shot();
    else if (n == "stop") cmd_stop();
    else return fail(E_BAD_ARG, "unknown command");
    return Json::object();
  }
  if (m == "app.open") {
    // 只允许打开自己的数据 / 日志目录与 https 链接，不接受网页给的任意路径。
    const std::string kind = p["kind"].as_str();
    if (kind == "data") sys::open_path(paths::data_dir().wstring());
    else if (kind == "logs") sys::open_path(paths::logs_dir().wstring());
    else if (kind == "url") {
      const std::string u = p["target"].as_str();
      if (!u.starts_with("https://")) return fail(E_DENIED, "only https links can be opened");
      sys::open_url(u);
    } else return fail(E_BAD_ARG, "unknown open kind");
    return Json::object();
  }
  if (m == "app.copy") {
    if (auto r = sys::copy_text(hwnd_, p["text"].as_str()); !r) return std::unexpected(r.error());
    return Json::object();
  }
  if (m == "app.hotkeys") return hotkeys_.status_json();

  if (m == "update.state") return updater.state();
  if (m == "update.check") {
    updater.check(true);
    return updater.state();
  }
  if (m == "update.download") {
    updater.download();
    return updater.state();
  }
  if (m == "update.apply") {
    if (auto r = updater.apply(); !r) return std::unexpected(r.error());
    PostMessageW(hwnd_, msg_quit_, 0, 0);
    return Json::object();
  }

  if (m == "claude.status") return claude::status(ipc_.clients());
  if (m == "claude.install") return claude::install();
  if (m == "claude.remove") return claude::remove();
  if (m == "claude.config") return claude::config_snippet();

  return fail(E_UNSUPPORTED, "unknown method " + m);
}

}  // namespace dx::app
