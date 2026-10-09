#include "core/base/crc32c.hpp"

#include <immintrin.h>

#include <array>
#include <cstring>

#include "core/base/cpu.hpp"

namespace dx {
namespace {
constexpr std::array<u32, 256> make_table() {
  std::array<u32, 256> t{};
  for (u32 i = 0; i < 256; ++i) {
    u32 c = i;
    for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0x82F63B78u : (c >> 1);
    t[i] = c;
  }
  return t;
}
constexpr auto kTable = make_table();

u32 soft(const u8* p, size_t n, u32 crc) {
  for (size_t i = 0; i < n; ++i) crc = kTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return crc;
}

__attribute__((target("sse4.2"))) u32 hard(const u8* p, size_t n, u32 crc) {
  u64 c = crc;
  while (n >= 8) {
    u64 v;
    std::memcpy(&v, p, 8);
    c = _mm_crc32_u64(c, v);
    p += 8;
    n -= 8;
  }
  u32 c32 = static_cast<u32>(c);
  while (n--) c32 = _mm_crc32_u8(c32, *p++);
  return c32;
}
}  // namespace

u32 crc32c(const void* data, size_t n, u32 seed) {
  static const bool hw = cpu().sse42;
  const u32 init = ~seed;
  const u32 r = hw ? hard(static_cast<const u8*>(data), n, init) : soft(static_cast<const u8*>(data), n, init);
  return ~r;
}

}  // namespace dx
