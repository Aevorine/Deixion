#pragma once
#include <functional>
#include <mutex>
#include <thread>

#include "core/base/json.hpp"

namespace dx::app {

// 自动更新：查 GitHub Releases 最新版 → 下载安装包 → 用发布页的 SHA256SUMS 校验 → 退出当前进程后静默安装并重启。
// 便携版（exe 旁有 portable.flag）不原地替换，只提示发布页地址。
class Updater {
 public:
  static Updater& get();

  void set_notify(std::function<void(const Json&)> fn) { notify_ = std::move(fn); }
  void check(bool user_initiated);
  void download();
  // 校验通过后启动安装程序；调用方随后应退出进程。
  Res<void> apply();
  Json state() const;

  static int compare_versions(const std::string& a, const std::string& b);

 private:
  Updater() = default;
  void publish();
  void set_error(const std::string& m);

  mutable std::mutex mu_;
  std::function<void(const Json&)> notify_;
  std::thread worker_;
  std::string state_{"idle"};  // idle checking current available downloading ready error
  std::string latest_, notes_, page_url_, asset_url_, sums_url_, error_, file_;
  u64 total_{0}, got_{0};
  bool busy_{false};
};

}  // namespace dx::app
