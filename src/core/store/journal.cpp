#include "core/store/journal.hpp"

#include <algorithm>

#include "core/base/clock.hpp"

namespace dx::store {
namespace {
constexpr size_t kKeep = 3000;
enum : u16 { R_ENTRY = 1, R_UNDONE = 2 };

Json entry_json(const Entry& e) {
  Json j = Json::object();
  j.set("id", e.id).set("ts", e.ts_ms).set("method", e.method).set("params", e.params).set("app", e.app).set("hwnd", e.hwnd).set("title", e.title);
  j.set("strategy", e.strategy).set("ok", e.ok).set("confirmed", e.confirmed).set("us", e.us).set("err", e.err).set("undone", e.undone);
  Json inv = Json::object();
  inv.set("kind", e.inv.kind).set("data", e.inv.data).set("best_effort", e.inv.best_effort);
  j.set("inv", std::move(inv));
  return j;
}

Entry entry_from(const Json& j) {
  Entry e;
  e.id = static_cast<u64>(j["id"].as_int());
  e.ts_ms = static_cast<u64>(j["ts"].as_int());
  e.method = j["method"].as_str();
  e.params = j["params"];
  e.app = j["app"].as_str();
  e.hwnd = static_cast<u64>(j["hwnd"].as_int());
  e.title = j["title"].as_str();
  e.strategy = j["strategy"].as_str();
  e.ok = j["ok"].as_bool();
  e.confirmed = j["confirmed"].as_bool();
  e.us = static_cast<u64>(j["us"].as_int());
  e.err = j["err"].as_str();
  e.undone = j["undone"].as_bool();
  e.inv.kind = j["inv"]["kind"].str_or("none");
  e.inv.data = j["inv"]["data"];
  e.inv.best_effort = j["inv"]["best_effort"].as_bool();
  return e;
}
}  // namespace

Res<void> Journal::open(const std::filesystem::path& file) {
  std::lock_guard lk(mu_);
  auto r = log_.open(file, [&](u16 type, u64, std::span<const u8> p) {
    auto j = Json::parse(std::string_view(reinterpret_cast<const char*>(p.data()), p.size()));
    if (!j) return;
    if (type == R_ENTRY) {
      Entry e = entry_from(*j);
      next_id_ = std::max(next_id_, e.id + 1);
      items_.push_back(std::move(e));
      if (items_.size() > kKeep) items_.pop_front();
    } else if (type == R_UNDONE) {
      const u64 id = static_cast<u64>((*j)["id"].as_int());
      for (auto& e : items_)
        if (e.id == id) e.undone = true;
    }
  });
  if (!r) return r;
  if (log_.records() > kKeep * 3) compact_locked();
  return {};
}

void Journal::close() { log_.close(); }

void Journal::compact_locked() {
  std::vector<std::pair<u16, std::string>> recs;
  recs.reserve(items_.size());
  for (const auto& e : items_) recs.emplace_back(R_ENTRY, entry_json(e).dump());
  (void)log_.rewrite(recs);
}

u64 Journal::add(Entry e) {
  std::lock_guard lk(mu_);
  e.id = next_id_++;
  e.ts_ms = unix_ms();
  (void)log_.append_json(R_ENTRY, entry_json(e));
  const u64 id = e.id;
  items_.push_back(std::move(e));
  if (items_.size() > kKeep) items_.pop_front();
  return id;
}

void Journal::mark_undone(u64 id) {
  std::lock_guard lk(mu_);
  for (auto& e : items_)
    if (e.id == id) e.undone = true;
  Json j = Json::object();
  j.set("id", id);
  (void)log_.append_json(R_UNDONE, j);
}

std::vector<Entry> Journal::list(size_t limit, bool only_undoable) const {
  std::lock_guard lk(mu_);
  std::vector<Entry> out;
  for (auto it = items_.rbegin(); it != items_.rend() && out.size() < limit; ++it) {
    if (only_undoable && (it->undone || !it->ok || it->inv.kind == "none" || it->inv.kind.empty())) continue;
    out.push_back(*it);
  }
  return out;
}

std::optional<Entry> Journal::get(u64 id) const {
  std::lock_guard lk(mu_);
  for (const auto& e : items_)
    if (e.id == id) return e;
  return std::nullopt;
}

std::vector<Entry> Journal::undo_plan(size_t count) const { return list(count, true); }

Res<void> Journal::clear() {
  std::lock_guard lk(mu_);
  items_.clear();
  return log_.rewrite({});
}

u64 Journal::count() const {
  std::lock_guard lk(mu_);
  return items_.size();
}

Json Journal::to_json(const Entry& e) { return entry_json(e); }

}  // namespace dx::store
