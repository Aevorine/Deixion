#include "payload.hpp"

#include <windows.h>

#include <cstring>

#include "inflate.hpp"

namespace dxsetup {
namespace {

uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

// 相对路径必须干净：不许绝对路径、盘符、`..`，否则恶意包能写到安装目录之外。
bool safe_rel(const std::wstring& s) {
  if (s.empty() || s.size() > 240 || s[0] == L'\\' || s.find(L':') != std::wstring::npos) return false;
  size_t i = 0;
  while (i <= s.size()) {
    size_t j = s.find(L'\\', i);
    if (j == std::wstring::npos) j = s.size();
    const std::wstring seg = s.substr(i, j - i);
    if (seg.empty() || seg == L"." || seg == L"..") return false;
    i = j + 1;
  }
  return true;
}

}  // namespace

bool Payload::load() {
  HRSRC r = FindResourceW(nullptr, L"PAYLOAD", RT_RCDATA);
  if (!r) return false;
  HGLOBAL g = LoadResource(nullptr, r);
  const DWORD len = SizeofResource(nullptr, r);
  const auto* p = static_cast<const uint8_t*>(g ? LockResource(g) : nullptr);
  if (!p || len < 12 || std::memcmp(p, "DXPL", 4) != 0 || rd32(p + 4) != 1) return false;
  const uint32_t n = rd32(p + 8);
  size_t pos = 12;
  std::vector<PayloadEntry> es;
  for (uint32_t i = 0; i < n; ++i) {
    if (pos + 2 > len) return false;
    const size_t nl = p[pos] | (p[pos + 1] << 8);
    pos += 2;
    if (pos + nl + 16 > len) return false;
    PayloadEntry e;
    const int wl = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(p + pos), static_cast<int>(nl), nullptr, 0);
    e.rel.resize(wl > 0 ? wl : 0);
    if (wl > 0) MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(p + pos), static_cast<int>(nl), e.rel.data(), wl);
    for (auto& c : e.rel) if (c == L'/') c = L'\\';
    pos += nl;
    e.raw = rd32(p + pos);
    e.packed = rd32(p + pos + 4);
    e.crc = rd32(p + pos + 8);
    e.off = rd32(p + pos + 12);
    pos += 16;
    if (!safe_rel(e.rel) || e.raw > (1u << 30) || e.packed > e.raw) return false;
    es.push_back(std::move(e));
  }
  const size_t base = pos;
  for (const auto& e : es)
    if (static_cast<uint64_t>(base) + e.off + e.packed > len) return false;
  blob_ = p + base;
  blob_len_ = len - base;
  entries_ = std::move(es);
  return true;
}

uint64_t Payload::raw_total() const {
  uint64_t t = 0;
  for (const auto& e : entries_) t += e.raw;
  return t;
}

bool Payload::unpack(const PayloadEntry& e, std::vector<uint8_t>& out) const {
  if (static_cast<uint64_t>(e.off) + e.packed > blob_len_) return false;
  out.assign(e.raw, 0);
  const uint8_t* src = blob_ + e.off;
  if (e.packed == e.raw) {
    if (e.raw) std::memcpy(out.data(), src, e.raw);
  } else if (!inflate_raw(src, e.packed, out.data(), e.raw)) {
    return false;
  }
  return crc32(out.data(), out.size()) == e.crc;
}

}  // namespace dxsetup
