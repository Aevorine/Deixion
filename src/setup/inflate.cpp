#include "inflate.hpp"

#include <cstring>

namespace dxsetup {
namespace {

struct Tree {
  uint16_t count[16];
  uint16_t sym[288];
};

struct State {
  const uint8_t* src;
  size_t len, pos{0};
  uint32_t tag{0};
  int bits{0};
  uint8_t* dst;
  size_t dlen, dpos{0};
  bool err{false};
};

constexpr uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
constexpr uint8_t kClOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

int getbit(State& s) {
  if (!s.bits--) {
    if (s.pos >= s.len) {
      s.err = true;
      return 0;
    }
    s.tag = s.src[s.pos++];
    s.bits = 7;
  }
  const int b = static_cast<int>(s.tag & 1);
  s.tag >>= 1;
  return b;
}

uint32_t read_bits(State& s, int n) {
  uint32_t v = 0;
  for (int i = 0; i < n; ++i) v |= static_cast<uint32_t>(getbit(s)) << i;
  return v;
}

void build(Tree& t, const uint8_t* lengths, int n) {
  std::memset(t.count, 0, sizeof t.count);
  for (int i = 0; i < n; ++i) ++t.count[lengths[i]];
  t.count[0] = 0;
  uint16_t offs[16];
  uint16_t sum = 0;
  for (int i = 0; i < 16; ++i) {
    offs[i] = sum;
    sum = static_cast<uint16_t>(sum + t.count[i]);
  }
  for (int i = 0; i < n; ++i)
    if (lengths[i]) t.sym[offs[lengths[i]]++] = static_cast<uint16_t>(i);
}

int decode(State& s, const Tree& t) {
  int sum = 0, cur = 0, len = 0;
  do {
    cur = 2 * cur + getbit(s);
    if (++len > 15 || s.err) {
      s.err = true;
      return 0;
    }
    sum += t.count[len];
    cur -= t.count[len];
  } while (cur >= 0);
  return t.sym[sum + cur];
}

bool block(State& s, const Tree& lt, const Tree& dt) {
  for (;;) {
    const int sym = decode(s, lt);
    if (s.err) return false;
    if (sym == 256) return true;
    if (sym < 256) {
      if (s.dpos >= s.dlen) return false;
      s.dst[s.dpos++] = static_cast<uint8_t>(sym);
      continue;
    }
    const int li = sym - 257;
    if (li >= 29) return false;
    const size_t length = kLenBase[li] + read_bits(s, kLenExtra[li]);
    const int di = decode(s, dt);
    if (s.err || di >= 30) return false;
    const size_t dist = kDistBase[di] + read_bits(s, kDistExtra[di]);
    if (dist > s.dpos || s.dpos + length > s.dlen) return false;
    for (size_t i = 0; i < length; ++i, ++s.dpos) s.dst[s.dpos] = s.dst[s.dpos - dist];
  }
}

}  // namespace

bool inflate_raw(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_len) {
  State s{};
  s.src = src;
  s.len = src_len;
  s.dst = dst;
  s.dlen = dst_len;
  Tree lt, dt;
  int final_block;
  do {
    final_block = getbit(s);
    const uint32_t type = read_bits(s, 2);
    if (s.err) return false;
    if (type == 0) {
      s.bits = 0;  // 丢弃当前字节剩余位
      if (s.pos + 4 > s.len) return false;
      const uint32_t n = s.src[s.pos] | (s.src[s.pos + 1] << 8), nn = s.src[s.pos + 2] | (s.src[s.pos + 3] << 8);
      if ((n ^ 0xFFFF) != nn) return false;
      s.pos += 4;
      if (s.pos + n > s.len || s.dpos + n > s.dlen) return false;
      std::memcpy(s.dst + s.dpos, s.src + s.pos, n);
      s.pos += n;
      s.dpos += n;
    } else if (type == 1) {
      uint8_t l[288];
      for (int i = 0; i < 144; ++i) l[i] = 8;
      for (int i = 144; i < 256; ++i) l[i] = 9;
      for (int i = 256; i < 280; ++i) l[i] = 7;
      for (int i = 280; i < 288; ++i) l[i] = 8;
      build(lt, l, 288);
      uint8_t d[30];
      for (auto& v : d) v = 5;
      build(dt, d, 30);
      if (!block(s, lt, dt)) return false;
    } else if (type == 2) {
      const int hlit = static_cast<int>(read_bits(s, 5)) + 257, hdist = static_cast<int>(read_bits(s, 5)) + 1, hclen = static_cast<int>(read_bits(s, 4)) + 4;
      uint8_t lengths[320] = {};
      for (int i = 0; i < hclen; ++i) lengths[kClOrder[i]] = static_cast<uint8_t>(read_bits(s, 3));
      Tree ct;
      build(ct, lengths, 19);
      uint8_t ll[320] = {};
      int num = 0;
      while (num < hlit + hdist) {
        const int sym = decode(s, ct);
        if (s.err) return false;
        int rep = 0;
        uint8_t val = 0;
        if (sym < 16) {
          ll[num++] = static_cast<uint8_t>(sym);
          continue;
        }
        if (sym == 16) {
          if (!num) return false;
          val = ll[num - 1];
          rep = 3 + static_cast<int>(read_bits(s, 2));
        } else if (sym == 17) {
          rep = 3 + static_cast<int>(read_bits(s, 3));
        } else {
          rep = 11 + static_cast<int>(read_bits(s, 7));
        }
        if (num + rep > hlit + hdist) return false;
        while (rep--) ll[num++] = val;
      }
      build(lt, ll, hlit);
      build(dt, ll + hlit, hdist);
      if (!block(s, lt, dt)) return false;
    } else {
      return false;
    }
    if (s.err) return false;
  } while (!final_block);
  return s.dpos == s.dlen;
}

uint32_t crc32(const uint8_t* p, size_t n, uint32_t seed) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    init = true;
  }
  uint32_t c = ~seed;
  for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return ~c;
}

}  // namespace dxsetup
