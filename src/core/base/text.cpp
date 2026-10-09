#include "core/base/text.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>

namespace dx::text {

std::wstring widen(std::string_view s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string narrow(std::wstring_view w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::string lower(std::string_view s) {
  bool ascii = true;
  for (unsigned char c : s)
    if (c >= 0x80) {
      ascii = false;
      break;
    }
  if (ascii) {
    std::string r(s);
    for (auto& c : r)
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return r;
  }
  std::wstring w = widen(s);
  CharLowerBuffW(w.data(), static_cast<DWORD>(w.size()));
  return narrow(w);
}

bool icontains(std::string_view hay, std::string_view needle) {
  if (needle.empty()) return true;
  return lower(hay).find(lower(needle)) != std::string::npos;
}

bool iequals(std::string_view a, std::string_view b) { return lower(a) == lower(b); }

namespace {
std::vector<u32> bigrams(std::wstring_view w) {
  std::vector<u32> v;
  if (w.size() < 2) {
    if (!w.empty()) v.push_back(w[0]);
    return v;
  }
  v.reserve(w.size() - 1);
  for (size_t i = 0; i + 1 < w.size(); ++i) v.push_back((static_cast<u32>(w[i]) << 16) | w[i + 1]);
  std::sort(v.begin(), v.end());
  return v;
}
}  // namespace

double similarity(std::string_view query, std::string_view cand) {
  if (query.empty() || cand.empty()) return 0;
  if (query == cand) return 1.0;
  const std::string q = lower(query), c = lower(cand);
  if (q == c) return 0.98;
  if (c.starts_with(q)) return 0.9;
  if (c.find(q) != std::string::npos) return 0.8;
  const auto a = bigrams(widen(q)), b = bigrams(widen(c));
  if (a.empty() || b.empty()) return 0;
  size_t i = 0, j = 0, hit = 0;
  while (i < a.size() && j < b.size()) {
    if (a[i] == b[j]) {
      ++hit;
      ++i;
      ++j;
    } else if (a[i] < b[j]) {
      ++i;
    } else {
      ++j;
    }
  }
  return 0.7 * (2.0 * static_cast<double>(hit)) / static_cast<double>(a.size() + b.size());
}

std::string hex(const void* p, size_t n) {
  static const char* d = "0123456789abcdef";
  std::string s(n * 2, '0');
  const u8* b = static_cast<const u8*>(p);
  for (size_t i = 0; i < n; ++i) {
    s[i * 2] = d[b[i] >> 4];
    s[i * 2 + 1] = d[b[i] & 15];
  }
  return s;
}

std::string base64(const void* p, size_t n) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const u8* b = static_cast<const u8*>(p);
  std::string o;
  o.resize(((n + 2) / 3) * 4);
  char* w = o.data();
  size_t i = 0;
  for (; i + 2 < n; i += 3) {
    const u32 v = (u32(b[i]) << 16) | (u32(b[i + 1]) << 8) | b[i + 2];
    *w++ = t[v >> 18];
    *w++ = t[(v >> 12) & 63];
    *w++ = t[(v >> 6) & 63];
    *w++ = t[v & 63];
  }
  if (i < n) {
    u32 v = u32(b[i]) << 16;
    if (i + 1 < n) v |= u32(b[i + 1]) << 8;
    *w++ = t[v >> 18];
    *w++ = t[(v >> 12) & 63];
    *w++ = i + 1 < n ? t[(v >> 6) & 63] : '=';
    *w++ = '=';
  }
  return o;
}

std::string format_bytes(u64 n) {
  char buf[32];
  if (n < 1024) std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(n));
  else if (n < 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f KB", static_cast<double>(n) / 1024.0);
  else if (n < 1024ull * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(n) / 1048576.0);
  else std::snprintf(buf, sizeof buf, "%.2f GB", static_cast<double>(n) / 1073741824.0);
  return buf;
}

}  // namespace dx::text
