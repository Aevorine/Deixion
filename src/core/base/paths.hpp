#pragma once
#include <filesystem>

#include "core/base/types.hpp"

namespace dx::paths {

namespace fs = std::filesystem;

const fs::path& exe_path();
const fs::path& exe_dir();
bool portable();
// 数据根目录：便携模式（exe 旁有 portable.flag）用 <exe>\data，否则 %LOCALAPPDATA%\Deixion。
const fs::path& data_dir();
fs::path logs_dir();
fs::path store_dir();
fs::path cache_dir();
fs::path update_dir();
void ensure_dirs();

}  // namespace dx::paths
