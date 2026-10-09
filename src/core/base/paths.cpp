#include "core/base/paths.hpp"

#include <shlobj.h>
#include <windows.h>

namespace dx::paths {

const fs::path& exe_path() {
  static const fs::path p = [] {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(std::wstring(buf, n));
  }();
  return p;
}

const fs::path& exe_dir() {
  static const fs::path p = exe_path().parent_path();
  return p;
}

bool portable() {
  static const bool v = fs::exists(exe_dir() / L"portable.flag");
  return v;
}

const fs::path& data_dir() {
  static const fs::path p = [] {
    if (portable()) return exe_dir() / L"data";
    PWSTR w = nullptr;
    fs::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &w))) {
      base = w;
      CoTaskMemFree(w);
    } else {
      base = exe_dir();
    }
    return base / L"Deixion";
  }();
  return p;
}

fs::path logs_dir() { return data_dir() / L"logs"; }
fs::path store_dir() { return data_dir() / L"store"; }
fs::path cache_dir() { return data_dir() / L"cache"; }
fs::path update_dir() { return data_dir() / L"update"; }

void ensure_dirs() {
  std::error_code ec;
  for (const auto& d : {logs_dir(), store_dir(), cache_dir(), update_dir()}) fs::create_directories(d, ec);
}

}  // namespace dx::paths
