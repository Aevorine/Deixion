#pragma once
#include <windows.h>

#include <filesystem>
#include <functional>
#include <mutex>

#include "core/base/json.hpp"

namespace dx::store {

// 追加式记录日志：每条记录 [len u32][type u16][rsv u16][ts u64][payload][crc32c u32]。
// 打开时逐条校验，遇到第一条损坏/截断的记录就把文件截到那里（崩溃/断电留下的“撕裂尾巴”），之前的全部保留。
// 压缩用“写临时文件 → FlushFileBuffers → 原子替换”，任何时刻磁盘上都是完整可读的一份。
class LogStore {
 public:
  using Replay = std::function<void(u16 type, u64 ts_ms, std::span<const u8> payload)>;

  ~LogStore() { close(); }

  Res<void> open(const std::filesystem::path& file, const Replay& replay);
  void close();
  Res<void> append(u16 type, std::span<const u8> payload);
  Res<void> append_json(u16 type, const Json& j);
  Res<void> rewrite(const std::vector<std::pair<u16, std::string>>& records);
  void sync();

  u64 records() const { return records_; }
  u64 bytes() const { return bytes_; }
  u64 dropped_bytes() const { return dropped_; }
  const std::filesystem::path& path() const { return path_; }

 private:
  Res<void> append_locked(u16 type, std::span<const u8> payload);
  std::filesystem::path path_;
  HANDLE h_{INVALID_HANDLE_VALUE};
  std::mutex mu_;
  u64 records_{0}, bytes_{0}, dropped_{0};
};

}  // namespace dx::store
