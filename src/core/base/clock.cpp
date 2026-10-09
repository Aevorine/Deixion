#include "core/base/clock.hpp"

#include <windows.h>
#include <immintrin.h>

namespace dx {
namespace {
struct Freq {
  u64 f;
  Freq() {
    LARGE_INTEGER l;
    QueryPerformanceFrequency(&l);
    f = static_cast<u64>(l.QuadPart);
  }
};
const Freq g_freq;

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x2
#endif

HANDLE hires_timer() {
  thread_local HANDLE h = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  return h;
}
}  // namespace

u64 now_ns() {
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  const u64 v = static_cast<u64>(c.QuadPart);
  return (v / g_freq.f) * 1000000000ull + (v % g_freq.f) * 1000000000ull / g_freq.f;
}

u64 unix_ms() {
  FILETIME ft;
  GetSystemTimePreciseAsFileTime(&ft);
  const u64 t = (static_cast<u64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
  return (t - 116444736000000000ull) / 10000ull;
}

void sleep_us(u64 us) {
  const u64 deadline = now_ns() + us * 1000;
  constexpr u64 kSpinNs = 150000;
  if (us * 1000 > kSpinNs + 300000) {
    const u64 coarse_ns = us * 1000 - kSpinNs;
    if (HANDLE h = hires_timer()) {
      LARGE_INTEGER due;
      due.QuadPart = -static_cast<LONGLONG>(coarse_ns / 100);
      if (SetWaitableTimer(h, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(h, INFINITE);
    } else {
      Sleep(static_cast<DWORD>(coarse_ns / 1000000));
    }
  }
  while (now_ns() < deadline) _mm_pause();
}

void init_timer_resolution() {
  using Fn = LONG(NTAPI*)(ULONG, BOOLEAN, PULONG);
  if (HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
    if (auto set = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(nt, "NtSetTimerResolution")))) {
      ULONG cur = 0;
      set(5000, TRUE, &cur);
    }
  }
}

}  // namespace dx
