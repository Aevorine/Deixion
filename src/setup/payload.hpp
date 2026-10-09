#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dxsetup {

struct PayloadEntry {
  std::wstring rel;  // 相对安装目录的路径，反斜杠分隔
  uint32_t raw{0}, packed{0}, crc{0}, off{0};
};

// 嵌在安装器里的压缩包；表项在加载时就核对过边界，解压时再核对 CRC。
class Payload {
 public:
  bool load();
  const std::vector<PayloadEntry>& entries() const { return entries_; }
  uint64_t raw_total() const;
  bool unpack(const PayloadEntry& e, std::vector<uint8_t>& out) const;

 private:
  const uint8_t* blob_{nullptr};
  size_t blob_len_{0};
  std::vector<PayloadEntry> entries_;
};

}  // namespace dxsetup
