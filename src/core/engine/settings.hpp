#pragma once
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "core/base/json.hpp"

namespace dx::eng {

struct Settings {
  std::string mode{"background"};
  bool allow_hop{false};
  bool allow_shell_launch{false};
  bool launch_strict{false};                // 开启后 launch 只能启动 launch_allow 清单里的程序
  std::vector<std::string> launch_allow;    // 程序名（如 notepad.exe）或完整路径
  std::string speed{"fast"};
  bool overlay{true};
  std::string verify{"auto"};
  int jpeg_quality{78};
  int max_image_dim{1568};
  bool grid_default{true};
  std::string log_level{"info"};
  bool autostart{false};
  bool check_updates{true};
  bool close_to_tray{true};
  bool paused{false};
  std::string theme{"auto"};
  std::string density{"standard"};
  Json hotkeys;

  Json to_json() const;
  static Settings from_json(const Json& j);
  bool foreground() const { return mode == "foreground"; }
};

// 配置原子落盘（临时文件 → 替换，上一份留作 .bak）；读取失败自动回退到 .bak，再失败用默认值。
class SettingsStore {
 public:
  static SettingsStore& get();
  void load(const std::filesystem::path& file);
  Settings snapshot() const;
  Res<Json> update(const Json& patch);
  u64 subscribe(std::function<void(const Settings&)> fn);
  void unsubscribe(u64 token);
  Json defaults_json() const;

 private:
  SettingsStore();
  Res<void> save_locked();
  mutable std::mutex mu_;
  Settings s_;
  std::filesystem::path file_;
  std::vector<std::pair<u64, std::function<void(const Settings&)>>> subs_;
  u64 next_{1};
};

}  // namespace dx::eng
