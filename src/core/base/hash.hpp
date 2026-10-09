#pragma once
#include <cstring>
#include <string_view>

#include "core/base/types.hpp"

namespace dx {

namespace detail {
inline u64 mum(u64 a, u64 b) {
  const __uint128_t r = static_cast<__uint128_t>(a) * b;
  return static_cast<u64>(r) ^ static_cast<u64>(r >> 64);
}
inline u64 r64(const u8* p) {
  u64 v;
  std::memcpy(&v, p, 8);
  return v;
}
inline u64 r32(const u8* p) {
  u32 v;
  std::memcpy(&v, p, 4);
  return v;
}
}  // namespace detail

// 128 位乘法折叠哈希，长输入 3 路并行；用于字符串索引与像素分块指纹。
inline u64 hash64(const void* key, size_t len, u64 seed = 0) {
  using namespace detail;
  constexpr u64 s0 = 0x2d358dccaa6c78a5ull, s1 = 0x8bb84b93962eacc9ull, s2 = 0x4b33a62ed433d4a3ull;
  const u8* p = static_cast<const u8*>(key);
  seed ^= mum(seed ^ s0, s1);
  u64 a, b;
  if (len <= 16) {
    if (len >= 4) {
      if (len >= 8) {
        a = r64(p);
        b = r64(p + len - 8);
      } else {
        a = r32(p);
        b = r32(p + len - 4);
      }
    } else if (len > 0) {
      a = (static_cast<u64>(p[0]) << 45) | p[len - 1];
      b = p[len >> 1];
    } else {
      a = b = 0;
    }
  } else {
    size_t i = len;
    if (i > 48) {
      u64 see1 = seed, see2 = seed;
      do {
        seed = mum(r64(p) ^ s0, r64(p + 8) ^ seed);
        see1 = mum(r64(p + 16) ^ s1, r64(p + 24) ^ see1);
        see2 = mum(r64(p + 32) ^ s2, r64(p + 40) ^ see2);
        p += 48;
        i -= 48;
      } while (i > 48);
      seed ^= see1 ^ see2;
    }
    while (i > 16) {
      seed = mum(r64(p) ^ s2, r64(p + 8) ^ seed);
      i -= 16;
      p += 16;
    }
    a = r64(p + i - 16);
    b = r64(p + i - 8);
  }
  a ^= s1;
  b ^= seed;
  const __uint128_t r = static_cast<__uint128_t>(a) * b;
  return mum(static_cast<u64>(r) ^ s0 ^ len, static_cast<u64>(r >> 64) ^ s1);
}

inline u64 hash64(std::string_view s, u64 seed = 0) { return hash64(s.data(), s.size(), seed); }
inline u64 hash_combine(u64 a, u64 b) { return detail::mum(a ^ 0x9e3779b97f4a7c15ull, b ^ 0xd6e8feb86659fd93ull); }

}  // namespace dx
