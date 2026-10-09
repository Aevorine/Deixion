#pragma once
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <format>
#include <functional>
#include <mutex>
#include <thread>

#include "core/base/types.hpp"

namespace dx {

enum class Lv : u8 { Trace, Debug, Info, Warn, Error };

struct LogRec {
  u64 id{0};
  u64 ts_ms{0};
  Lv lv{Lv::Info};
  std::string cat;
  std::string msg;
};

const char* lv_name(Lv lv);

// 异步日志：调用线程只入队，后台线程落盘（JSON Lines，按天+按大小滚动，保留 14 天），内存环形缓冲供界面实时查看。
class Log {
 public:
  static Log& get();

  void start(const std::filesystem::path& dir);
  void stop();
  void set_level(Lv lv) { level_.store(static_cast<u8>(lv), std::memory_order_relaxed); }
  Lv level() const { return static_cast<Lv>(level_.load(std::memory_order_relaxed)); }
  bool enabled(Lv lv) const { return static_cast<u8>(lv) >= level_.load(std::memory_order_relaxed); }

  void write(Lv lv, std::string_view cat, std::string msg);
  std::vector<LogRec> tail(size_t n, Lv min = Lv::Info, u64 after_id = 0) const;
  u64 subscribe(std::function<void(const LogRec&)> fn);
  void unsubscribe(u64 token);

 private:
  Log() = default;
  void run();
  void rotate_locked();

  std::atomic<u8> level_{static_cast<u8>(Lv::Info)};
  std::atomic<u64> next_id_{1};
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<LogRec> queue_;
  std::deque<LogRec> ring_;
  std::vector<std::pair<u64, std::function<void(const LogRec&)>>> subs_;
  u64 next_sub_{1};
  std::thread th_;
  bool running_{false};
  std::filesystem::path dir_;
  void* file_{nullptr};
  u64 file_bytes_{0};
  std::string file_day_;
};

}  // namespace dx

#define DXLOG(lv, cat, ...)                                                     \
  do {                                                                          \
    if (::dx::Log::get().enabled(lv)) ::dx::Log::get().write(lv, cat, std::format(__VA_ARGS__)); \
  } while (0)
#define LOGI(cat, ...) DXLOG(::dx::Lv::Info, cat, __VA_ARGS__)
#define LOGW(cat, ...) DXLOG(::dx::Lv::Warn, cat, __VA_ARGS__)
#define LOGE(cat, ...) DXLOG(::dx::Lv::Error, cat, __VA_ARGS__)
#define LOGD(cat, ...) DXLOG(::dx::Lv::Debug, cat, __VA_ARGS__)
