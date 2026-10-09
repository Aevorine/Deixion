#include "core/engine/activity.hpp"

#include <mutex>

#include "core/base/clock.hpp"

namespace dx::eng {

Activity& Activity::get() {
  static Activity a;
  return a;
}

namespace {
constexpr u64 kPidTag = 1ull << 63;
u64 key_root(HWND h) { return static_cast<u64>(reinterpret_cast<uintptr_t>(h)) & ~kPidTag; }
u64 key_pid(u32 pid) { return kPidTag | pid; }
}  // namespace

void Activity::bump(u64 key) {
  const size_t i0 = static_cast<size_t>((key * 0x9E3779B97F4A7C15ull) >> 54) % kSlots;
  for (size_t k = 0; k < 12; ++k) {
    Slot& s = slots_[(i0 + k) % kSlots];
    u64 cur = s.key.load(std::memory_order_relaxed);
    if (cur == key || (cur == 0 && s.key.compare_exchange_strong(cur, key)) || cur == key) {
      s.count.fetch_add(1, std::memory_order_relaxed);
      s.last.store(now_ns(), std::memory_order_relaxed);
      return;
    }
  }
}

Activity::Mark Activity::read(u64 key) const {
  const size_t i0 = static_cast<size_t>((key * 0x9E3779B97F4A7C15ull) >> 54) % kSlots;
  for (size_t k = 0; k < 12; ++k) {
    const Slot& s = slots_[(i0 + k) % kSlots];
    if (s.key.load(std::memory_order_relaxed) == key) return {s.count.load(std::memory_order_relaxed), s.last.load(std::memory_order_relaxed)};
  }
  return {};
}

void Activity::proc(HWINEVENTHOOK, DWORD ev, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
  if (!hwnd) return;
  if (idObject == OBJID_CURSOR || idObject == OBJID_CARET) return;
  Activity& a = get();
  HWND root = GetAncestor(hwnd, GA_ROOT);
  if (root) a.bump(key_root(root));
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid) a.bump(key_pid(pid));
  (void)ev;
}

void Activity::run() {
  tid_ = GetCurrentThreadId();
  MSG m;
  PeekMessageW(&m, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
  HWINEVENTHOOK h1 = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_VALUECHANGE, nullptr, proc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  HWINEVENTHOOK h2 = SetWinEventHook(EVENT_SYSTEM_MENUSTART, EVENT_SYSTEM_DIALOGEND, nullptr, proc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  HWINEVENTHOOK h3 = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, proc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  hooked_ = true;
  UINT_PTR timer = SetTimer(nullptr, 0, 5000, nullptr);
  while (running_ && GetMessageW(&m, nullptr, 0, 0) > 0) {
    if (m.message == WM_TIMER && unix_ms() - last_touch_ms_.load() > 60000) break;
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  KillTimer(nullptr, timer);
  if (h1) UnhookWinEvent(h1);
  if (h2) UnhookWinEvent(h2);
  if (h3) UnhookWinEvent(h3);
  hooked_ = false;
  running_ = false;
}

void Activity::touch() {
  last_touch_ms_ = unix_ms();
  if (running_.load()) return;
  std::lock_guard lk(mu_);
  if (running_.load()) return;
  if (th_.joinable()) th_.join();
  running_ = true;
  th_ = std::thread([this] { run(); });
  const u64 end = now_us() + 50000;
  while (!hooked_.load() && now_us() < end) sleep_us(200);
}

void Activity::stop() {
  std::lock_guard lk(mu_);
  running_ = false;
  if (const DWORD t = tid_.load()) PostThreadMessageW(t, WM_QUIT, 0, 0);
  if (th_.joinable()) th_.join();
}

Activity::Mark Activity::mark(HWND root, u32 pid) const {
  Mark a = read(key_root(root)), b = pid ? read(key_pid(pid)) : Mark{};
  return {a.count + b.count, std::max(a.last_ns, b.last_ns)};
}

u64 Activity::wait_change(HWND root, u32 pid, const Mark& before, u64 timeout_us) {
  const u64 t0 = now_us();
  for (;;) {
    if (mark(root, pid).count != before.count) return std::max<u64>(1, now_us() - t0);
    if (now_us() - t0 >= timeout_us) return 0;
    sleep_us(120);
  }
}

bool Activity::wait_quiet(HWND root, u32 pid, u64 quiet_us, u64 timeout_us) {
  const u64 t0 = now_us();
  for (;;) {
    const Mark m = mark(root, pid);
    const u64 now = now_ns();
    const u64 since = m.last_ns ? (now > m.last_ns ? (now - m.last_ns) / 1000 : 0) : quiet_us;
    if (since >= quiet_us) return true;
    if (now_us() - t0 >= timeout_us) return false;
    sleep_us(std::min<u64>(quiet_us - since, 800));
  }
}

}  // namespace dx::eng
