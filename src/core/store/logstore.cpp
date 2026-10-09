#include "core/store/logstore.hpp"

#include <cstring>

#include "core/base/clock.hpp"
#include "core/base/crc32c.hpp"

namespace dx::store {
namespace fs = std::filesystem;
namespace {
constexpr u32 kMagic = 0x474C5844;  // "DXLG"
constexpr u32 kVersion = 1;
constexpr size_t kHdr = 16;
constexpr size_t kRecHdr = 16;
constexpr u32 kMaxPayload = 8u * 1024 * 1024;

void put32(u8* p, u32 v) { std::memcpy(p, &v, 4); }
void put16(u8* p, u16 v) { std::memcpy(p, &v, 2); }
void put64(u8* p, u64 v) { std::memcpy(p, &v, 8); }
u32 get32(const u8* p) {
  u32 v;
  std::memcpy(&v, p, 4);
  return v;
}
u16 get16(const u8* p) {
  u16 v;
  std::memcpy(&v, p, 2);
  return v;
}
u64 get64(const u8* p) {
  u64 v;
  std::memcpy(&v, p, 8);
  return v;
}

std::vector<u8> frame(u16 type, std::span<const u8> payload, u64 ts) {
  std::vector<u8> b(kRecHdr + payload.size() + 4);
  put32(b.data(), static_cast<u32>(payload.size()));
  put16(b.data() + 4, type);
  put16(b.data() + 6, 0);
  put64(b.data() + 8, ts);
  if (!payload.empty()) std::memcpy(b.data() + kRecHdr, payload.data(), payload.size());
  put32(b.data() + kRecHdr + payload.size(), crc32c(b.data(), kRecHdr + payload.size()));
  return b;
}

std::vector<u8> file_header() {
  std::vector<u8> h(kHdr);
  put32(h.data(), kMagic);
  put32(h.data() + 4, kVersion);
  put64(h.data() + 8, unix_ms());
  return h;
}

bool read_all(const fs::path& p, std::vector<u8>& out) {
  HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz{};
  GetFileSizeEx(h, &sz);
  out.resize(static_cast<size_t>(sz.QuadPart));
  size_t off = 0;
  while (off < out.size()) {
    DWORD got = 0;
    if (!ReadFile(h, out.data() + off, static_cast<DWORD>(std::min<size_t>(out.size() - off, 1u << 24)), &got, nullptr) || !got) break;
    off += got;
  }
  out.resize(off);
  CloseHandle(h);
  return true;
}
}  // namespace

Res<void> LogStore::open(const fs::path& file, const Replay& replay) {
  std::lock_guard lk(mu_);
  path_ = file;
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  std::vector<u8> data;
  size_t good = 0;
  bool have = read_all(file, data) && data.size() >= kHdr && get32(data.data()) == kMagic && get32(data.data() + 4) == kVersion;
  if (have) {
    size_t off = kHdr;
    good = kHdr;
    while (off + kRecHdr + 4 <= data.size()) {
      const u32 len = get32(data.data() + off);
      if (len > kMaxPayload || off + kRecHdr + len + 4 > data.size()) break;
      const u32 crc = get32(data.data() + off + kRecHdr + len);
      if (crc != crc32c(data.data() + off, kRecHdr + len)) break;
      if (replay) replay(get16(data.data() + off + 4), get64(data.data() + off + 8), std::span<const u8>(data.data() + off + kRecHdr, len));
      ++records_;
      off += kRecHdr + len + 4;
      good = off;
    }
    dropped_ = data.size() - good;
  }
  h_ = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, have ? OPEN_EXISTING : CREATE_ALWAYS,
                   FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h_ == INVALID_HANDLE_VALUE) return fail(E_IO, "cannot open store file");
  if (have) {
    LARGE_INTEGER pos;
    pos.QuadPart = static_cast<LONGLONG>(good);
    SetFilePointerEx(h_, pos, nullptr, FILE_BEGIN);
    SetEndOfFile(h_);
    bytes_ = good;
  } else {
    const auto hdr = file_header();
    DWORD w = 0;
    WriteFile(h_, hdr.data(), static_cast<DWORD>(hdr.size()), &w, nullptr);
    bytes_ = kHdr;
    records_ = 0;
  }
  return {};
}

void LogStore::close() {
  std::lock_guard lk(mu_);
  if (h_ != INVALID_HANDLE_VALUE) {
    FlushFileBuffers(h_);
    CloseHandle(h_);
    h_ = INVALID_HANDLE_VALUE;
  }
}

Res<void> LogStore::append_locked(u16 type, std::span<const u8> payload) {
  if (h_ == INVALID_HANDLE_VALUE) return fail(E_IO, "store is closed");
  if (payload.size() > kMaxPayload) return fail(E_BAD_ARG, "record too large");
  const auto b = frame(type, payload, unix_ms());
  DWORD w = 0;
  if (!WriteFile(h_, b.data(), static_cast<DWORD>(b.size()), &w, nullptr) || w != b.size()) return fail(E_IO, "store write failed");
  ++records_;
  bytes_ += b.size();
  return {};
}

Res<void> LogStore::append(u16 type, std::span<const u8> payload) {
  std::lock_guard lk(mu_);
  return append_locked(type, payload);
}

Res<void> LogStore::append_json(u16 type, const Json& j) {
  const std::string s = j.dump();
  return append(type, std::span<const u8>(reinterpret_cast<const u8*>(s.data()), s.size()));
}

Res<void> LogStore::rewrite(const std::vector<std::pair<u16, std::string>>& recs) {
  std::lock_guard lk(mu_);
  const fs::path tmp = path_.wstring() + L".tmp";
  HANDLE t = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (t == INVALID_HANDLE_VALUE) return fail(E_IO, "cannot create temp store");
  std::vector<u8> buf = file_header();
  const u64 ts = unix_ms();
  for (const auto& r : recs) {
    const auto f = frame(r.first, std::span<const u8>(reinterpret_cast<const u8*>(r.second.data()), r.second.size()), ts);
    buf.insert(buf.end(), f.begin(), f.end());
  }
  DWORD w = 0;
  const BOOL ok = WriteFile(t, buf.data(), static_cast<DWORD>(buf.size()), &w, nullptr);
  FlushFileBuffers(t);
  CloseHandle(t);
  if (!ok || w != buf.size()) {
    DeleteFileW(tmp.c_str());
    return fail(E_IO, "store compaction write failed");
  }
  if (h_ != INVALID_HANDLE_VALUE) {
    CloseHandle(h_);
    h_ = INVALID_HANDLE_VALUE;
  }
  if (!MoveFileExW(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return fail(E_IO, "store replace failed");
  h_ = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h_ == INVALID_HANDLE_VALUE) return fail(E_IO, "cannot reopen store");
  LARGE_INTEGER end{};
  SetFilePointerEx(h_, end, nullptr, FILE_END);
  records_ = recs.size();
  bytes_ = buf.size();
  return {};
}

void LogStore::sync() {
  std::lock_guard lk(mu_);
  if (h_ != INVALID_HANDLE_VALUE) FlushFileBuffers(h_);
}

}  // namespace dx::store
