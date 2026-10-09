#pragma once
#include <string>
#include <string_view>

#include "core/base/types.hpp"

namespace dx::text {

std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view w);
std::string lower(std::string_view utf8);
bool icontains(std::string_view hay, std::string_view needle);
bool iequals(std::string_view a, std::string_view b);

// 模糊相似度 [0,1]：完全相同 1，忽略大小写相同 .98，前缀 .9，子串 .8，否则二元组 Dice 系数 ×.7。
double similarity(std::string_view query, std::string_view candidate);

std::string hex(const void* p, size_t n);
std::string base64(const void* p, size_t n);
std::string format_bytes(u64 n);

}  // namespace dx::text
