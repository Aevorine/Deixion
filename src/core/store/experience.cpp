#include "core/store/experience.hpp"

#include <algorithm>
#include <cmath>

#include "core/base/clock.hpp"

namespace dx::store {
namespace {
constexpr double kDecay = 0.985;
constexpr double kPriorN = 1.5;
constexpr double kExplore = 0.35;
enum : u16 { R_ARM = 1, R_NUM = 2 };

Json arm_to_json(const std::string& key, const ArmStat& s) {
  Json j = Json::object();
  j.set("k", key).set("n", s.n).set("r", s.reward).set("s", s.succ).set("lm", s.lat_mean).set("lv", s.lat_var).set("t", s.total).set("ts", s.last_ms);
  return j;
}
}  // namespace

Res<void> Experience::open(const std::filesystem::path& file) {
  std::lock_guard lk(mu_);
  auto r = log_.open(file, [&](u16 type, u64, std::span<const u8> p) {
    auto j = Json::parse(std::string_view(reinterpret_cast<const char*>(p.data()), p.size()));
    if (!j) return;
    if (type == R_ARM) {
      ArmStat s;
      s.n = (*j)["n"].as_num();
      s.reward = (*j)["r"].as_num();
      s.succ = (*j)["s"].as_num();
      s.lat_mean = (*j)["lm"].as_num();
      s.lat_var = (*j)["lv"].as_num();
      s.total = static_cast<u64>((*j)["t"].as_int());
      s.last_ms = static_cast<u64>((*j)["ts"].as_int());
      arms_[(*j)["k"].as_str()] = s;
    } else if (type == R_NUM) {
      nums_[(*j)["k"].as_str()] = (*j)["v"].as_num();
    }
  });
  if (!r) return r;
  if (log_.records() > arms_.size() * 6 + nums_.size() * 4 + 4000) compact_locked();
  return {};
}

void Experience::close() {
  flush();
  log_.close();
}

void Experience::write_arm_locked(const std::string& key, const ArmStat& s) { (void)log_.append_json(R_ARM, arm_to_json(key, s)); }

void Experience::compact_locked() {
  std::vector<std::pair<u16, std::string>> recs;
  recs.reserve(arms_.size() + nums_.size());
  for (const auto& [k, s] : arms_) recs.emplace_back(R_ARM, arm_to_json(k, s).dump());
  for (const auto& [k, v] : nums_) {
    Json j = Json::object();
    j.set("k", k).set("v", v);
    recs.emplace_back(R_NUM, j.dump());
  }
  (void)log_.rewrite(recs);
}

void Experience::update(const ArmKey& k, bool success, double reward, double latency_ms) {
  std::lock_guard lk(mu_);
  const std::string key = k.str();
  ArmStat& s = arms_[key];
  s.n = s.n * kDecay + 1.0;
  s.reward = s.reward * kDecay + reward;
  s.succ = s.succ * kDecay + (success ? 1.0 : 0.0);
  if (success) {
    const double a = s.total == 0 ? 1.0 : 0.15;
    const double d = latency_ms - s.lat_mean;
    s.lat_mean += a * d;
    s.lat_var = (1 - a) * (s.lat_var + a * d * d);
  }
  ++s.total;
  s.last_ms = unix_ms();
  dirty_[key] = true;
  if (++since_flush_ >= 12) {
    for (const auto& [dk, _] : dirty_) write_arm_locked(dk, arms_[dk]);
    dirty_.clear();
    since_flush_ = 0;
    if (log_.records() > arms_.size() * 12 + 6000) compact_locked();
  }
}

void Experience::flush() {
  std::lock_guard lk(mu_);
  for (const auto& [dk, _] : dirty_) write_arm_locked(dk, arms_[dk]);
  dirty_.clear();
  since_flush_ = 0;
  log_.sync();
}

ArmStat Experience::get(const ArmKey& k) const {
  std::lock_guard lk(mu_);
  auto it = arms_.find(k.str());
  return it == arms_.end() ? ArmStat{} : it->second;
}

double Experience::score(const ArmStat& s, double prior, double total_n) const {
  const double n = s.n + kPriorN;
  const double mean = (s.reward + prior * kPriorN) / n;
  return mean + kExplore * std::sqrt(std::log(total_n + 2.0) / n);
}

std::vector<std::string> Experience::rank(const std::string& app, const std::string& role, const std::string& action,
                                          const std::vector<std::string>& cands) const {
  std::lock_guard lk(mu_);
  struct Row {
    std::string name;
    double score;
    size_t order;
  };
  std::vector<ArmStat> st(cands.size());
  double total = 0;
  for (size_t i = 0; i < cands.size(); ++i) {
    auto it = arms_.find(ArmKey{app, role, action, cands[i]}.str());
    if (it != arms_.end()) st[i] = it->second;
    total += st[i].n;
  }
  std::vector<Row> rows;
  for (size_t i = 0; i < cands.size(); ++i) {
    const double prior = 0.9 - 0.1 * static_cast<double>(i);
    rows.push_back({cands[i], score(st[i], prior, total), i});
  }
  std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.score > b.score; });
  std::vector<std::string> out;
  for (auto& r : rows) out.push_back(std::move(r.name));
  return out;
}

