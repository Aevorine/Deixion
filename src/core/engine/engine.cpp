#include "core/engine/engine.hpp"

#include <psapi.h>

#include <algorithm>
#include <cmath>

#include "core/base/clock.hpp"
#include "core/base/cpu.hpp"
#include "core/base/log.hpp"
#include "core/base/paths.hpp"
#include "core/base/text.hpp"
#include "core/engine/activity.hpp"
#include "core/engine/engine_util.hpp"
#include "core/engine/overlay.hpp"
#include "core/input/input.hpp"

namespace dx::eng {

Engine& Engine::get() {
  static Engine e;
  return e;
}

Res<void> Engine::start() {
  if (started_) return {};
  paths::ensure_dirs();
  init_timer_resolution();
  Log::get().start(paths::logs_dir());
  SettingsStore::get().load(paths::data_dir() / L"settings.json");
  const Settings st = SettingsStore::get().snapshot();
  Log::get().set_level(st.log_level == "debug" ? Lv::Debug : st.log_level == "warn" ? Lv::Warn : st.log_level == "error" ? Lv::Error : Lv::Info);
  if (auto r = exp_.open(paths::store_dir() / L"experience.dxl"); !r) LOGW("engine", "experience store: {}", r.error().msg);
  if (auto r = journal_.open(paths::store_dir() / L"journal.dxl"); !r) LOGW("engine", "journal: {}", r.error().msg);
  started_ms_ = unix_ms();
  started_ = true;
  SettingsStore::get().subscribe([](const Settings& s) {
    Log::get().set_level(s.log_level == "debug" ? Lv::Debug : s.log_level == "warn" ? Lv::Warn : s.log_level == "error" ? Lv::Error : Lv::Info);
    if (s.foreground() && s.overlay) Overlay::get().start();
    else Overlay::get().stop();
  });
  if (st.foreground() && st.overlay) Overlay::get().start();
  LOGI("engine", "Deixion {} started | cpu: {} | bmi2={} avx2={} sse4.2={} | data: {} | store recovered {}B/{}B", DX_VERSION, cpu().brand, cpu().bmi2, cpu().avx2,
       cpu().sse42, text::narrow(paths::data_dir().wstring()), exp_.recovered_bytes(), journal_.recovered_bytes());
  return {};
}

void Engine::shutdown() {
  if (!started_) return;
  started_ = false;
  Overlay::get().stop();
  Activity::get().stop();
  exp_.close();
  journal_.close();
  LOGI("engine", "stopped");
  Log::get().stop();
}

u64 Engine::subscribe_actions(std::function<void(const Json&)> fn) {
  std::lock_guard lk(sub_mu_);
  const u64 t = next_sub_++;
  sub_actions_.emplace_back(t, std::move(fn));
  return t;
}

void Engine::unsubscribe_actions(u64 token) {
  std::lock_guard lk(sub_mu_);
  std::erase_if(sub_actions_, [&](auto& p) { return p.first == token; });
}

void Engine::record_perf(const std::string& name, u64 ns) {
  Hist* h = nullptr;
  {
    std::lock_guard lk(hist_mu_);
    auto& p = hist_[name];
    if (!p) p = std::make_unique<Hist>();
    h = p.get();
  }
  h->record(ns);
}

// 取快照：缓存未过期且（列表类查询时）窗口没有新的界面事件才复用，否则重建并记下事件计数。
Res<std::shared_ptr<uia::Snapshot>> Engine::snap_for(const Target& t, u32 ttl_ms, bool listing) {
  uia::Service& svc = uia::Service::get();
  Activity::get().touch();
  const u64 now_act = Activity::get().mark(t.hwnd, t.pid).count;
  if (auto c = svc.cached(t.hwnd, ttl_ms); c && (!listing || c->act == now_act)) return c;
  auto s = svc.snapshot(t.hwnd, {.ttl_ms = 0, .force = true});
  if (!s) return std::unexpected(s.error());
  (*s)->act = now_act;
  return *s;
}

Res<void> Engine::gate(const Settings& st) const {
  if (st.paused) {
    const std::string hk = st.hotkeys["pause"].as_str();
    return fail(E_BUSY, "Deixion is paused by the user. Ask them to resume it (" + (hk.empty() ? std::string("the pause hotkey") : hk) + " or the tray menu).");
  }
  return {};
}

// XAML/WinUI/UWP 的内容窗口不处理 Win32 鼠标键盘消息，向它们发消息会“成功”但没有效果；
// 先验知识比让经验库去试错更省：这类窗口里消息通道降为兜底，UI Automation 优先。
bool Engine::msg_hostile(HWND top, geo::PointI px) {
  auto bad = [](HWND h) {
    wchar_t c[96] = {};
    GetClassNameW(h, c, 96);
    const std::wstring_view v(c);
    return v.find(L"ApplicationFrameWindow") != v.npos || v.find(L"InputSite") != v.npos || v.find(L"DesktopChildSiteBridge") != v.npos ||
           v.find(L"DesktopWindowContentBridge") != v.npos || v.find(L"CoreWindow") != v.npos;
  };
  if (!top) return false;
  if (bad(top)) return true;
  return bad(win::deepest_child_at(top, px));
}

// via 参数：只用指定通道（诊断、基准测试、或模型确认某通道对这个应用无效后换一条）。
Res<void> Engine::apply_via(std::vector<Attempt>& ladder, const Json& p) {
  const std::string via = p["via"].as_str();
  if (via.empty()) return {};
  std::string have;
  for (const auto& a : ladder) have += (have.empty() ? "" : ", ") + a.name;
  std::erase_if(ladder, [&](const Attempt& a) { return a.name != via; });
  if (ladder.empty()) return fail(E_UNSUPPORTED, "strategy '" + via + "' is not available for this request under the current mode (available: " + have + ")");
  ladder[0].tail = false;
  return {};
}

bool Engine::fg_mode(const Settings& st, const Json& p) const { return st.foreground() && p["mode"].as_str() != "background"; }

int Engine::speed_ms(const Settings& st) const { return st.speed == "instant" ? 0 : st.speed == "smooth" ? 380 : 120; }

Res<Engine::Target> Engine::target_of(const Json& p, bool required, bool allow_minimized) {
  Target t;
  std::string spec = p["window"].as_str();
  if (spec.empty() && p["hwnd"].is_str()) spec = "hwnd:" + p["hwnd"].as_str();
  if (spec.empty()) {
    if (required) return fail(E_BAD_ARG, "window is required: a title fragment, exe:name.exe, hwnd:0x…, or active. Call windows to list candidates.");
    spec = "screen";
  }
  const std::string l = text::lower(spec);
  if (l == "screen" || l == "desktop" || l == "all") {
    t.screen = true;
    t.frame = win::screen_frame();
    t.app = "screen";
    t.title = "screen";
    return t;
  }
  auto r = win::resolve(spec);
  if (!r) return std::unexpected(r.error());
  t.hwnd = *r;
  DWORD pid = 0;
  GetWindowThreadProcessId(t.hwnd, &pid);
  t.pid = pid;
  t.app = text::lower(win::exe_name_of_pid(pid));
  wchar_t buf[256];
  const int n = GetWindowTextW(t.hwnd, buf, 256);
  t.title = n > 0 ? text::narrow(std::wstring_view(buf, static_cast<size_t>(n))) : std::string();
  t.frame = win::client_frame(t.hwnd);
  if (t.frame.r.empty()) {
    if (IsIconic(t.hwnd) && !allow_minimized) return fail(E_UNSUPPORTED, "window is minimized; restore it first (window op=restore)");
    t.frame = win::window_frame(t.hwnd);
  }
  return t;
}

Res<Engine::PointRes> Engine::point_of(const Target& t, const Json& p, bool allow_hit) {
  PointRes r;
  const geo::LatLon none{-1, -1};
  auto from_px = [&](geo::PointI px, const char* how) {
    r.px = px;
    r.ll = geo::from_px(t.frame, px);
    r.how = how;
  };
  uia::Service& svc = uia::Service::get();
  if (p.has("element") || p.has("find")) {
    if (t.screen) return fail(E_BAD_ARG, "element/find need a window target");
    if (p.has("element")) {
      const std::string id = p["element"].as_str();
      if (id.size() < 2 || id[0] != 'e') return fail(E_BAD_ARG, "element id looks like e12 (from the elements call)");
      const int idx = std::atoi(id.c_str() + 1);
      auto snap = svc.cached(t.hwnd, 60000);
      if (!snap) {
        auto s = snap_for(t, 60000, false);
        if (!s) return std::unexpected(s.error());
        snap = *s;
      }
      if (idx <= 0 || idx >= static_cast<int>(snap->nodes.size())) return fail(E_STALE, "element " + id + " is not in the latest snapshot; call elements again");
      if (!uia::Service::still_valid(snap->nodes[static_cast<size_t>(idx)])) return fail(E_STALE, "element " + id + " changed or disappeared; call elements again");
      r.snap = snap;
      r.node = idx;
      from_px(snap->nodes[static_cast<size_t>(idx)].r.center(), "element");
      return r;
    }
    const Json& q = p["find"];
    uia::FindQuery fq;
    fq.text = q["text"].as_str();
    fq.role = q["role"].as_str();
    fq.aid = q["aid"].as_str();
    fq.enabled_only = q["enabled"].as_bool(false);
    fq.interactive = q["interactive"].as_bool(false);
    fq.limit = 6;
    if (fq.text.empty() && fq.role.empty() && fq.aid.empty()) return fail(E_BAD_ARG, "find needs text, role or aid");
    auto s = snap_for(t, static_cast<u32>(q["max_age_ms"].as_int(5000)), false);
    if (!s) return std::unexpected(s.error());
    auto ms = uia::Service::find(**s, fq);
    if (!ms.empty() && !uia::Service::still_valid((*s)->nodes[static_cast<size_t>(ms[0].idx)])) ms.clear();
    if (ms.empty()) {
      auto s2 = svc.snapshot(t.hwnd, {.ttl_ms = 0, .force = true});
      if (s2) {
        (*s2)->act = Activity::get().mark(t.hwnd, t.pid).count;
        s = s2;
        ms = uia::Service::find(**s, fq);
      }
    }
    if (ms.empty()) return fail(E_NOT_FOUND, "no element matches " + q.dump() + " in this window. Use elements (query=…) to see what is there.");
    r.snap = *s;
    r.node = ms[0].idx;
    from_px(r.snap->nodes[static_cast<size_t>(r.node)].r.center(), "find");
    if (ms.size() > 1) r.how = "find(" + std::to_string(ms.size()) + " candidates, best " + std::to_string(static_cast<int>(ms[0].score * 100)) + "%)";
    return r;
  }
  geo::LatLon ll = none;
  if (p.has("at")) {
    if (!parse_ll(p["at"], ll)) return fail(E_BAD_ARG, "at must be \"lam,phi\" or {lam,phi}, each in [0,1]");
    if (ll.lam < 0 || ll.lam > 1 || ll.phi < 0 || ll.phi > 1) return fail(E_BAD_ARG, "at is outside [0,1]");
    from_px(geo::to_px(t.frame, ll), "at");
  } else if (p.has("code")) {
    const auto c = geo::decode(p["code"].as_str());
    if (!c.valid) return fail(E_BAD_ARG, "bad Meridian code: " + p["code"].as_str());
    from_px(geo::to_px(t.frame, c.center), "code");
  } else if (p.has("px")) {
    geo::PointI v;
    if (!parse_xy(p["px"], v)) return fail(E_BAD_ARG, "px must be {x,y} in client pixels");
    from_px({t.frame.r.x + v.x, t.frame.r.y + v.y}, "px");
  } else if (p.has("screen")) {
    geo::PointI v;
    if (!parse_xy(p["screen"], v)) return fail(E_BAD_ARG, "screen must be {x,y} in screen pixels");
    from_px(v, "screen");
  } else {
    return fail(E_BAD_ARG, "no target point: give element, find, at (λ,φ), code, px or screen");
  }
  if (!t.frame.r.contains(r.px.x, r.px.y)) return fail(E_BAD_ARG, "the point is outside the target window");
  if (allow_hit && !t.screen) {
    if (auto snap = svc.cached(t.hwnd, 1500)) {
      const int n = uia::Service::hit_test(*snap, r.px, false);
      if (n > 0) {
        r.snap = snap;
        r.node = n;
      }
    }
  }
  return r;
}

Json Engine::selector_of(const uia::Node& n, HWND h) const {
  Json j = Json::object();
  j.set("hwnd", hwnd_str(h)).set("role", n.role).set("name", n.name).set("aid", n.aid);
  return j;
}

Res<std::pair<std::shared_ptr<uia::Snapshot>, int>> Engine::resolve_selector(const Json& sel, u32 ttl_ms) {
  auto h = win::resolve(sel["hwnd"].as_str().empty() ? std::string("active") : "hwnd:" + sel["hwnd"].as_str());
  if (!h) return std::unexpected(h.error());
  auto s = uia::Service::get().snapshot(*h, {.ttl_ms = ttl_ms});
  if (!s) return std::unexpected(s.error());
  uia::FindQuery q;
  q.aid = sel["aid"].as_str();
  q.role = sel["role"].as_str();
  if (q.aid.empty()) q.text = sel["name"].as_str();
  q.limit = 1;
  auto ms = uia::Service::find(**s, q);
  if (ms.empty()) return fail(E_NOT_FOUND, "the element is no longer there");
  return std::make_pair(*s, ms[0].idx);
}

Res<void> Engine::uia_activate(const uia::Node& n, store::Inverse* inv, HWND h) {
  uia::Service& s = uia::Service::get();
  if (!(n.flags & uia::F_ENABLED)) return fail(E_DENIED, "element is disabled");
  const bool toggle_first = (n.patterns & uia::P_TOGGLE) && (n.ctype == 50002 || n.ctype == 50013 || !(n.patterns & uia::P_INVOKE));
  // 经典 Win32 按钮：UIA 的 Invoke / Toggle 在目标进程里会自己抢前台，前台锁拦不住；
  // 直接发 BM_CLICK 与系统自带的 UIA 代理做的事等价，而且前台锁拦得住。
  if (n.native && n.cls == "Button" && (n.patterns & (uia::P_INVOKE | uia::P_TOGGLE))) {
    HWND b = reinterpret_cast<HWND>(static_cast<uintptr_t>(n.native));
    if (IsWindow(b)) {
      auto r = input::bm_click(b);
      if (r && toggle_first && inv) {
        inv->kind = "toggle";
        inv->data = selector_of(n, h);
      }
      return r;
    }
  }
  if (!toggle_first && (n.patterns & uia::P_INVOKE)) return s.invoke(n);
  if (n.patterns & uia::P_TOGGLE) {
    auto r = s.toggle(n);
    if (r && inv) {
      inv->kind = "toggle";
      inv->data = selector_of(n, h);
    }
    return r;
  }
  if (n.patterns & uia::P_SELECT) return s.select(n);
  if (n.patterns & uia::P_LEGACY) return s.default_action(n);
  if (n.patterns & uia::P_EXPAND) return s.expand(n, true);
  return fail(E_UNSUPPORTED, "element has no activatable UI Automation pattern");
}

void Engine::fg_prelude(const Target& t, const PointRes* pr, const std::string& label, const Settings& st) {
  if (!t.screen) input::force_foreground(t.hwnd);
  if (!pr) return;
  if (st.overlay) {
    if (pr->node >= 0 && pr->snap) Overlay::get().highlight(pr->snap->nodes[static_cast<size_t>(pr->node)].r, label);
    else Overlay::get().highlight({pr->px.x - 14, pr->px.y - 14, 28, 28}, label);
  }
  const POINT c = input::cursor_pos();
  (void)input::si_move_smooth({c.x, c.y}, pr->px, speed_ms(st));
  if (st.overlay) Overlay::get().ripple(pr->px, label);
}

Engine::Outcome Engine::run_ladder(const Target& t, const std::string& action, const std::string& role, std::vector<Attempt> ladder, const Settings& st,
                                   bool verify_on, bool prelude_done) {
  Outcome out;
  if (ladder.empty()) {
    out.code = E_UNSUPPORTED;
    out.err = "no strategy is allowed here under the current mode; in background mode some inputs need the user to enable the brief-foreground fallback";
    return out;
  }
  Activity::get().touch();
  std::vector<std::string> names, tails;
  for (const auto& a : ladder) (a.tail ? tails : names).push_back(a.name);
  auto order = exp_.rank(t.app, role, action, names);
  order.insert(order.end(), tails.begin(), tails.end());
  if (st.verify == "auto" && t.pid && prelude_done) Activity::get().wait_quiet(t.hwnd, t.pid, 2500, 20000);
  const bool verify = verify_on && st.verify == "auto" && t.pid != 0;
  std::string tried;
  int last_code = E_UNSUPPORTED;
  for (const auto& name : order) {
    auto it = std::find_if(ladder.begin(), ladder.end(), [&](const Attempt& a) { return a.name == name; });
    if (it == ladder.end()) continue;
    const Activity::Mark mark = verify ? Activity::get().mark(t.hwnd, t.pid) : Activity::Mark{};
    Stopwatch sw;
    Res<void> r;
    bool disturbed = false;
    {
      // 后台模式下，除了明确允许切前台的 hop，其余通道都不能把目标窗口顶到用户当前窗口之上。
      input::FocusShield shield(t.hwnd, !st.foreground() && it->name != "hop");
      r = it->run();
      disturbed = shield.disturbed();
    }
    const u64 ns = sw.ns();
    const double ms = static_cast<double>(ns) / 1e6;
    const store::ArmKey key{t.app, role, action, name};
    if (!r) {
      exp_.update(key, false, 0.0, ms);
      last_code = r.error().code;
      tried += (tried.empty() ? "" : "; ") + name + ": " + r.error().msg;
      continue;
    }
    bool confirmed = false;
    u64 react_us = 0;
    if (verify) {
      const double react = exp_.get_num("react:" + t.app, 8.0);
      const u64 budget = static_cast<u64>(std::clamp(react * 1.6, 2.0, 30.0) * 1000.0);
      react_us = Activity::get().wait_change(t.hwnd, t.pid, mark, budget);
      confirmed = react_us > 0;
      exp_.set_num("react:" + t.app, confirmed ? 0.7 * react + 0.3 * (static_cast<double>(react_us) / 1000.0) : std::max(1.5, react * 0.85));
    }
    double reward = !verify ? 0.9 / (1.0 + ms / 6.0) : confirmed ? std::max(0.3, 1.0 / (1.0 + ms / 6.0)) : 0.45;
    if (disturbed) reward *= 0.4;  // 通道虽然生效，却动了用户的前台窗口：让经验库往不打扰的通道倾斜
    exp_.update(key, true, reward, ms);
    out.ok = true;
    out.confirmed = confirmed;
    out.strategy = name;
    out.us = ns / 1000;
    out.extra = Json::object();
    if (verify) out.extra.set("reaction_us", react_us);
    if (disturbed) out.extra.set("focus", "target briefly took the foreground and was put back");
    if (!tried.empty()) out.extra.set("fell_back_from", tried);
    return out;
  }
  out.code = last_code;
  out.err = tried;
  return out;
}

Json Engine::outcome_json(const Target& t, const PointRes* pr, const Outcome& o, u64 jid) {
  Json j = Json::object();
  j.set("ok", o.ok);
  if (o.ok) {
    j.set("strategy", o.strategy).set("confirmed", o.confirmed).set("us", o.us);
    if (o.extra.is_obj())
      for (const auto& kv : o.extra.obj()) j.set(kv.first, kv.second);
  } else {
    Json e = Json::object();
    e.set("code", err_name(o.code)).set("message", o.err);
    j.set("error", std::move(e));
  }
  if (pr) {
    j.set("at", geo::fmt(pr->ll)).set("px", px_json(pr->px)).set("code", geo::code_of(pr->ll, geo::level_for(t.frame))).set("via", pr->how);
    if (pr->node >= 0 && pr->snap) {
      const auto& n = pr->snap->nodes[static_cast<size_t>(pr->node)];
      Json e = Json::object();
      e.set("id", "e" + std::to_string(n.id)).set("role", n.role).set("name", n.name);
      j.set("element", std::move(e));
    }
  }
  Json tg = Json::object();
  tg.set("app", t.app).set("title", t.title);
  if (t.hwnd) tg.set("hwnd", hwnd_str(t.hwnd));
  j.set("target", std::move(tg));
  if (jid) j.set("journal", jid);
  return j;
}

u64 Engine::log_action(const std::string& method, const Json& params, const Target& t, const Outcome& o) {
  store::Entry e;
  e.method = method;
  Json sp = Json::object();
  for (const auto& kv : params.obj()) {
    if ((kv.first == "text" || kv.first == "value") && kv.second.is_str()) sp.set(kv.first, "<" + std::to_string(text::widen(kv.second.as_str()).size()) + " chars>");
    else sp.set(kv.first, kv.second);
  }
  e.params = std::move(sp);
  e.app = t.app;
  e.hwnd = reinterpret_cast<uintptr_t>(t.hwnd);
  e.title = t.title;
  e.strategy = o.strategy;
  e.ok = o.ok;
  e.confirmed = o.confirmed;
  e.us = o.us;
  e.err = o.err;
  e.inv = o.inv;
  if (e.inv.kind.empty()) e.inv.kind = "none";
  Json ev = Json::object();
  ev.set("method", e.method).set("app", e.app).set("title", e.title).set("strategy", e.strategy).set("ok", e.ok).set("confirmed", e.confirmed).set("us", e.us).set("err", e.err);
  ev.set("undoable", e.inv.kind != "none").set("ts", unix_ms());
  // 事件里带上落点经纬度，界面据此画落点雷达；元素 / 查找类的落点要等解析后才知道，这里不重复解析。
  geo::LatLon ll{-1, -1};
  if (params.has("at")) {
    if (!parse_ll(params["at"], ll)) ll = {-1, -1};
  } else if (params.has("code")) {
    const auto c = geo::decode(params["code"].as_str());
    if (c.valid) ll = c.center;
  } else if (params.has("px") && !t.screen) {
    geo::PointI v;
    if (parse_xy(params["px"], v)) ll = geo::from_px(t.frame, {t.frame.r.x + v.x, t.frame.r.y + v.y});
  }
  if (ll.lam >= 0 && ll.phi >= 0) ev.set("at", geo::fmt(ll));
  const u64 id = journal_.add(std::move(e));
  ev.set("id", id);
  std::vector<std::function<void(const Json&)>> subs;
  {
    std::lock_guard lk(sub_mu_);
    for (auto& s : sub_actions_) subs.push_back(s.second);
  }
  for (auto& f : subs) f(ev);
  return id;
}

}  // namespace dx::eng
