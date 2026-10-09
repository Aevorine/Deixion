#include <windows.h>
#include <shellapi.h>

#include "app/app.hpp"
#include "app/claude.hpp"
#include "core/base/paths.hpp"
#include "core/base/text.hpp"

namespace {

struct Args {
  bool tray{false};  // 只驻留托盘，不弹窗口（开机自启用）
  bool quit{false};  // 通知正在运行的实例退出（安装程序用）
  bool connect{false};     // 无界面：安装 skill 并注册 MCP（安装程序用）
  bool disconnect{false};  // 无界面：移除 skill 与 MCP 注册（卸载程序用）
};

Args parse_args() {
  Args a;
  int n = 0;
  LPWSTR* v = CommandLineToArgvW(GetCommandLineW(), &n);
  for (int i = 1; v && i < n; ++i) {
    const std::wstring s = v[i];
    if (s == L"--tray") a.tray = true;
    else if (s == L"--quit") a.quit = true;
    else if (s == L"--connect-claude") a.connect = true;
    else if (s == L"--disconnect-claude") a.disconnect = true;
  }
  if (v) LocalFree(v);
  return a;
}

// 已有实例时找到它的窗口（刚启动的实例可能还没建窗口，最多等 3 秒）。
HWND find_running() {
  for (int i = 0; i < 30; ++i) {
    if (HWND h = FindWindowW(dx::app::kHostClass, nullptr)) return h;
    Sleep(100);
  }
  return nullptr;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const Args args = parse_args();
  if (args.connect || args.disconnect) return (args.connect ? dx::app::claude::install() : dx::app::claude::remove()) ? 0 : 1;
  const UINT msg_show = RegisterWindowMessageW(L"Deixion.Show");
  const UINT msg_quit = RegisterWindowMessageW(L"Deixion.Quit");

  // 单实例：互斥量名带上 exe 路径哈希无必要——同一用户同时只允许一个 Deixion，便携版与安装版也不并存。
  HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\Deixion.SingleInstance");
  if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
    if (HWND h = find_running()) {
      if (args.quit) PostMessageW(h, msg_quit, 0, 0);
      else if (!args.tray) PostMessageW(h, msg_show, 0, 0);
    }
    CloseHandle(mtx);
    return 0;
  }
  if (args.quit) {
    if (mtx) CloseHandle(mtx);
    return 0;
  }

  const int rc = dx::app::App::get().run(hi, args.tray);
  if (mtx) {
    ReleaseMutex(mtx);
    CloseHandle(mtx);
  }
  return rc;
}
