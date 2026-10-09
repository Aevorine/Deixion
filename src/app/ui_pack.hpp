#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>

#include "core/base/types.hpp"

namespace dx::app {

// 界面文件包：正式版从 exe 内嵌资源读取；开发时设环境变量 DEIXION_UI_DIR 指向 ui/ 目录，直接读磁盘，改完刷新即可看到。
class UiPack {
 public:
  bool load();
  bool get(const std::string& path, std::string& mime, const void*& data, size_t& size);
  bool from_disk() const { return !dir_.empty(); }

 private:
  struct Ent {
    size_t off{0}, size{0};
  };
  const u8* base_{nullptr};
  std::unordered_map<std::string, Ent> map_;
  std::filesystem::path dir_;
  std::string scratch_;
};

}  // namespace dx::app
