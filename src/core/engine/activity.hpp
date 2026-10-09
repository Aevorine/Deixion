#pragma once
#include <windows.h>

#include <array>
#include <mutex>
#include <thread>

#include "core/base/types.hpp"

namespace dx::eng {

// 事件驱动的“界面是否在动”探测：用 WinEvent 钩子统计每个根窗口和每个进程最近一次界面事件的时间，
// 动作后据此判断有没有反应、什么时候稳定，替代固定 sleep。按根窗口计数能覆盖 UWP（内容窗口属于另一个进程）；
// 空闲 60 秒自动卸钩，不占 CPU。
class Activity {
 public:
  static Activity& get();

  struct Mark {
    u64 count{0};
    u64 last_ns{0};
  };

  void touch();
  Mark mark(HWND root, u32 pid) const;
  // 等到事件计数相对 before 增加；返回等到的耗时(µs)，超时返回 0。
  u64 wait_change(HWND root, u32 pid, const Mark& before, u64 timeout_us);
  // 等到连续 quiet_us 内没有事件；返回是否在 timeout 内稳定。
  bool wait_quiet(HWND root, u32 pid, u64 quiet_us, u64 timeout_us);
  bool hook_active() const { return hooked_.load(); }
  void stop();

 private:
  Activity() = default;
  void run();
  void bump(u64 key);
  Mark read(u64 key) const;
  static void CALLBACK proc(HWINEVENTHOOK, DWORD ev, HWND, LONG idObject, LONG idChild, DWORD, DWORD);

  struct Slot {
    std::atomic<u64> key{0};
    std::atomic<u64> count{0};
    std::atomic<u64> last{0};
  };
  static constexpr size_t kSlots = 1024;
  std::array<Slot, kSlots> slots_{};
  std::atomic<bool> hooked_{false};
  std::atomic<bool> running_{false};
  std::atomic<u64> last_touch_ms_{0};
  std::atomic<DWORD> tid_{0};
  std::thread th_;
  std::mutex mu_;
};

}  // namespace dx::eng
