#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace dxsetup::sys {

namespace fs = std::filesystem;

void log(const std::wstring& line);
void set_log_file(const fs::path& p);

fs::path known_folder(int csidl);
fs::path local_appdata();
fs::path home();
fs::path default_base();
// 无论用户选了哪里，最终目录名一定是 Deixion。
fs::path normalize_dir(fs::path chosen);

bool app_running();
// 让运行中的 Deixion 退出；超时仍不走就结束进程。返回是否已退出。
bool stop_app(unsigned wait_ms = 10000);
// 启动并等待；timeout 内未结束返回 -1。
int run_wait(const std::wstring& cmdline, unsigned timeout_ms, const fs::path& cwd = {});
void spawn(const std::wstring& cmdline, const fs::path& cwd);

bool webview2_present();
// 缺运行库时下载微软的引导程序，验证签名后静默安装。
bool ensure_webview2(std::wstring* detail);

bool make_shortcut(const fs::path& link, const fs::path& target, const std::wstring& args, const fs::path& workdir);

struct UninstallInfo {
  fs::path dir;
  std::wstring version;
  uint64_t size_kb{0};
};
bool write_uninstall_entry(const UninstallInfo& i);
void remove_uninstall_entry();
fs::path installed_dir();

bool run_key_present();
void set_run_key(const fs::path& exe);
void clear_run_key();

fs::path start_menu_link();
fs::path desktop_link();

}  // namespace dxsetup::sys
