#pragma once
#include "core/base/types.hpp"

namespace dx {

// CRC-32C（Castagnoli）。有 SSE4.2 时走硬件指令，否则走查表，结果一致。
u32 crc32c(const void* data, size_t n, u32 seed = 0);

}  // namespace dx
