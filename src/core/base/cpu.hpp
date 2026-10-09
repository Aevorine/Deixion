#pragma once
#include "core/base/types.hpp"

namespace dx {

struct CpuFeatures {
  bool sse42{false};
  bool popcnt{false};
  bool bmi2{false};
  bool avx2{false};
  char brand[49]{};
  unsigned logical{1};
};

const CpuFeatures& cpu();

}  // namespace dx
