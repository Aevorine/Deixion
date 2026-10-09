#include "app/ui_pack.hpp"

#include <windows.h>

#include <cstring>
#include <fstream>
#include <sstream>

#include "core/base/text.hpp"

namespace dx::app {
namespace {
const char* mime_of(const std::string& p) {
  const size_t d = p.rfind('.');
  const std::string e = d == std::string::npos ? "" : p.substr(d + 1);
  if (e == "html") return "text/html; charset=utf-8";
  if (e == "js" || e == "mjs") return "text/javascript; charset=utf-8";
  if (e == "css") return "text/css; charset=utf-8";
  if (e == "json") return "application/json; charset=utf-8";
  if (e == "svg") return "image/svg+xml";
  if (e == "png") return "image/png";
  if (e == "ico") return "image/x-icon";
  if (e == "woff2") return "font/woff2";
  if (e == "woff") return "font/woff";
  if (e == "txt" || e == "md") return "text/plain; charset=utf-8";
  return "application/octet-stream";
}
}  // namespace

bool UiPack::load() {
  wchar_t env[1024];
  if (GetEnvironmentVariableW(L"DEIXION_UI_DIR", env, 1024) > 0) {
    dir_ = env;
    return std::filesystem::exists(dir_ / L"index.html");
  }
  HRSRC r = FindResourceW(nullptr, L"UIPACK", reinterpret_cast<LPCWSTR>(RT_RCDATA));
  if (!r) return false;
  HGLOBAL g = LoadResource(nullptr, r);
  const DWORD sz = SizeofResource(nullptr, r);
  const u8* p = static_cast<const u8*>(LockResource(g));
  if (!p || sz < 8 || std::memcmp(p, "DXUP", 4) != 0) return false;
  u32 count;
  std::memcpy(&count, p + 4, 4);
  size_t off = 8;
  for (u32 i = 0; i < count; ++i) {
    u16 nl;
    std::memcpy(&nl, p + off, 2);
    off += 2;
    std::string name(reinterpret_cast<const char*>(p + off), nl);
    off += nl;
    u32 o, s;
    std::memcpy(&o, p + off, 4);
    std::memcpy(&s, p + off + 4, 4);
    off += 8;
    map_[name] = {o, s};
  }
  base_ = p;
  return true;
}

bool UiPack::get(const std::string& path, std::string& mime, const void*& data, size_t& size) {
  mime = mime_of(path);
  if (!dir_.empty()) {
    std::filesystem::path rel = text::widen(path.substr(1));
    if (rel.is_absolute() || path.find("..") != std::string::npos) return false;
    std::ifstream f(dir_ / rel, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    scratch_ = ss.str();
    data = scratch_.data();
    size = scratch_.size();
    return true;
  }
  auto it = map_.find(path);
  if (it == map_.end()) return false;
  data = base_ + it->second.off;
  size = it->second.size;
  return true;
}

}  // namespace dx::app