double Experience::get_num(const std::string& key, double dflt) const {
  std::lock_guard lk(mu_);
  auto it = nums_.find(key);
  return it == nums_.end() ? dflt : it->second;
}

void Experience::set_num(const std::string& key, double v) {
  std::lock_guard lk(mu_);
  auto it = nums_.find(key);
  const bool known = it != nums_.end();
  const double prev = known ? it->second : 0.0;
  nums_[key] = v;
  // 数值每次都更新内存，只有变化超过 5% 才落盘，避免每个动作都写一条记录。
  if (known && std::fabs(prev - v) <= 0.05 * std::max(1.0, std::fabs(prev))) return;
  Json j = Json::object();
  j.set("k", key).set("v", v);
  (void)log_.append_json(R_NUM, j);
}

Json Experience::arms_json(const std::string& app_filter, size_t limit) const {
  std::lock_guard lk(mu_);
  std::vector<std::pair<std::string, ArmStat>> v(arms_.begin(), arms_.end());
  std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second.last_ms > b.second.last_ms; });
  Json arr = Json::array();
  for (const auto& [k, s] : v) {
    if (!app_filter.empty() && k.find(app_filter) != 0) continue;
    if (arr.size() >= limit) break;
    Json j = Json::object();
    size_t p0 = k.find('|'), p1 = k.find('|', p0 + 1), p2 = k.find('|', p1 + 1);
    if (p0 == std::string::npos || p1 == std::string::npos || p2 == std::string::npos) continue;
    j.set("app", k.substr(0, p0)).set("role", k.substr(p0 + 1, p1 - p0 - 1)).set("action", k.substr(p1 + 1, p2 - p1 - 1)).set("strategy", k.substr(p2 + 1));
    j.set("n", std::round(s.n * 100) / 100).set("success_rate", s.n > 0 ? std::round(s.succ / s.n * 1000) / 1000 : 0.0);
    j.set("mean_reward", s.n > 0 ? std::round(s.reward / s.n * 1000) / 1000 : 0.0).set("latency_ms", std::round(s.lat_mean * 1000) / 1000);
    j.set("total", s.total).set("last_ms", s.last_ms);
    arr.push(std::move(j));
  }
  return arr;
}

Json Experience::summary_json() const {
  std::lock_guard lk(mu_);
  u64 total = 0;
  std::unordered_map<std::string, bool> apps;
  for (const auto& [k, s] : arms_) {
    total += s.total;
    apps[k.substr(0, k.find('|'))] = true;
  }
  Json j = Json::object();
  j.set("arms", arms_.size()).set("apps", apps.size()).set("observations", total).set("records", log_.records());
  j.set("bytes", log_.bytes()).set("recovered_bytes", log_.dropped_bytes());
  return j;
}

Res<void> Experience::reset() {
  std::lock_guard lk(mu_);
  arms_.clear();
  nums_.clear();
  dirty_.clear();
  return log_.rewrite({});
}

Json Experience::export_json() const {
  std::lock_guard lk(mu_);
  Json o = Json::object();
  Json a = Json::array();
  for (const auto& [k, s] : arms_) a.push(arm_to_json(k, s));
  Json n = Json::object();
  for (const auto& [k, v] : nums_) n.set(k, v);
  o.set("version", 1).set("arms", std::move(a)).set("numbers", std::move(n));
  return o;
}

}  // namespace dx::store
