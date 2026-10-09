#pragma once
#include "core/base/types.hpp"

namespace dx {

u64 now_ns();
inline u64 now_us() { return now_ns() / 1000; }
u64 unix_ms();

// 高精度休眠：长段用高分辨率等待计时器，末尾 ~150 µs 自旋，保证亚毫秒误差。
void sleep_us(u64 us);
void init_timer_resolution();

class Stopwatch {
 public:
  Stopwatch() : t0_(now_ns()) {}
  u64 ns() const { return now_ns() - t0_; }
  double ms() const { return static_cast<double>(ns()) / 1e6; }
  void reset() { t0_ = now_ns(); }

 private:
  u64 t0_;
};

}  // namespace dx
