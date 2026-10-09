#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cmath>

#include "core/base/clock.hpp"
#include "core/base/cpu.hpp"
#include "core/base/log.hpp"
#include "core/base/paths.hpp"
#include "core/base/text.hpp"
#include "core/engine/activity.hpp"
#include "core/engine/engine.hpp"
#include "core/engine/engine_util.hpp"

namespace dx::eng {
namespace {
u64 ft_to_100ns(const FILETIME& f) { return (static_cast<u64>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

bool is_interactive(const uia::Node& n) { return (n.patterns & (uia::P_INVOKE | uia::P_VALUE | uia::P_TOGGLE | uia::P_SELECT | uia::P_EXPAND | uia::P_RANGE)) != 0; }

std::string line_of(const uia::Snapshot& s, const uia::Node& n) {
  std::string l = "e" + std::to_string(n.id) + " " + n.role;
  if (!n.name.empty()) l += " \"" + (n.name.size() > 60 ? n.name.substr(0, 57) + "..." : n.name) + "\"";
  const geo::LatLon c = geo::from_px(s.frame, n.r.center());
  l += " @" + geo::fmt(c);
  if (n.patterns) {
    l += " [";
    const Json can = uia::Service::can_list(n.patterns);
    for (size_t i = 0; i < can.size(); ++i) l += (i ? "," : "") + can[i].as_str();
    l += "]";
  }
  if (!(n.flags & uia::F_ENABLED)) l += " disabled";
  if (n.flags & uia::F_FOCUSED) l += " focused";
  if (!n.aid.empty()) l += " aid=" + n.aid;
  return l;
}
}  // namespace

Res<Json> Engine::q_ping(const Json&) {
  Json j = Json::object();
  j.set("ok", true).set("version", DX_VERSION).set("t", unix_ms());
  return j;
}

Res<Json> Engine::q_windows(const Json& p) {
  win::ListOpts o;
  o.include_hidden = p["hidden"].as_bool(false);
  o.include_untitled = p["untitled"].as_bool(false);
  o.filter = p["filter"].as_str();
  const auto ws = win::list_windows(o);
  Json arr = Json::array();
  std::string txt;
  for (const auto& w : ws) {
    arr.push(win::to_json(w));
    char hb[24];
    std::snprintf(hb, sizeof hb, "0x%llX", static_cast<unsigned long long>(w.hwnd));
    txt += std::string(hb) + " " + w.exe + " \"" + (w.title.size() > 70 ? w.title.substr(0, 67) + "..." : w.title) + "\" " + std::to_string(w.client.w) + "x" + std::to_string(w.client.h) +
           (w.foreground ? " [front]" : "") + (w.minimized ? " [min]" : "") + "\n";
  }
  Json j = Json::object();
  j.set("ok", true).set("count", ws.size()).set("windows", std::move(arr)).set("text", txt);
  const geo::Frame sf = win::screen_frame();
  j.set("screen", rect_json(sf.r)).set("dpi", sf.dpi);
  return j;
}

Res<Json> Engine::q_capture(const Json& p) {
  const Settings st = SettingsStore::get().snapshot();
  Stopwatch total;
  auto tr = target_of(p, false);
  if (!tr) return std::unexpected(tr.error());
  Target t = *tr;
  Stopwatch sw;
  Res<cap::Shot> shot = t.screen ? cap::shoot_screen(t.frame.r) : cap::shoot_window(t.hwnd);
  if (!shot) return std::unexpected(shot.error());
  const u64 cap_us = sw.ns() / 1000;
  const geo::Frame frame = shot->frame;
  cap::Image* full = shot->img.get();

  // 区域：{"region":{"a":"0.2,0.3","b":"0.6,0.7"}} 或 {"code":"K7Q"}
  geo::Region reg{{0, 0}, {1, 1}};
  bool has_region = false;
  if (p["region"].is_obj()) {
    geo::LatLon a, b;
    if (!parse_ll(p["region"]["a"], a) || !parse_ll(p["region"]["b"], b)) return fail(E_BAD_ARG, "region needs a and b as \"lam,phi\"");
    reg = {geo::clamp01({std::min(a.lam, b.lam), std::min(a.phi, b.phi)}), geo::clamp01({std::max(a.lam, b.lam), std::max(a.phi, b.phi)})};
    has_region = true;
  } else if (p.has("code")) {
    const auto c = geo::decode(p["code"].as_str());
    if (!c.valid) return fail(E_BAD_ARG, "bad Meridian code: " + p["code"].as_str());
    reg = c.cell;
    has_region = true;
  }
  geo::RectI crop_px{0, 0, full->w, full->h};
  std::unique_ptr<cap::Image> cropped;
  if (has_region) {
    const geo::Frame sub = geo::sub_frame(frame, reg);
    crop_px = {sub.r.x - frame.r.x, sub.r.y - frame.r.y, sub.r.w, sub.r.h};
    auto c = cap::crop(*full, crop_px);
    if (!c) return std::unexpected(c.error());
    cropped = std::move(*c);
    full = cropped.get();
  }

  // 变化检测：与同一窗口上一次截图的分块指纹比，给出变化占比和变化区域（用经纬度表示）。
  Json changed;
  if (!has_region) {
    cap::TileMap tm = cap::tiles(*full, 32);
    std::lock_guard lk(shot_mu_);
    const u64 key = reinterpret_cast<uintptr_t>(t.hwnd);
    auto it = last_tiles_.find(key);
    if (it != last_tiles_.end() && it->second.cols == tm.cols && it->second.rows == tm.rows) {
      const cap::Diff d = cap::diff(it->second, tm, full->w, full->h);
      changed = Json::object();
      changed.set("fraction", std::round(d.fraction * 10000) / 10000);
      if (d.changed_tiles) {
        const geo::Frame f2 = frame;
        const geo::LatLon a = geo::from_px(f2, {f2.r.x + d.bbox.x, f2.r.y + d.bbox.y});
        const geo::LatLon b = geo::from_px(f2, {f2.r.x + d.bbox.right() - 1, f2.r.y + d.bbox.bottom() - 1});
        Json box = Json::array();
        for (double v : {a.lam, a.phi, b.lam, b.phi}) box.push(std::round(v * 1000) / 1000);
        changed.set("box", std::move(box));
      }
    }
    last_tiles_[key] = std::move(tm);
    if (last_tiles_.size() > 32) last_tiles_.erase(last_tiles_.begin());
  }

  const int max_dim = static_cast<int>(p["max_dim"].as_int(st.max_image_dim));
  double factor = 1.0;
  std::unique_ptr<cap::Image> small = cap::scaled(*full, max_dim, &factor);
  cap::Image* img = small ? small.get() : full;

  if (p["elements"].as_bool(false) && !t.screen) {
    auto s = uia::Service::get().snapshot(t.hwnd, {.ttl_ms = 500});
    if (s) {
      int drawn = 0;
      for (const auto& n : (*s)->nodes) {
        if (n.id == 0 || !is_interactive(n) || n.r.w < 4 || n.r.h < 4) continue;
        const geo::RectI rel{static_cast<i32>(std::lround((n.r.x - frame.r.x - crop_px.x) * factor)), static_cast<i32>(std::lround((n.r.y - frame.r.y - crop_px.y) * factor)),
                             static_cast<i32>(std::lround(n.r.w * factor)), static_cast<i32>(std::lround(n.r.h * factor))};
        if (rel.intersect({0, 0, img->w, img->h}).empty()) continue;
        cap::draw_box(*img, rel, "e" + std::to_string(n.id));
        if (++drawn >= 80) break;
      }
    }
  }
  if (p["marks"].is_arr()) {
    for (const auto& m : p["marks"].arr()) {
      geo::LatLon ll;
      if (!parse_ll(m["at"], ll)) continue;
      const geo::PointI px = geo::to_px(frame, ll);
      cap::draw_marker(*img, {static_cast<i32>(std::lround((px.x - frame.r.x - crop_px.x) * factor)), static_cast<i32>(std::lround((px.y - frame.r.y - crop_px.y) * factor))},
                       m["label"].as_str());
    }
  }
  const bool grid = p["grid"].as_bool(st.grid_default);
  if (grid) {
    cap::GridSpec g;
    g.region = reg;
    g.cols = static_cast<int>(p["cols"].as_int(10));
    g.rows = static_cast<int>(p["rows"].as_int(10));
    g.labels = p["labels"].as_bool(true);
    cap::draw_grid(*img, g);
  }
  sw.reset();
  const std::string fmt = text::lower(p["format"].str_or("jpeg"));
  auto enc = cap::encode(*img, fmt == "png" ? cap::Fmt::Png : cap::Fmt::Jpeg, static_cast<int>(p["quality"].as_int(st.jpeg_quality)));
  if (!enc) return std::unexpected(enc.error());
  const u64 enc_us = sw.ns() / 1000;

  Json image = Json::object();
  image.set("mime", fmt == "png" ? "image/png" : "image/jpeg").set("b64", text::base64(enc->data(), enc->size())).set("w", img->w).set("h", img->h).set("bytes", enc->size());
  Json fj = Json::object();
  fj.set("x", frame.r.x).set("y", frame.r.y).set("w", frame.r.w).set("h", frame.r.h).set("dpi", frame.dpi);
  Json regj = Json::object();
  regj.set("a", geo::fmt(reg.a)).set("b", geo::fmt(reg.b));
  Json j = Json::object();
  j.set("ok", true).set("image", std::move(image)).set("frame", std::move(fj)).set("region", std::move(regj));
  j.set("method", cap::method_name(shot->method)).set("us_capture", cap_us).set("us_encode", enc_us).set("us", total.ns() / 1000);
  j.set("scale", std::round(factor * 1000) / 1000);
  if (grid) j.set("grid", Json::object().set("cols", static_cast<int>(p["cols"].as_int(10))).set("rows", static_cast<int>(p["rows"].as_int(10))));
  if (!changed.is_null()) j.set("changed", std::move(changed));
  if (t.hwnd) j.set("target", Json::object().set("hwnd", hwnd_str(t.hwnd)).set("app", t.app).set("title", t.title));
  return j;
}

Res<Json> Engine::q_elements(const Json& p) {
  auto tr = target_of(p, true);
  if (!tr) return std::unexpected(tr.error());
  Target t = *tr;
  if (t.screen) return fail(E_BAD_ARG, "elements needs a window");
  uia::SnapOpts so;
  so.include_offscreen = p["offscreen"].as_bool(false);
  so.force = p["refresh"].as_bool(false);
  so.ttl_ms = static_cast<u32>(p["max_age_ms"].as_int(350));
  so.max_nodes = static_cast<u32>(std::clamp<i64>(p["scan"].as_int(1500), 50, 6000));
  Stopwatch sw;
  Res<std::shared_ptr<uia::Snapshot>> s = so.force ? uia::Service::get().snapshot(t.hwnd, so) : snap_for(t, so.ttl_ms, true);
  if (!s) return std::unexpected(s.error());
  if (so.force || so.max_nodes != 1500 || so.include_offscreen) {
    // 非默认参数（刷新、扫描上限、含屏幕外）不走共享缓存。
  }
  const auto& snap = **s;
  const std::string query = p["query"].as_str();
  const std::string role = p["role"].as_str();
  const bool interactive = p["interactive"].as_bool(query.empty() && role.empty());
  const size_t limit = static_cast<size_t>(std::clamp<i64>(p["limit"].as_int(150), 1, 1000));
  std::vector<int> pick;
  if (!query.empty() || !role.empty()) {
    uia::FindQuery fq;
    fq.text = query;
    fq.role = role;
    fq.interactive = p["interactive"].as_bool(false);
    fq.limit = static_cast<int>(limit);
    if (query.empty() && role.empty()) fq.text = "";
    for (const auto& m : uia::Service::find(snap, fq)) pick.push_back(m.idx);
  } else {
    for (const auto& n : snap.nodes) {
      if (n.id == 0) continue;
      if (interactive && !is_interactive(n) && n.name.empty()) continue;
      if (interactive && !is_interactive(n) && n.role != "Text" && n.role != "Edit" && n.role != "Document") continue;
      pick.push_back(n.id);
      if (pick.size() >= limit) break;
    }
  }
  Json arr = Json::array();
  std::string txt;
  for (int id : pick) {
    const auto& n = snap.nodes[static_cast<size_t>(id)];
    arr.push(uia::Service::node_json(snap, n));
    txt += line_of(snap, n) + "\n";
  }
  Json j = Json::object();
  j.set("ok", true).set("count", pick.size()).set("total", snap.total).set("truncated", snap.truncated).set("nodes", std::move(arr)).set("text", txt);
  j.set("frame", rect_json(snap.frame.r)).set("us", sw.ns() / 1000).set("build_us", snap.build_us).set("gen", snap.gen);
  j.set("target", Json::object().set("hwnd", hwnd_str(t.hwnd)).set("app", t.app).set("title", t.title));
  return j;
}

Res<Json> Engine::q_find(const Json& p) {
  auto tr = target_of(p, true);
  if (!tr) return std::unexpected(tr.error());
  Target t = *tr;
  if (t.screen) return fail(E_BAD_ARG, "find needs a window");
  uia::FindQuery fq;
  fq.text = p["text"].as_str();
  fq.role = p["role"].as_str();
  fq.aid = p["aid"].as_str();
  fq.interactive = p["interactive"].as_bool(false);
  fq.limit = static_cast<int>(std::clamp<i64>(p["limit"].as_int(5), 1, 50));
  if (fq.text.empty() && fq.role.empty() && fq.aid.empty()) return fail(E_BAD_ARG, "text, role or aid is required");
  auto s = snap_for(t, static_cast<u32>(p["max_age_ms"].as_int(5000)), true);
  if (!s) return std::unexpected(s.error());
  Json arr = Json::array();
  for (const auto& m : uia::Service::find(**s, fq)) {
    Json n = uia::Service::node_json(**s, (*s)->nodes[static_cast<size_t>(m.idx)]);
    n.set("score", std::round(m.score * 100) / 100);
    arr.push(std::move(n));
  }
  Json j = Json::object();
  j.set("ok", true).set("count", arr.size()).set("matches", std::move(arr));
  return j;
}

Res<Json> Engine::q_locate(const Json& p) {
  auto tr = target_of(p, false);
  if (!tr) return std::unexpected(tr.error());
  Target t = *tr;
  auto pr = point_of(t, p, false);
  if (!pr) return std::unexpected(pr.error());
  Json j = Json::object();
  j.set("ok", true).set("at", geo::fmt(pr->ll)).set("px", px_json(pr->px));
  Json codes = Json::array();
  for (int lv = 2; lv <= geo::kMaxLevel; ++lv) codes.push(geo::code_of(pr->ll, lv));
  j.set("codes", std::move(codes));
  HWND top = t.hwnd ? t.hwnd : WindowFromPoint({pr->px.x, pr->px.y});
  if (top) {
    HWND child = win::deepest_child_at(GetAncestor(top, GA_ROOT), pr->px);
    wchar_t cls[128] = {};
    GetClassNameW(child, cls, 128);
    j.set("hwnd", hwnd_str(child)).set("class", text::narrow(cls));
    HWND root = GetAncestor(top, GA_ROOT);
    if (!t.hwnd && root) {
      auto wi = win::info(root);
      if (wi) j.set("window", win::to_json(*wi));
    }
    if (!t.screen || root) {
      auto s = uia::Service::get().snapshot(t.hwnd ? t.hwnd : root, {.ttl_ms = 800});
      if (s) {
        const int n = uia::Service::hit_test(**s, pr->px, false);
        if (n > 0) j.set("element", uia::Service::node_json(**s, (*s)->nodes[static_cast<size_t>(n)]));
        const int in = uia::Service::hit_test(**s, pr->px, true);
        if (in > 0 && in != n) j.set("interactive", uia::Service::node_json(**s, (*s)->nodes[static_cast<size_t>(in)]));
      }
    }
  }
  return j;
}

Res<Json> Engine::q_geo(const Json& p) {
  auto tr = target_of(p, false);
  if (!tr) return std::unexpected(tr.error());
  Target t = *tr;
  auto pr = point_of(t, p, false);
  if (!pr) return std::unexpected(pr.error());
  Json j = Json::object();
  j.set("ok", true).set("frame", rect_json(t.frame.r)).set("dpi", t.frame.dpi).set("at", geo::fmt(pr->ll)).set("px_client", px_json({pr->px.x - t.frame.r.x, pr->px.y - t.frame.r.y}));
  j.set("px_screen", px_json(pr->px)).set("code", geo::code_of(pr->ll, geo::level_for(t.frame)));
  Json nb = Json::array();
  for (const auto& c : geo::neighbors(geo::code_of(pr->ll, geo::level_for(t.frame)))) nb.push(c);
  j.set("neighbors", std::move(nb));
  const auto cell = geo::decode(geo::code_of(pr->ll, geo::level_for(t.frame)));
  if (cell.valid) j.set("cell", Json::object().set("a", geo::fmt(cell.cell.a)).set("b", geo::fmt(cell.cell.b)));
  return j;
}

Res<Json> Engine::q_read(const Json& p) {
  auto tr = target_of(p, true);
  if (!tr) return std::unexpected(tr.error());
  Target t = *tr;
  auto pr = point_of(t, p, true);
  if (!pr) return std::unexpected(pr.error());
  if (pr->node < 0) return fail(E_NOT_FOUND, "no UI Automation element at that point");
  const auto& n = pr->snap->nodes[static_cast<size_t>(pr->node)];
  Json j = uia::Service::node_json(*pr->snap, n);
  auto v = uia::Service::get().get_value(n);
  if (v) j.set("value", *v);
  if (n.patterns & uia::P_TOGGLE)
    if (auto ts = uia::Service::get().toggle_state(n)) j.set("toggle", *ts);
  Json out = Json::object();
  out.set("ok", true).set("element", std::move(j));
  return out;
}

Res<Json> Engine::q_status(const Json&) {
  const Settings st = SettingsStore::get().snapshot();
  Json j = Json::object();
  j.set("ok", true).set("version", DX_VERSION).set("uptime_s", (unix_ms() - started_ms_) / 1000).set("mode", st.mode).set("paused", st.paused);
  j.set("allow_hop", st.allow_hop).set("calls", calls_.load()).set("errors", errors_.load());
  Json c = Json::object();
  c.set("name", cpu().brand).set("threads", cpu().logical).set("bmi2", cpu().bmi2).set("avx2", cpu().avx2).set("sse42", cpu().sse42);
  j.set("cpu", std::move(c));
  j.set("data_dir", text::narrow(paths::data_dir().wstring())).set("portable", paths::portable());
  j.set("experience", exp_.summary_json());
  Json jr = Json::object();
  jr.set("entries", journal_.count()).set("recovered_bytes", journal_.recovered_bytes()).set("undoable", journal_.list(1000, true).size());
  j.set("journal", std::move(jr));
  const geo::Frame sf = win::screen_frame();
  j.set("screen", rect_json(sf.r)).set("dpi", sf.dpi);
  j.set("activity_hook", Activity::get().hook_active());
  return j;
}

Res<Json> Engine::q_perf(const Json&) {
  Json methods = Json::array();
  {
    std::lock_guard lk(hist_mu_);
    for (const auto& [name, h] : hist_) {
      Json m = Json::object();
      m.set("name", name).set("count", h->count()).set("p50_us", h->quantile(0.5) / 1000).set("p90_us", h->quantile(0.9) / 1000).set("p99_us", h->quantile(0.99) / 1000);
      m.set("min_us", h->min() / 1000).set("max_us", h->max() / 1000).set("mean_us", static_cast<u64>(h->mean() / 1000.0));
      methods.push(std::move(m));
    }
  }
  FILETIME c, e, k, u;
  GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
  PROCESS_MEMORY_COUNTERS_EX pm{};
  pm.cb = sizeof pm;
  GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pm), sizeof pm);
  Json proc = Json::object();
  proc.set("cpu_ms", (ft_to_100ns(k) + ft_to_100ns(u)) / 10000).set("working_set", pm.WorkingSetSize).set("private", pm.PrivateUsage).set("threads", 0);
  Json j = Json::object();
  j.set("ok", true).set("methods", std::move(methods)).set("process", std::move(proc)).set("uptime_s", (unix_ms() - started_ms_) / 1000);
  return j;
}

Res<Json> Engine::q_exp(const Json& p) {
  Json j = Json::object();
  j.set("ok", true).set("summary", exp_.summary_json()).set("arms", exp_.arms_json(p["app"].as_str(), static_cast<size_t>(std::clamp<i64>(p["limit"].as_int(200), 1, 2000))));
  if (p["export"].as_bool(false)) j.set("export", exp_.export_json());
  return j;
}

Res<Json> Engine::q_exp_reset(const Json&) {
  if (auto r = exp_.reset(); !r) return std::unexpected(r.error());
  Json j = Json::object();
  j.set("ok", true);
  return j;
}

Res<Json> Engine::q_journal(const Json& p) {
  const auto list = journal_.list(static_cast<size_t>(std::clamp<i64>(p["limit"].as_int(100), 1, 1000)), p["undoable"].as_bool(false));
  Json arr = Json::array();
  for (const auto& e : list) arr.push(store::Journal::to_json(e));
  Json j = Json::object();
  j.set("ok", true).set("count", arr.size()).set("entries", std::move(arr)).set("total", journal_.count());
  return j;
}

Res<Json> Engine::q_journal_clear(const Json&) {
  if (auto r = journal_.clear(); !r) return std::unexpected(r.error());
  Json j = Json::object();
  j.set("ok", true);
  return j;
}

Res<Json> Engine::q_log(const Json& p) {
  const std::string lv = p["level"].str_or("info");
  const Lv min = lv == "debug" ? Lv::Debug : lv == "warn" ? Lv::Warn : lv == "error" ? Lv::Error : Lv::Info;
  const auto recs = Log::get().tail(static_cast<size_t>(std::clamp<i64>(p["limit"].as_int(200), 1, 2000)), min, static_cast<u64>(p["after"].as_int(0)));
  Json arr = Json::array();
  for (const auto& r : recs) {
    Json o = Json::object();
    o.set("id", r.id).set("t", r.ts_ms).set("lv", lv_name(r.lv)).set("cat", r.cat).set("msg", r.msg);
    arr.push(std::move(o));
  }
  Json j = Json::object();
  j.set("ok", true).set("records", std::move(arr));
  return j;
}

Res<Json> Engine::q_settings_get(const Json&) {
  Json j = Json::object();
  j.set("ok", true).set("settings", SettingsStore::get().snapshot().to_json()).set("defaults", SettingsStore::get().defaults_json());
  return j;
}

Res<Json> Engine::q_settings_set(const Json& p) {
  auto r = SettingsStore::get().update(p["patch"].is_obj() ? p["patch"] : p);
  if (!r) return std::unexpected(r.error());
  Json j = Json::object();
  j.set("ok", true).set("settings", *r);
  return j;
}

Res<Json> Engine::q_stop(const Json&) {
  request_stop();
  Json j = Json::object();
  j.set("ok", true);
  return j;
}

}  // namespace dx::eng
