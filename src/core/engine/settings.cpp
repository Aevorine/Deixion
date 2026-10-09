#include "core/engine/settings.hpp"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include "core/base/log.hpp"

namespace dx::eng {
namespace fs = std::filesystem;

namespace {
Json default_hotkeys() {
  Json h = Json::object();
  h.set("toggle", "ctrl+alt+d").set("mode", "ctrl+alt+m").set("pause", "ctrl+alt+u").set("undo", "ctrl+alt+b").set("shot", "ctrl+alt+s").set("stop", "ctrl+alt+x");
  return h;
}
bool one_of(const std::string& v, std::initializer_list<const char*> opts) {
  for (const char* o : opts)
    if (v == o) return true;
  return false;
}
}  // namespace

Json Settings::to_json() const {
  Json j = Json::object();
  j.set("mode", mode).set("allow_hop", allow_hop).set("allow_shell_launch", allow_shell_launch).set("launch_strict", launch_strict).set("speed", speed).set("overlay", overlay).set("verify", verify);
  j.set("jpeg_quality", jpeg_quality).set("max_image_dim", max_image_dim).set("grid_default", grid_default).set("log_level", log_level);
  j.set("autostart", autostart).set("check_updates", check_updates).set("close_to_tray", close_to_tray).set("paused", paused);
  j.set("theme", theme).set("density", density).set("language", language).set("hotkeys", hotkeys);
  Json allow = Json::array();
  for (const auto& e : launch_allow) allow.push(e);
  j.set("launch_allow", std::move(allow));
  return j;
}

Settings Settings::from_json(const Json& j) {
  Settings s;
  s.hotkeys = default_hotkeys();
  if (one_of(j["mode"].as_str(), {"background", "foreground"})) s.mode = j["mode"].as_str();
  if (j.has("allow_hop")) s.allow_hop = j["allow_hop"].as_bool(s.allow_hop);
  if (j.has("allow_shell_launch")) s.allow_shell_launch = j["allow_shell_launch"].as_bool(false);
  if (j.has("launch_strict")) s.launch_strict = j["launch_strict"].as_bool(false);
  if (j["launch_allow"].is_arr())
    for (const auto& e : j["launch_allow"].arr())
      if (e.is_str() && !e.as_str().empty() && e.as_str().size() <= 520 && s.launch_allow.size() < 200) s.launch_allow.push_back(e.as_str());
  if (one_of(j["speed"].as_str(), {"instant", "fast", "smooth"})) s.speed = j["speed"].as_str();
  if (j.has("overlay")) s.overlay = j["overlay"].as_bool(s.overlay);
  if (one_of(j["verify"].as_str(), {"auto", "off"})) s.verify = j["verify"].as_str();
  if (j.has("jpeg_quality")) s.jpeg_quality = static_cast<int>(std::clamp<i64>(j["jpeg_quality"].as_int(78), 30, 100));
  if (j.has("max_image_dim")) s.max_image_dim = static_cast<int>(std::clamp<i64>(j["max_image_dim"].as_int(1568), 400, 4096));
  if (j.has("grid_default")) s.grid_default = j["grid_default"].as_bool(true);
  if (one_of(j["log_level"].as_str(), {"debug", "info", "warn", "error"})) s.log_level = j["log_level"].as_str();
  if (j.has("autostart")) s.autostart = j["autostart"].as_bool(false);
  if (j.has("check_updates")) s.check_updates = j["check_updates"].as_bool(true);
  if (j.has("close_to_tray")) s.close_to_tray = j["close_to_tray"].as_bool(true);
  if (j.has("paused")) s.paused = j["paused"].as_bool(false);
  if (one_of(j["theme"].as_str(), {"auto", "light", "dark"})) s.theme = j["theme"].as_str();
  if (one_of(j["density"].as_str(), {"compact", "standard", "relaxed"})) s.density = j["density"].as_str();
  if (one_of(j["language"].as_str(), {"auto", "zh", "en"})) s.language = j["language"].as_str();
  if (j["hotkeys"].is_obj())
    for (const auto& kv : j["hotkeys"].obj())
      if (kv.second.is_str() && !kv.second.as_str().empty()) s.hotkeys.set(kv.first, kv.second.as_str());
  return s;
}

SettingsStore::SettingsStore() { s_ = Settings::from_json(Json::object()); }

SettingsStore& SettingsStore::get() {
  static SettingsStore s;
  return s;
}

namespace {
bool read_file(const fs::path& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}
}  // namespace

void SettingsStore::load(const fs::path& file) {
  std::lock_guard lk(mu_);
  file_ = file;
  std::string txt;
  for (const fs::path& p : {file, fs::path(file.wstring() + L".bak")}) {
    if (!read_file(p, txt)) continue;
    auto j = Json::parse(txt);
    if (j && j->is_obj()) {
      s_ = Settings::from_json(*j);
      if (p != file) {
        LOGW("settings", "settings.json unreadable, restored from backup");
        (void)save_locked();
      }
      return;
    }
  }
  s_ = Settings::from_json(Json::object());
}

Settings SettingsStore::snapshot() const {
  std::lock_guard lk(mu_);
  return s_;
}

Json SettingsStore::defaults_json() const { return Settings::from_json(Json::object()).to_json(); }

Res<void> SettingsStore::save_locked() {
  if (file_.empty()) return {};
  std::error_code ec;
  fs::create_directories(file_.parent_path(), ec);
  const fs::path tmp = file_.wstring() + L".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return fail(E_IO, "cannot write settings");
    const std::string s = s_.to_json().dump();
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
    f.flush();
    if (!f) return fail(E_IO, "cannot write settings");
  }
  if (fs::exists(file_, ec)) fs::copy_file(file_, file_.wstring() + L".bak", fs::copy_options::overwrite_existing, ec);
  if (!MoveFileExW(tmp.c_str(), file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return fail(E_IO, "cannot replace settings file");
  return {};
}

Res<Json> SettingsStore::update(const Json& patch) {
  Settings next;
  std::vector<std::function<void(const Settings&)>> subs;
  {
    std::lock_guard lk(mu_);
    Json merged = s_.to_json();
    for (const auto& kv : patch.obj()) {
      if (kv.first == "hotkeys" && kv.second.is_obj()) {
        for (const auto& hk : kv.second.obj()) merged["hotkeys"].set(hk.first, hk.second);
      } else {
        merged.set(kv.first, kv.second);
      }
    }
    s_ = Settings::from_json(merged);
    next = s_;
    if (auto r = save_locked(); !r) return std::unexpected(r.error());
    for (auto& s : subs_) subs.push_back(s.second);
  }
  for (auto& f : subs) f(next);
  return next.to_json();
}

u64 SettingsStore::subscribe(std::function<void(const Settings&)> fn) {
  std::lock_guard lk(mu_);
  const u64 t = next_++;
  subs_.emplace_back(t, std::move(fn));
  return t;
}

void SettingsStore::unsubscribe(u64 token) {
  std::lock_guard lk(mu_);
  std::erase_if(subs_, [&](auto& p) { return p.first == token; });
}

}  // namespace dx::eng
