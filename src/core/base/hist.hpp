#pragma once
#include <array>
#include <bit>

#include "core/base/types.hpp"

namespace dx {

// 对数-线性直方图：每个 2 的幂拆 16 格（相对误差 ≤ 6.25%），记录 O(1)、无锁、无分配。
class Hist {
 public:
  static constexpr unsigned kSub = 4;
  static constexpr unsigned kBuckets = 32 + (64 - 5) * 16;

  static unsigned bucket_of(u64 v) {
    if (v < 32) return static_cast<unsigned>(v);
    const unsigned msb = 63u - static_cast<unsigned>(std::countl_zero(v));
    const unsigned sub = static_cast<unsigned>(v >> (msb - kSub)) & 15u;
    return 32 + (msb - 5) * 16 + sub;
  }
  static u64 lower_of(unsigned idx) {
    if (idx < 32) return idx;
    const unsigned msb = 5 + (idx - 32) / 16;
    const unsigned sub = (idx - 32) % 16;
    return static_cast<u64>(16 + sub) << (msb - kSub);
  }

  void record(u64 v) {
    b_[bucket_of(v)].fetch_add(1, std::memory_order_relaxed);
    n_.fetch_add(1, std::memory_order_relaxed);
    sum_.fetch_add(v, std::memory_order_relaxed);
    u64 m = max_.load(std::memory_order_relaxed);
    while (v > m && !max_.compare_exchange_weak(m, v, std::memory_order_relaxed)) {}
    u64 lo = min_.load(std::memory_order_relaxed);
    while (v < lo && !min_.compare_exchange_weak(lo, v, std::memory_order_relaxed)) {}
  }

  u64 count() const { return n_.load(std::memory_order_relaxed); }
  u64 max() const { return max_.load(std::memory_order_relaxed); }
  u64 min() const { return count() ? min_.load(std::memory_order_relaxed) : 0; }
  double mean() const {
    const u64 n = count();
    return n ? static_cast<double>(sum_.load(std::memory_order_relaxed)) / static_cast<double>(n) : 0.0;
  }

  u64 quantile(double q) const {
    const u64 n = count();
    if (!n) return 0;
    const u64 target = static_cast<u64>(q * static_cast<double>(n - 1)) + 1;
    u64 acc = 0;
    for (unsigned i = 0; i < kBuckets; ++i) {
      acc += b_[i].load(std::memory_order_relaxed);
      if (acc >= target) return i + 1 < kBuckets ? (lower_of(i) + lower_of(i + 1)) / 2 : lower_of(i);
    }
    return max();
  }

  void reset() {
    for (auto& x : b_) x.store(0, std::memory_order_relaxed);
    n_ = 0;
    sum_ = 0;
    max_ = 0;
    min_ = ~0ull;
  }

 private:
  std::array<std::atomic<u64>, kBuckets> b_{};
  std::atomic<u64> n_{0}, sum_{0}, max_{0}, min_{~0ull};
};

}  // namespace dx
