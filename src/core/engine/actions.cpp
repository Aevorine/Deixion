#include <unordered_set>
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>

#include "core/base/clock.hpp"
#include "core/base/log.hpp"
#include "core/base/text.hpp"
#include "core/engine/activity.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/engine_util.hpp"
#include "core/input/input.hpp"

namespace dx::eng {
namespace {

input::Button btn_of(const Json& p) {
  const std::string b = text::lower(p["button"].str_or("left"));
  return b == "right" ? input::Button::Right : b == "middle" ? input::Button::Middle : input::Button::Left;
}

bool has_point(const Json& p) { return p.has("element") || p.has("find") || p.has("at") || p.has("code") || p.has("px") || p.has("screen"); }

HWND top_at(geo::PointI px) {
  POINT pt{px.x, px.y};
  HWND h = WindowFromPoint(pt);
  return h ? GetAncestor(h, GA_ROOT) : nullptr;
}

std::string label_of(const std::string& verb, const std::string& what) { return what.empty() ? verb : verb + " · " + what; }

int clampi(i64 v, int lo, int hi) { return static_cast<int>(std::clamp<i64>(v, lo, hi)); }

// 输入类动作不许操作 Deixion 自己的窗口：否则模型可以点界面里的开关，绕过只属于用户的授权（短暂切前台、更新、移除接入）。
// 截图、读元素这类被动查询不受限制。
constexpr const char* kSelfMsg = "Deixion's own windows cannot be operated by input actions; those switches belong to the user";
bool is_own(u32 pid, const std::string& app) { return pid == GetCurrentProcessId() || app == "deixion.exe" || app == "deixion-cli.exe"; }
bool own_window(HWND w) {
  DWORD pid = 0;
  GetWindowThreadProcessId(w, &pid);
  return pid && is_own(pid, text::lower(win::exe_name_of_pid(pid)));
}

// 写入文本值。经典 Win32 编辑框上 UIA 的 SetValue 会在目标进程里自己抢前台，前台锁拦不住；
// 直接发 WM_SETTEXT 与系统自带的 UIA 代理等价，而且拦得住。其余控件走 UIA 的 ValuePattern。
Res<void> set_text(const uia::Node& n, const std::wstring& v) {
  if (n.native && (n.cls == "Edit" || n.cls.rfind("RichEdit", 0) == 0)) {
    HWND c = win::to_hwnd(n.native);
    if (c && IsWindow(c)) {
      if (!(n.flags & uia::F_ENABLED)) return fail(E_DENIED, "element is disabled");
      if (GetWindowLongPtrW(c, GWL_STYLE) & ES_READONLY) return fail(E_DENIED, "element is read-only");
      DWORD_PTR res = 0;
      if (!SendMessageTimeoutW(c, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(v.c_str()), SMTO_ABORTIFHUNG, 400, &res)) return fail(E_TIMEOUT, "control did not respond");
      return {};
    }
  }
  return uia::Service::get().set_value(n, v);
}

Res<void> toggle_node(const uia::Node& n) {
  if (n.native && n.cls == "Button") {
    HWND b = win::to_hwnd(n.native);
    if (b && IsWindow(b)) return input::bm_click(b);
  }
  return uia::Service::get().toggle(n);
}

// launch 能起任何程序，等于绕开 Claude Code 自己对 Bash 的授权；这里设两道护栏（不是沙箱）：
// 不起 Deixion 自己的程序；命令行解释器与脚本宿主默认不起，需要用户在设置里打开。
std::string launch_name(const std::string& path) {
  std::string n = path;
  while (!n.empty() && (n.back() == ' ' || n.back() == '"' || n.back() == '\\' || n.back() == '/')) n.pop_back();
  const size_t cut = n.find_last_of("\\/");
  if (cut != std::string::npos) n.erase(0, cut + 1);
  while (!n.empty() && (n.front() == ' ' || n.front() == '"')) n.erase(0, 1);
  n = text::lower(n);
  if (n.find('.') == std::string::npos) n += ".exe";
  return n;
}
bool launches_own_program(const std::string& name, const std::string& path) {
  if (name.rfind("deixion", 0) == 0 && name.size() > 4 && name.compare(name.size() - 4, 4, ".exe") == 0) return true;
  if (name != "uninstall.exe") return false;
  wchar_t self[MAX_PATH]{}, full[MAX_PATH]{};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  const std::wstring wp = text::widen(path);
  if (!GetFullPathNameW(wp.c_str(), MAX_PATH, full, nullptr)) return false;
  std::wstring a = full, b = self;
  a = a.substr(0, a.find_last_of(L"\\/"));
  b = b.substr(0, b.find_last_of(L"\\/"));
  return text::lower(text::narrow(a)) == text::lower(text::narrow(b));
}
bool launches_shell_host(const std::string& name) {
  static const std::unordered_set<std::string> kHosts = {"cmd.exe",     "powershell.exe", "pwsh.exe",   "powershell_ise.exe", "wscript.exe", "cscript.exe", "mshta.exe",
                                                         "rundll32.exe", "regsvr32.exe",   "wsl.exe",    "bash.exe",           "wt.exe",      "conhost.exe"};
  if (kHosts.count(name)) return true;
  static const char* kScripts[] = {".bat", ".cmd", ".ps1", ".vbs", ".vbe", ".js", ".jse", ".wsf", ".wsh", ".hta", ".reg"};
  for (const char* ext : kScripts) {
    const size_t n = std::char_traits<char>::length(ext);
    if (name.size() >= n && name.compare(name.size() - n, n, ext) == 0) return true;
  }
  return false;
}
}  // namespace

// 窗口目标缺省时，若给了屏幕坐标就用屏幕作目标；再把屏幕目标落到该点下面的真实顶层窗口，供消息通道使用。
#define DX_TARGET(var, p)                                                                         \
  Json p2_##var = (p);                                                                            \
  auto var##_r = target_of(p2_##var, !(p2_##var["window"].as_str().empty() && p2_##var.has("screen"))); \
  if (!var##_r) return std::unexpected(var##_r.error());                                          \
  if (is_own(var##_r->pid, var##_r->app)) return fail(E_DENIED, kSelfMsg);                        \
  Target var = *var##_r

Res<Json> Engine::a_click(const Json& p) {
  const Settings st = SettingsStore::get().snapshot();
  if (auto g = gate(st); !g) return std::unexpected(g.error());
  DX_TARGET(t, p);
  auto prr = point_of(t, p, true);
  if (!prr) return std::unexpected(prr.error());
  PointRes pr = *prr;
  std::lock_guard act(act_mu_);
  HWND top = t.hwnd;
  if (t.screen) {
    top = top_at(pr.px);
    if (top && own_window(top)) return fail(E_DENIED, kSelfMsg);
    if (top) {
      t.hwnd = top;
      DWORD pid = 0;
      GetWindowThreadProcessId(top, &pid);
      t.pid = pid;
      t.app = text::lower(win::exe_name_of_pid(pid));
    }
  }
  const bool fg = fg_mode(st, p);
  const auto btn = btn_of(p);
  const int count = clampi(p["count"].as_int(1), 1, 3);
  const bool hover = p["hover"].as_bool(false);
  const bool left_single = btn == input::Button::Left && count == 1 && !hover;
  const std::string role = pr.node >= 0 ? pr.snap->nodes[static_cast<size_t>(pr.node)].role : "point";
  const std::string what = pr.node >= 0 ? pr.snap->nodes[static_cast<size_t>(pr.node)].name : std::string();
  const std::string label = label_of(hover ? "hover" : count > 1 ? "double click" : btn == input::Button::Right ? "right click" : "click", what);
  store::Inverse inv;
  std::vector<Attempt> ladder;
  auto uia_node = [&]() -> Res<void> {
    return uia_activate(pr.snap->nodes[static_cast<size_t>(pr.node)], &inv, t.hwnd);
  };
  auto uia_hit = [&]() -> Res<void> {
    if (!top) return fail(E_NOT_FOUND, "no window under the point");
    auto s = uia::Service::get().snapshot(top, {.ttl_ms = 1500});
    if (!s) return std::unexpected(s.error());
    const int n = uia::Service::hit_test(**s, pr.px, true);
    if (n <= 0) return fail(E_NOT_FOUND, "no activatable element at the point");
    return uia_activate((*s)->nodes[static_cast<size_t>(n)], &inv, top);
  };
  auto msg = [&]() -> Res<void> {
    if (!top) return fail(E_NOT_FOUND, "no window under the point");
    return input::msg_click(top, pr.px, btn, count, hover);
  };
  auto hop = [&]() -> Res<void> {
    if (!top) return fail(E_NOT_FOUND, "no window under the point");
    input::Hop h(top);
    if (!h.ok()) return fail(E_DENIED, "cannot bring the window forward briefly");
    return hover ? input::si_move(pr.px) : input::si_click(pr.px, btn, count);
  };
  if (fg) {
    ladder.push_back({"real", [&]() -> Res<void> {
                        fg_prelude(t, &pr, label, st);
                        return hover ? Res<void>{} : input::si_click(pr.px, btn, count);
                      }});
    if (left_single && pr.node >= 0) ladder.push_back({"uia", uia_node, true});
    ladder.push_back({"msg", msg, true});
  } else {
    const bool hostile = msg_hostile(top, pr.px);
    if (left_single && pr.node >= 0) ladder.push_back({"uia", uia_node});
    ladder.push_back({"msg", msg, hostile && left_single && pr.node >= 0});
    if (left_single && pr.node < 0) ladder.push_back({"uia_hit", uia_hit, true});
    if (st.allow_hop) ladder.push_back({"hop", hop, true});
  }
  if (auto v = apply_via(ladder, p); !v) return std::unexpected(v.error());
  Outcome out = run_ladder(t, "click", role, std::move(ladder), st, !hover);
  if (out.ok && !hover && pr.node >= 0) {
    if (const u64 nat = pr.snap->nodes[static_cast<size_t>(pr.node)].native) input::remember_focus(win::to_hwnd(nat));
  }
  if (out.ok && out.strategy == "uia") out.inv = inv;
  if (out.ok && out.strategy == "uia_hit") out.inv = inv;
  const u64 jid = log_action("click", p, t, out);
  Json r = outcome_json(t, &pr, out, jid);
  if (!out.ok) return fail(out.code ? out.code : E_INTERNAL, out.err.empty() ? "click failed" : out.err);
  return r;
}

Res<Json> Engine::a_type(const Json& p) {
  const Settings st = SettingsStore::get().snapshot();
  if (auto g = gate(st); !g) return std::unexpected(g.error());
  const std::string txt = p["text"].as_str();
  if (txt.empty()) return fail(E_BAD_ARG, "text is required");
  const bool replace = p["replace"].as_bool(false);
  DX_TARGET(t, p);
  PointRes pr;
  bool have_pt = false;
  if (has_point(p)) {
    auto prr = point_of(t, p, true);
    if (!prr) return std::unexpected(prr.error());
    pr = *prr;
    have_pt = true;
  }
  std::lock_guard act(act_mu_);
  HWND top = t.hwnd;
  if (t.screen) {
    if (!have_pt) return fail(E_BAD_ARG, "type needs a window");
    top = top_at(pr.px);
    if (top && own_window(top)) return fail(E_DENIED, kSelfMsg);
    if (!top) return fail(E_NOT_FOUND, "no window under the point");
    t.hwnd = top;
    DWORD pid = 0;
    GetWindowThreadProcessId(top, &pid);
    t.pid = pid;
    t.app = text::lower(win::exe_name_of_pid(pid));
  }
  const bool fg = fg_mode(st, p);
  const std::wstring wtxt = text::widen(txt);
  uia::Service& svc = uia::Service::get();
  const std::string role = have_pt && pr.node >= 0 ? pr.snap->nodes[static_cast<size_t>(pr.node)].role : "focus";

  // 把输入焦点放到目标上：有控件就 UI Automation 聚焦，只有坐标就发一次后台点击。
  // UI Automation 的 SetFocus 会把目标窗口抬成系统前台，等于抢走用户正在输入的焦点；后台通道（消息 / 直接设值）本来就不需要焦点，
  // 所以只在目标本来就是前台窗口时才设，绝不为了设焦点去激活别人的窗口。
  if (p["focus"].as_bool(true) && have_pt && !fg && GetForegroundWindow() == GetAncestor(top, GA_ROOT)) {
    if (pr.node >= 0) (void)svc.focus(pr.snap->nodes[static_cast<size_t>(pr.node)]);
    else (void)input::msg_click(top, pr.px, input::Button::Left, 1);
  }

  store::Inverse inv;
  auto find_node = [&]() -> Res<std::pair<std::shared_ptr<uia::Snapshot>, int>> {
    if (have_pt && pr.node >= 0) return std::make_pair(pr.snap, pr.node);
    auto s = svc.snapshot(top, {.ttl_ms = 200});
    if (!s) return std::unexpected(s.error());
    for (const auto& n : (*s)->nodes)
      if ((n.flags & uia::F_FOCUSED) && (n.patterns & (uia::P_VALUE | uia::P_TEXT))) return std::make_pair(*s, n.id);
    if (have_pt) {
      const int h = uia::Service::hit_test(**s, pr.px, false);
      if (h > 0) return std::make_pair(*s, h);
    }
    return fail(E_NOT_FOUND, "no focused text element found");
  };
  std::vector<Attempt> ladder;
  auto set_inverse_value = [&](const uia::Node& n, const std::string& prev) {
    inv.kind = "set_value";
    inv.data = selector_of(n, top);
    inv.data.set("prev", prev);
  };
  auto uia_set = [&]() -> Res<void> {
    auto f = find_node();
    if (!f) return std::unexpected(f.error());
    const auto& n = f->first->nodes[static_cast<size_t>(f->second)];
    if (!(n.patterns & uia::P_VALUE)) return fail(E_UNSUPPORTED, "element has no value pattern");
    auto prev = svc.get_value(n);
    if (auto r = set_text(n, replace ? wtxt : text::widen(prev ? *prev : std::string()) + wtxt); !r) return r;
    set_inverse_value(n, prev ? *prev : std::string());
    return {};
  };
  auto msg_text = [&]() -> Res<void> {
    HWND dest = nullptr;
    if (have_pt && pr.node >= 0 && pr.snap->nodes[static_cast<size_t>(pr.node)].native) dest = win::to_hwnd(pr.snap->nodes[static_cast<size_t>(pr.node)].native);
    else if (have_pt) dest = win::deepest_child_at(top, pr.px);
    else dest = input::focus_hwnd(top);
    if (!dest || !IsWindow(dest)) dest = top;
    if (replace) (void)input::msg_chord_attached(dest, *input::parse_chord("ctrl+a"));
    auto r = input::msg_text(dest, wtxt, true);
    if (r) {
      inv.kind = replace ? "none" : "backspace";
      inv.data = Json::object();
      inv.data.set("hwnd", hwnd_str(dest)).set("count", wtxt.size());
      inv.best_effort = true;
    }
    return r;
  };
  auto hop_text = [&]() -> Res<void> {
    input::Hop h(top);
    if (!h.ok()) return fail(E_DENIED, "cannot bring the window forward briefly");
    if (have_pt) (void)input::si_click(pr.px, input::Button::Left, 1);
    if (replace) (void)input::si_key(*input::parse_chord("ctrl+a"));
    return input::si_text(wtxt);
  };
  if (fg) {
    ladder.push_back({"real", [&]() -> Res<void> {
                        if (have_pt) {
                          fg_prelude(t, &pr, label_of("type", std::to_string(wtxt.size()) + " chars"), st);
                          (void)input::si_click(pr.px, input::Button::Left, 1);
                        } else {
                          input::force_foreground(top);
                        }
                        if (replace) (void)input::si_key(*input::parse_chord("ctrl+a"));
                        return input::si_text(wtxt);
                      }});
    ladder.push_back({"uia_set", uia_set});
  } else if (replace) {
    ladder.push_back({"uia_set", uia_set});
    ladder.push_back({"msg_char", msg_text});
    if (st.allow_hop) ladder.push_back({"hop", hop_text, true});
  } else {
    const bool hostile = msg_hostile(top, have_pt ? pr.px : geo::PointI{t.frame.r.x + t.frame.r.w / 2, t.frame.r.y + t.frame.r.h / 2});
    if (hostile) {
      ladder.push_back({"uia_set", uia_set});
      ladder.push_back({"msg_char", msg_text, true});
    } else {
      ladder.push_back({"msg_char", msg_text});
      ladder.push_back({"uia_set", uia_set, true});
    }
    if (st.allow_hop) ladder.push_back({"hop", hop_text, true});
  }
  if (auto v = apply_via(ladder, p); !v) return std::unexpected(v.error());
  Outcome out = run_ladder(t, replace ? "replace_text" : "type", role, std::move(ladder), st, true, have_pt && !fg);
  if (out.ok) out.inv = inv;
  svc.invalidate(top);
  const u64 jid = log_action("type", p, t, out);
  if (!out.ok) return fail(out.code ? out.code : E_INTERNAL, out.err.empty() ? "type failed" : out.err);
  Json r = outcome_json(t, have_pt ? &pr : nullptr, out, jid);
  r.set("chars", wtxt.size());
  return r;
}

Res<Json> Engine::a_set_value(const Json& p) {
  const Settings st = SettingsStore::get().snapshot();
  if (auto g = gate(st); !g) return std::unexpected(g.error());
  if (!p.has("value")) return fail(E_BAD_ARG, "value is required");
  DX_TARGET(t, p);
  auto prr = point_of(t, p, true);
  if (!prr) return std::unexpected(prr.error());
  PointRes pr = *prr;
  std::lock_guard act(act_mu_);
  uia::Service& svc = uia::Service::get();
  store::Inverse inv;
  std::vector<Attempt> ladder;
  const bool numeric = p["value"].is_num();
  ladder.push_back({"uia_set", [&]() -> Res<void> {
                      if (pr.node < 0) return fail(E_NOT_FOUND, "no UI Automation element at the point");
                      const auto& n = pr.snap->nodes[static_cast<size_t>(pr.node)];
                      if (numeric && (n.patterns & uia::P_RANGE)) {
                        auto r = svc.set_range(n, p["value"].as_num());
                        if (r) {
                          inv.kind = "none";
                        }
                        return r;
                      }
                      auto prev = svc.get_value(n);
                      const std::string v = p["value"].is_str() ? p["value"].as_str() : p["value"].dump();
                      if (auto r = set_text(n, text::widen(v)); !r) return r;
                      inv.kind = "set_value";
                      inv.data = selector_of(n, t.hwnd);
                      inv.data.set("prev", prev ? *prev : std::string());
                      return {};
                    }});
  ladder.push_back({"msg_settext", [&]() -> Res<void> {
                      if (!t.hwnd) return fail(E_UNSUPPORTED, "needs a window");
                      HWND c = win::deepest_child_at(t.hwnd, pr.px);
                      const std::wstring v = text::widen(p["value"].is_str() ? p["value"].as_str() : p["value"].dump());
                      DWORD_PTR res = 0;
                      std::wstring prev(4096, L'\0');
                      DWORD_PTR got = 0;
                      if (SendMessageTimeoutW(c, WM_GETTEXT, static_cast<WPARAM>(prev.size()), reinterpret_cast<LPARAM>(prev.data()), SMTO_ABORTIFHUNG, 400, &got)) prev.resize(static_cast<size_t>(got));
                      else prev.clear();
                      if (!SendMessageTimeoutW(c, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(v.c_str()), SMTO_ABORTIFHUNG, 400, &res)) return fail(E_TIMEOUT, "control did not respond");
                      if (pr.node >= 0) {
                        inv.kind = "set_value";
                        inv.data = selector_of(pr.snap->nodes[static_cast<size_t>(pr.node)], t.hwnd);
                        inv.data.set("prev", text::narrow(prev));
                      }
                      return {};
                    }});
  if (auto v = apply_via(ladder, p); !v) return std::unexpected(v.error());
  Outcome out = run_ladder(t, "set_value", pr.node >= 0 ? pr.snap->nodes[static_cast<size_t>(pr.node)].role : "point", std::move(ladder), st, true);
  if (out.ok) out.inv = inv;
  if (t.hwnd) svc.invalidate(t.hwnd);
  const u64 jid = log_action("set_value", p, t, out);
  if (!out.ok) return fail(out.code ? out.code : E_INTERNAL, out.err.empty() ? "set_value failed" : out.err);
  return outcome_json(t, &pr, out, jid);
}

Res<Json> Engine::a_key(const Json& p) {
  const Settings st = SettingsStore::get().snapshot();
  if (auto g = gate(st); !g) return std::unexpected(g.error());
  const std::string spec = p["keys"].as_str();
  if (spec.empty()) return fail(E_BAD_ARG, "keys is required, e.g. \"enter\", \"ctrl+s\", \"ctrl+a ctrl+c\"");
  std::vector<input::KeyChord> chords;
  {
    std::string cur;
    for (size_t i = 0; i <= spec.size(); ++i) {
      if (i == spec.size() || spec[i] == ' ') {
        if (!cur.empty()) {
          auto c = input::parse_chord(cur);
          if (!c) return std::unexpected(c.error());
          chords.push_back(*c);
          cur.clear();
        }
      } else {
        cur.push_back(spec[i]);
      }
    }
  }
  const int repeat = clampi(p["repeat"].as_int(1), 1, 200);
  DX_TARGET(t, p);
  if (t.screen) return fail(E_BAD_ARG, "keys need a window target");
  std::lock_guard act(act_mu_);
  const bool fg = fg_mode(st, p);
  const bool any_mod = std::any_of(chords.begin(), chords.end(), [](const input::KeyChord& c) { return c.has_mods(); });
  std::vector<Attempt> ladder;
  auto each = [&](auto&& fn) -> Res<void> {
    for (int r = 0; r < repeat; ++r)
      for (const auto& c : chords) {
        if (auto x = fn(c); !x) return x;
        sleep_us(3000);
      }
    return {};
  };
  if (fg) {
    ladder.push_back({"real", [&]() -> Res<void> {
                        input::force_foreground(t.hwnd);
                        return each([&](const input::KeyChord& c) { return input::si_key(c); });
                      }});
  } else {
    if (!any_mod)
      ladder.push_back({"msg_key", [&]() -> Res<void> {
                          return each([&](const input::KeyChord& c) -> Res<void> {
                            if (!c.text.empty()) return input::msg_text(t.hwnd, text::widen(c.text));
                            return input::msg_key(t.hwnd, c);
                          });
                        }});
    else
      ladder.push_back({"msg_chord", [&]() -> Res<void> {
                          return each([&](const input::KeyChord& c) -> Res<void> { return c.has_mods() ? input::msg_chord_attached(t.hwnd, c) : input::msg_key(t.hwnd, c); });
                        }});
    if (st.allow_hop)
      ladder.push_back({"hop", [&]() -> Res<void> {
                          input::Hop h(t.hwnd);
                          if (!h.ok()) return fail(E_DENIED, "cannot bring the window forward briefly");
                          return each([&](const input::KeyChord& c) { return input::si_key(c); });
                        }});
  }
  if (auto v = apply_via(ladder, p); !v) return std::unexpected(v.error());
  Outcome out = run_ladder(t, "key", any_mod ? "chord" : "key", std::move(ladder), st, true);
  uia::Service::get().invalidate(t.hwnd);
  const u64 jid = log_action("key", p, t, out);
  if (!out.ok) return fail(out.code ? out.code : E_INTERNAL, out.err.empty() ? "key failed" : out.err);
  return outcome_json(t, nullptr, out, jid);
}

Res<Json> Engine::a_scroll(const Json& p) {
  const Settings st = SettingsStore::get().snapshot();
  if (auto g = gate(st); !g) return std::unexpected(g.error());
  const int dy = clampi(p["dy"].as_int(0), -100, 100), dx = clampi(p["dx"].as_int(0), -100, 100);
  if (!dy && !dx) return fail(E_BAD_ARG, "dy (rows; positive scrolls down) or dx is required");
  DX_TARGET(t, p);
  Json pp = p;
  if (!has_point(pp)) pp.set("at", "0.5,0.5");
  auto prr = point_of(t, pp, true);
  if (!prr) return std::unexpected(prr.error());
  PointRes pr = *prr;
  std::lock_guard act(act_mu_);
  HWND top = t.hwnd ? t.hwnd : top_at(pr.px);
  if (top && own_window(top)) return fail(E_DENIED, kSelfMsg);
  if (!top) return fail(E_NOT_FOUND, "no window under the point");
  if (t.screen) {
    t.hwnd = top;
    DWORD pid = 0;
    GetWindowThreadProcessId(top, &pid);
    t.pid = pid;
    t.app = text::lower(win::exe_name_of_pid(pid));
  }
  const bool fg = fg_mode(st, p);
  std::vector<Attempt> ladder;
  auto uia_scroll = [&]() -> Res<void> {
    auto s = uia::Service::get().snapshot(top, {.ttl_ms = 1500});
    if (!s) return std::unexpected(s.error());
    int n = uia::Service::hit_test(**s, pr.px, false);
    while (n > 0 && !((*s)->nodes[static_cast<size_t>(n)].patterns & uia::P_SCROLL)) n = (*s)->nodes[static_cast<size_t>(n)].parent;
    if (n <= 0) return fail(E_NOT_FOUND, "no scrollable element at the point");
    const auto& node = (*s)->nodes[static_cast<size_t>(n)];
    for (int i = 0; i < std::abs(dy); ++i)
      if (auto r = uia::Service::get().scroll(node, 0, dy > 0 ? 1 : -1); !r) return r;
    for (int i = 0; i < std::abs(dx); ++i)
      if (auto r = uia::Service::get().scroll(node, dx > 0 ? 1 : -1, 0); !r) return r;
    return {};
  };
  if (fg) {
    ladder.push_back({"real", [&]() -> Res<void> {
                        fg_prelude(t, &pr, label_of("scroll", ""), st);
                        return input::si_scroll(pr.px, -dy, dx);
                      }});
  } else {
    const bool hostile = msg_hostile(top, pr.px);
    ladder.push_back({"msg", [&]() -> Res<void> { return input::msg_scroll(top, pr.px, -dy, dx); }, hostile});
    ladder.push_back({"uia_scroll", uia_scroll});
    if (st.allow_hop)
      ladder.push_back({"hop", [&]() -> Res<void> {
                          input::Hop h(top);
                          if (!h.ok()) return fail(E_DENIED, "cannot bring the window forward briefly");
                          return input::si_scroll(pr.px, -dy, dx);
                        }});
  }
  if (auto v = apply_via(ladder, p); !v) return std::unexpected(v.error());
  Outcome out = run_ladder(t, "scroll", "point", std::move(ladder), st, true);
  uia::Service::get().invalidate(top);
  const u64 jid = log_action("scroll", p, t, out);
  if (!out.ok) return fail(out.code ? out.code : E_INTERNAL, out.err.empty() ? "scroll failed" : out.err);
  return outcome_json(t, &pr, out, jid);
}

Res<Json> Engine::a_drag(const Json& p) {
  const Settings st = SettingsStore::get().snapshot();
  if (auto g = gate(st); !g) return std::unexpected(g.error());
  if (!p.has("from") || !p.has("to")) return fail(E_BAD_ARG, "from and to are required, each like {\"at\":\"0.2,0.4\"} or {\"element\":\"e5\"}");
  DX_TARGET(t, p);
  auto a = point_of(t, p["from"], false);
  if (!a) return std::unexpected(a.error());
  auto b = point_of(t, p["to"], false);
  if (!b) return std::unexpected(b.error());
  std::lock_guard act(act_mu_);
  HWND top = t.hwnd ? t.hwnd : top_at(a->px);
  if (top && own_window(top)) return fail(E_DENIED, kSelfMsg);
  if (!top) return fail(E_NOT_FOUND, "no window under the start point");
  if (t.screen) {
    t.hwnd = top;
    DWORD pid = 0;
    GetWindowThreadProcessId(top, &pid);
    t.pid = pid;
    t.app = text::lower(win::exe_name_of_pid(pid));
  }
  const bool fg = fg_mode(st, p);
  const auto btn = btn_of(p);
  const int steps = clampi(p["steps"].as_int(24), 2, 200);
  std::vector<Attempt> ladder;
  if (fg) {
    ladder.push_back({"real", [&]() -> Res<void> {
                        PointRes pa = *a;
                        fg_prelude(t, &pa, label_of("drag", ""), st);
                        return input::si_drag(a->px, b->px, btn, speed_ms(st) ? speed_ms(st) * 2 : 60);
                      }});
  } else {
    ladder.push_back({"msg", [&]() -> Res<void> { return input::msg_drag(top, a->px, b->px, btn, steps); }});
    if (st.allow_hop)
      ladder.push_back({"hop", [&]() -> Res<void> {
                          input::Hop h(top);
                          if (!h.ok()) return fail(E_DENIED, "cannot bring the window forward briefly");
                          return input::si_drag(a->px, b->px, btn, 80);
                        }});
  }
  if (auto v = apply_via(ladder, p); !v) return std::unexpected(v.error());
  Outcome out = run_ladder(t, "drag", "point", std::move(ladder), st, true);
  uia::Service::get().invalidate(top);
  const u64 jid = log_action("drag", p, t, out);
  if (!out.ok) return fail(out.code ? out.code : E_INTERNAL, out.err.empty() ? "drag failed" : out.err);
  Json r = outcome_json(t, &*a, out, jid);
  r.set("to", geo::fmt(b->ll)).set("to_px", px_json(b->px));
  return r;
}

Res<Json> Engine::a_window(const Json& p) {
  const Settings st = SettingsStore::get().snapshot();
  if (auto g = gate(st); !g) return std::unexpected(g.error());
  const std::string op = text::lower(p["op"].as_str());
  if (op.empty()) return fail(E_BAD_ARG, "op is required: focus, minimize, maximize, restore, close, move, resize, topmost");
  auto tr = target_of(p, true);
  if (!tr) return std::unexpected(tr.error());
  Target t = *tr;
  if (t.screen) return fail(E_BAD_ARG, "window ops need a window");
  std::lock_guard act(act_mu_);
  const bool fg = fg_mode(st, p);
  Stopwatch sw;
  Outcome out;
  out.strategy = "win32";
  RECT rc{};
  GetWindowRect(t.hwnd, &rc);
  const bool was_min = IsIconic(t.hwnd), was_max = IsZoomed(t.hwnd);
  auto set_rect_inv = [&]() {
    out.inv.kind = "window_rect";
    out.inv.data = Json::object();
    out.inv.data.set("hwnd", hwnd_str(t.hwnd)).set("x", rc.left).set("y", rc.top).set("w", rc.right - rc.left).set("h", rc.bottom - rc.top);
  };
  auto set_state_inv = [&]() {
    out.inv.kind = "window_state";
    out.inv.data = Json::object();
    out.inv.data.set("hwnd", hwnd_str(t.hwnd)).set("state", was_min ? "minimized" : was_max ? "maximized" : "normal");
  };
  bool ok = true;
  if (op == "focus") {
    if (!fg && !st.allow_hop) return fail(E_DENIED, "bringing a window to the front would interrupt the user; the user has to enable foreground mode or the brief-foreground fallback");
    ok = input::force_foreground(t.hwnd);
  } else if (op == "minimize") {
    set_state_inv();
    ShowWindowAsync(t.hwnd, SW_SHOWMINNOACTIVE);
  } else if (op == "maximize") {
    set_state_inv();
    ShowWindowAsync(t.hwnd, SW_MAXIMIZE);
  } else if (op == "restore") {
    set_state_inv();
    ShowWindowAsync(t.hwnd, fg ? SW_RESTORE : (was_min ? SW_SHOWNOACTIVATE : SW_RESTORE));
  } else if (op == "close") {
    ok = PostMessageW(t.hwnd, WM_CLOSE, 0, 0) != 0;
  } else if (op == "move" || op == "resize") {
    const Json& r = p["rect"];
    set_rect_inv();
    const int x = static_cast<int>(r["x"].as_int(rc.left)), y = static_cast<int>(r["y"].as_int(rc.top));
    const int w = static_cast<int>(r["w"].as_int(rc.right - rc.left)), h = static_cast<int>(r["h"].as_int(rc.bottom - rc.top));
    UINT fl = SWP_NOACTIVATE | SWP_NOZORDER;
    if (op == "move") fl |= SWP_NOSIZE;
    else if (!r.has("x") && !r.has("y")) fl |= SWP_NOMOVE;
    ok = SetWindowPos(t.hwnd, nullptr, x, y, w, h, fl) != 0;
  } else if (op == "topmost") {
    ok = SetWindowPos(t.hwnd, p["on"].as_bool(true) ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0;
  } else {
    return fail(E_BAD_ARG, "unknown op: " + op);
  }
  out.ok = ok;
  out.us = sw.ns() / 1000;
  if (!ok) {
    out.code = E_WIN32;
    out.err = "the window refused the operation";
  }
  uia::Service::get().invalidate(t.hwnd);
  const u64 jid = log_action("window", p, t, out);
  if (!ok) return fail(E_WIN32, out.err);
  Json r = outcome_json(t, nullptr, out, jid);
  r.set("op", op);
  if (auto wi = win::info(t.hwnd)) r.set("window", win::to_json(*wi));
  return r;
}

Res<Json> Engine::a_launch(const Json& p) {
  const Settings st = SettingsStore::get().snapshot();
  if (auto g = gate(st); !g) return std::unexpected(g.error());
  const std::string path = p["path"].as_str();
  if (path.empty()) return fail(E_BAD_ARG, "path is required (an exe, document, or URL)");
  const std::string lname = launch_name(path);
  if (launches_own_program(lname, path)) return fail(E_DENIED, "launch cannot start Deixion's own programs");
  if (!st.allow_shell_launch && path.find("://") == std::string::npos && launches_shell_host(lname))
    return fail(E_DENIED, "launching a command shell or script host is off; only the user can enable it in Deixion's settings (allow_shell_launch)");
  const bool fg = fg_mode(st, p);
  SHELLEXECUTEINFOW sei{sizeof sei};
  const std::wstring wp = text::widen(path), wa = text::widen(p["args"].as_str()), wd = text::widen(p["cwd"].as_str());
  sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
  sei.lpFile = wp.c_str();
  sei.lpParameters = wa.empty() ? nullptr : wa.c_str();
  sei.lpDirectory = wd.empty() ? nullptr : wd.c_str();
  sei.nShow = fg ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE;
  Stopwatch sw;
  if (!ShellExecuteExW(&sei)) return fail(E_WIN32, "launch failed (error " + std::to_string(GetLastError()) + ")");
  u32 pid = sei.hProcess ? GetProcessId(sei.hProcess) : 0;
  if (sei.hProcess) {
    WaitForInputIdle(sei.hProcess, static_cast<DWORD>(std::clamp<i64>(p["idle_ms"].as_int(1500), 0, 15000)));
    CloseHandle(sei.hProcess);
  }
  Json r = Json::object();
  r.set("ok", true).set("pid", pid).set("us", sw.ns() / 1000);
  const i64 wait_ms = std::clamp<i64>(p["wait_window_ms"].as_int(3000), 0, 30000);
  if (pid && wait_ms) {
    const u64 end = now_us() + static_cast<u64>(wait_ms) * 1000;
    while (now_us() < end) {
      for (const auto& w : win::list_windows({false, false, ""}))
        if (w.pid == pid) {
          r.set("window", win::to_json(w));
          goto done;
        }
      sleep_us(40000);
    }
  }
done:
  Target t;
  t.app = pid ? text::lower(win::exe_name_of_pid(pid)) : "launch";
  t.title = path;
  Outcome out;
  out.ok = true;
  out.strategy = "shell";
  out.us = sw.ns() / 1000;
  log_action("launch", p, t, out);
  return r;
}

Res<Json> Engine::a_wait(const Json& p) {
  const std::string what = text::lower(p["for"].str_or("settle"));
  const u64 timeout_us = static_cast<u64>(std::clamp<i64>(p["timeout_ms"].as_int(5000), 10, 120000)) * 1000;
  Activity::get().touch();
  Stopwatch sw;
  const u64 epoch = stop_epoch_.load();
  Json r = Json::object();
  if (what == "window") {
    const std::string spec = p["window"].as_str();
    if (spec.empty()) return fail(E_BAD_ARG, "window is required");
    const u64 t0 = now_us();
    for (;;) {
      auto h = win::resolve(spec);
      if (h) {
        r.set("ok", true).set("us", sw.ns() / 1000);
        if (auto wi = win::info(*h)) r.set("window", win::to_json(*wi));
        return r;
      }
      if (now_us() - t0 > timeout_us) return fail(E_TIMEOUT, "window did not appear: " + spec);
      if (stop_epoch_.load() != epoch) return fail(E_CANCELLED, "stopped by the user");
      sleep_us(30000);
    }
  }
  auto tr = target_of(p, true);
  if (!tr) return std::unexpected(tr.error());
  Target t = *tr;
  if (what == "settle" || what == "idle") {
    const double learned = exp_.get_num("quiet:" + t.app, 30.0);
    const u64 quiet_us = static_cast<u64>(std::clamp<double>(p["quiet_ms"].as_num(learned), 2, 2000) * 1000);
    const bool ok = Activity::get().wait_quiet(t.hwnd, t.pid, quiet_us, timeout_us);
    const double waited_ms = static_cast<double>(sw.ns()) / 1e6;
    if (ok && !p.has("quiet_ms")) exp_.set_num("quiet:" + t.app, std::clamp(0.8 * learned + 0.2 * std::max(4.0, waited_ms * 0.5), 6.0, 200.0));
    r.set("ok", ok).set("settled", ok).set("us", sw.ns() / 1000).set("quiet_ms", static_cast<double>(quiet_us) / 1000.0);
    if (!ok) return fail(E_TIMEOUT, "the window kept changing for the whole timeout");
    return r;
  }
  if (what == "element" || what == "gone") {
    uia::FindQuery fq;
    fq.text = p["find"]["text"].as_str();
    fq.role = p["find"]["role"].as_str();
    fq.aid = p["find"]["aid"].as_str();
    fq.limit = 1;
    if (fq.text.empty() && fq.role.empty() && fq.aid.empty()) return fail(E_BAD_ARG, "find needs text, role or aid");
    const u64 t0 = now_us();
    u64 gap = 15000;
    for (;;) {
      auto s = uia::Service::get().snapshot(t.hwnd, {.force = true});
      if (s) {
        auto ms = uia::Service::find(**s, fq);
        if (what == "element" && !ms.empty()) {
          r.set("ok", true).set("us", sw.ns() / 1000).set("element", uia::Service::node_json(**s, (*s)->nodes[static_cast<size_t>(ms[0].idx)]));
          return r;
        }
        if (what == "gone" && ms.empty()) {
          r.set("ok", true).set("us", sw.ns() / 1000);
          return r;
        }
      } else if (what == "gone") {
        r.set("ok", true).set("us", sw.ns() / 1000);
        return r;
      }
      if (now_us() - t0 > timeout_us) return fail(E_TIMEOUT, what == "element" ? "the element did not appear" : "the element did not disappear");
      if (stop_epoch_.load() != epoch) return fail(E_CANCELLED, "stopped by the user");
      const auto mk = Activity::get().mark(t.hwnd, t.pid);
      Activity::get().wait_change(t.hwnd, t.pid, mk, gap);
      gap = std::min<u64>(gap * 3 / 2, 120000);
    }
  }
  return fail(E_BAD_ARG, "for must be settle, element, gone or window");
}

Res<Json> Engine::a_rollback(const Json& p) {
  const Settings st = SettingsStore::get().snapshot();
  if (auto g = gate(st); !g) return std::unexpected(g.error());
  std::vector<store::Entry> plan;
  if (p.has("id")) {
    auto e = journal_.get(static_cast<u64>(p["id"].as_int()));
    if (!e) return fail(E_NOT_FOUND, "no such journal entry");
    plan.push_back(*e);
  } else {
    plan = journal_.undo_plan(static_cast<size_t>(clampi(p["count"].as_int(1), 1, 50)));
  }
  if (plan.empty()) return fail(E_NOT_FOUND, "nothing to roll back");
  std::lock_guard act(act_mu_);
  Json results = Json::array();
  int done = 0;
  for (const auto& e : plan) {
    Json row = Json::object();
    row.set("id", e.id).set("method", e.method).set("kind", e.inv.kind).set("best_effort", e.inv.best_effort);
    Res<void> r = fail(E_UNSUPPORTED, "this action cannot be undone");
    const Json& d = e.inv.data;
    if (e.undone) {
      r = fail(E_BAD_ARG, "already rolled back");
    } else if (e.inv.kind == "set_value" || e.inv.kind == "toggle") {
      auto sel = resolve_selector(d, 100);
      if (!sel) {
        r = std::unexpected(sel.error());
      } else {
        const auto& n = sel->first->nodes[static_cast<size_t>(sel->second)];
        auto win_h = win::resolve(d["hwnd"].as_str().empty() ? std::string("active") : "hwnd:" + d["hwnd"].as_str());
        input::FocusShield shield(win_h ? *win_h : nullptr, !st.foreground());
        r = e.inv.kind == "toggle" ? toggle_node(n) : set_text(n, text::widen(d["prev"].as_str()));
      }
    } else if (e.inv.kind == "window_rect") {
      auto h = win::resolve("hwnd:" + d["hwnd"].as_str());
      if (!h) r = std::unexpected(h.error());
      else r = SetWindowPos(*h, nullptr, static_cast<int>(d["x"].as_int()), static_cast<int>(d["y"].as_int()), static_cast<int>(d["w"].as_int()), static_cast<int>(d["h"].as_int()), SWP_NOACTIVATE | SWP_NOZORDER) ? Res<void>{} : fail(E_WIN32, "cannot restore the window rectangle");
    } else if (e.inv.kind == "window_state") {
      auto h = win::resolve("hwnd:" + d["hwnd"].as_str());
      if (!h) {
        r = std::unexpected(h.error());
      } else {
        const std::string s = d["state"].as_str();
        ShowWindowAsync(*h, s == "minimized" ? SW_SHOWMINNOACTIVE : s == "maximized" ? SW_MAXIMIZE : SW_SHOWNOACTIVATE);
        r = {};
      }
    } else if (e.inv.kind == "backspace") {
      auto h = win::resolve("hwnd:" + d["hwnd"].as_str());
      if (!h) {
        r = std::unexpected(h.error());
      } else {
        r = {};
        const auto bs = *input::parse_chord("backspace");
        for (i64 i = 0; i < d["count"].as_int() && r; ++i) r = input::msg_key(*h, bs);
      }
    }
    row.set("ok", static_cast<bool>(r));
    if (!r) row.set("error", r.error().msg);
    else {
      journal_.mark_undone(e.id);
      ++done;
    }
    results.push(std::move(row));
  }
  uia::Service::get().invalidate_all();
  u64 oldest = ~0ull;
  for (const auto& e : plan) oldest = std::min(oldest, e.id);
  int irreversible = 0;
  for (const auto& e : journal_.list(500, false))
    if (e.id > oldest && e.ok && !e.undone && e.method != "rollback" && (e.inv.kind == "none" || e.inv.kind.empty())) ++irreversible;
  Json out = Json::object();
  out.set("ok", done > 0).set("rolled_back", done).set("irreversible_actions_since", irreversible).set("results", std::move(results));
  if (done == 0) {
    const auto& rows = out["results"].arr();
    return fail(E_UNSUPPORTED, "nothing could be rolled back: " + (rows.empty() ? std::string() : rows[0]["error"].as_str()));
  }
  return out;
}

// batch 只能串起 MCP 本来就开放的动作与查询。设置、清日志、重置经验、急停是用户专属的控制，不许经由它调用，
// 否则拿到 MCP 的模型可以解除用户的暂停、打开「短暂切前台」。
static const std::unordered_set<std::string> kBatchAllowed = {"ping",  "status", "windows", "capture", "elements", "find",   "locate", "geo",     "read",   "click",
                                                              "type",  "set_value", "key",  "scroll",  "drag",     "window", "launch", "wait",    "rollback"};

Res<Json> Engine::a_batch(const Json& p) {
  const Json& steps = p["steps"];
  if (!steps.is_arr() || steps.size() == 0) return fail(E_BAD_ARG, "steps must be a non-empty array like [{\"do\":\"click\",...}]");
  if (steps.size() > 200) return fail(E_BAD_ARG, "at most 200 steps per batch");
  const bool stop_on_error = p["stop_on_error"].as_bool(true);
  const u64 epoch = stop_epoch_.load();
  Stopwatch sw;
  Json results = Json::array();
  int failed = 0;
  for (size_t i = 0; i < steps.size(); ++i) {
    const Json& st = steps[i];
    const std::string m = st["do"].as_str();
    Json row = Json::object();
    row.set("i", i).set("do", m);
    if (m.empty() || !kBatchAllowed.contains(m)) {
      row.set("ok", false).set("error", m.empty() ? "each step needs a valid do" : "'" + m + "' is not available inside batch").set("code", err_name(E_DENIED));
      results.push(std::move(row));
      ++failed;
      if (stop_on_error) break;
      continue;
    }
    if (stop_epoch_.load() != epoch) {
      row.set("ok", false).set("error", "stopped by the user");
      results.push(std::move(row));
      ++failed;
      break;
    }
    Json params = Json::object();
    for (const auto& kv : p["defaults"].obj()) params.set(kv.first, kv.second);
    for (const auto& kv : st.obj())
      if (kv.first != "do") params.set(kv.first, kv.second);
    Stopwatch s1;
    auto r = call(m, params);
    row.set("us", s1.ns() / 1000);
    if (r) {
      row.set("ok", true).set("result", *r);
    } else {
      row.set("ok", false).set("error", r.error().msg).set("code", err_name(r.error().code));
      ++failed;
    }
    results.push(std::move(row));
    if (!r && stop_on_error) break;
  }
  Json out = Json::object();
  out.set("ok", failed == 0).set("steps", steps.size()).set("ran", results.size()).set("failed", failed).set("us", sw.ns() / 1000).set("results", std::move(results));
  return out;
}

}  // namespace dx::eng
