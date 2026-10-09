#include "core/base/log.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>

#include "core/base/clock.hpp"
#include "core/base/json.hpp"
#include "core/base/text.hpp"

namespace dx {
namespace fs = std::filesystem;

namespace {
constexpr size_t kRing = 4000;
constexpr u64 kMaxFile = 8ull * 1024 * 1024;

std::string today() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char b[16];
  std::snprintf(b, sizeof b, "%04d%02d%02d", st.wYear, st.wMonth, st.wDay);
  return b;
}
}  // namespace

const char* lv_name(Lv lv) {
  switch (lv) {
    case Lv::Trace: return "trace";
    case Lv::Debug: return "debug";
    case Lv::Info: return "info";
    case Lv::Warn: return "warn";
    default: return "error";
  }
}

Log& Log::get() {
  static Log l;
  return l;
}

void Log::start(const fs::path& dir) {
  std::lock_guard lk(mu_);
  if (running_) return;
  dir_ = dir;
  std::error_code ec;
  fs::create_directories(dir_, ec);
  const auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(24 * 14);
  for (const auto& e : fs::directory_iterator(dir_, ec)) {
    if (e.path().extension() == L".jsonl" && fs::last_write_time(e.path(), ec) < cutoff) fs::remove(e.path(), ec);
  }
  running_ = true;
  th_ = std::thread([this] { run(); });
}

void Log::stop() {
  {
    std::lock_guard lk(mu_);
    if (!running_) return;
    running_ = false;
  }
  cv_.notify_all();
  if (th_.joinable()) th_.join();
  if (file_) CloseHandle(static_cast<HANDLE>(file_));
  file_ = nullptr;
}

void Log::write(Lv lv, std::string_view cat, std::string msg) {
  LogRec r;
  r.id = next_id_.fetch_add(1, std::memory_order_relaxed);
  r.ts_ms = unix_ms();
  r.lv = lv;
  r.cat = std::string(cat);
  r.msg = std::move(msg);
  std::vector<std::function<void(const LogRec&)>> subs;
  {
    std::lock_guard lk(mu_);
    ring_.push_back(r);
    if (ring_.size() > kRing) ring_.pop_front();
    if (running_) queue_.push_back(r);
    for (auto& s : subs_) subs.push_back(s.second);
  }
  cv_.notify_one();
  for (auto& f : subs) f(r);
}

std::vector<LogRec> Log::tail(size_t n, Lv min, u64 after_id) const {
  std::lock_guard lk(mu_);
  std::vector<LogRec> out;
  for (auto it = ring_.rbegin(); it != ring_.rend() && out.size() < n; ++it) {
    if (it->id <= after_id) break;
    if (it->lv >= min) out.push_back(*it);
  }
  std::reverse(out.begin(), out.end());
  return out;
}

u64 Log::subscribe(std::function<void(const LogRec&)> fn) {
  std::lock_guard lk(mu_);
  const u64 t = next_sub_++;
  subs_.emplace_back(t, std::move(fn));
  return t;
}

void Log::unsubscribe(u64 token) {
  std::lock_guard lk(mu_);
  std::erase_if(subs_, [&](auto& p) { return p.first == token; });
}

void Log::rotate_locked() {
  if (file_) CloseHandle(static_cast<HANDLE>(file_));
  file_ = nullptr;
  const std::string day = today();
  fs::path p = dir_ / text::widen("deixion-" + day + ".jsonl");
  std::error_code ec;
  if (fs::exists(p, ec) && fs::file_size(p, ec) >= kMaxFile) {
    fs::rename(p, dir_ / text::widen("deixion-" + day + "-" + std::to_string(unix_ms()) + ".jsonl"), ec);
  }
  HANDLE h = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  file_ = h == INVALID_HANDLE_VALUE ? nullptr : h;
  file_day_ = day;
  LARGE_INTEGER sz{};
  if (file_) GetFileSizeEx(static_cast<HANDLE>(file_), &sz);
  file_bytes_ = static_cast<u64>(sz.QuadPart);
}

void Log::run() {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
  std::deque<LogRec> batch;
  for (;;) {
    bool done = false;
    {
      std::unique_lock lk(mu_);
      cv_.wait_for(lk, std::chrono::milliseconds(500), [&] { return !queue_.empty() || !running_; });
      batch.swap(queue_);
      done = !running_ && batch.empty();
      if (!file_ || file_day_ != today() || file_bytes_ >= kMaxFile) rotate_locked();
    }
    if (!batch.empty() && file_) {
      std::string buf;
      for (const auto& r : batch) {
        Json j = Json::object();
        j.set("t", r.ts_ms).set("lv", lv_name(r.lv)).set("cat", r.cat).set("msg", r.msg);
        j.dump_to(buf);
        buf.push_back('\n');
      }
      DWORD w = 0;
      WriteFile(static_cast<HANDLE>(file_), buf.data(), static_cast<DWORD>(buf.size()), &w, nullptr);
      file_bytes_ += w;
      batch.clear();
    }
    if (done) break;
  }
}

}  // namespace dx
