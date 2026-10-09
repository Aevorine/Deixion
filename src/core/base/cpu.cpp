#include "core/base/cpu.hpp"

#include <windows.h>
#include <cpuid.h>
#include <immintrin.h>

#include <cstring>

namespace dx {
namespace {
CpuFeatures detect() {
  CpuFeatures f;
  unsigned a = 0, b = 0, c = 0, d = 0;
  if (__get_cpuid(1, &a, &b, &c, &d)) {
    f.sse42 = (c >> 20) & 1;
    f.popcnt = (c >> 23) & 1;
    const bool osxsave = (c >> 27) & 1;
    const bool avx = (c >> 28) & 1;
    bool ymm = false;
    if (osxsave && avx) {
      unsigned lo, hi;
      __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
      ymm = (lo & 6) == 6;
    }
    unsigned a7 = 0, b7 = 0, c7 = 0, d7 = 0;
    if (__get_cpuid_count(7, 0, &a7, &b7, &c7, &d7)) {
      f.bmi2 = (b7 >> 8) & 1;
      f.avx2 = ymm && ((b7 >> 5) & 1);
    }
  }
  unsigned char* p = reinterpret_cast<unsigned char*>(f.brand);
  for (unsigned i = 0; i < 3; ++i) {
    if (__get_cpuid(0x80000002 + i, &a, &b, &c, &d)) {
      std::memcpy(p + i * 16 + 0, &a, 4);
      std::memcpy(p + i * 16 + 4, &b, 4);
      std::memcpy(p + i * 16 + 8, &c, 4);
      std::memcpy(p + i * 16 + 12, &d, 4);
    }
  }
  f.brand[48] = 0;
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  f.logical = si.dwNumberOfProcessors;
  return f;
}
}  // namespace

const CpuFeatures& cpu() {
  static const CpuFeatures f = detect();
  return f;
}

}  // namespace dx
