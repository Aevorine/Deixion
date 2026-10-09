#pragma once
#include <cstddef>
#include <cstdint>

namespace dxsetup {

// RFC 1951 raw deflate 解码。dst 的容量必须恰好等于解压后大小；成功返回 true。
bool inflate_raw(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_len);

uint32_t crc32(const uint8_t* p, size_t n, uint32_t seed = 0);

}  // namespace dxsetup
