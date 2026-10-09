#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <format>

#include "installer.hpp"
#include "sysops.hpp"
#include "ui.hpp"

#ifndef DX_VERSION
#define DX_VERSION "0"
#endif
#define DX_W2(x) L##x
#define DX_W(x) DX_W2(x)
#define DX_VERSION_W DX_W(DX_VERSION)

namespace {

using namespace dxsetup;

struct Args {
  bool silent{false}, update{false}, relaunch{false}, uninstall{false}, purge{false};
  int start_menu{-1}, desktop{-1}, autostart{-1}, claude{-1};  // -1 = 未指定，取默认或沿用现状
  fs::path dir, from_temp, log;
};

bool starts(const std::wstring& s, const wchar_t* p) { return _wcsnicmp(s.c_str(), p, wcslen(p)) == 0; }
bool eq(const std::wstring& s, const wchar_t* p) { return _wcsicmp(s.c_str(), p) == 0; }

Args parse() {
  Args a;
  int n = 0;
  LPWSTR* v = CommandLineToArgvW(GetCommandLineW(), &n);
  for (int i = 1; v && i < n; ++i) {
    std::wstring s = v[i];
    auto flag = [&](const wchar_t* name, int& out) {
      const std::wstring k = std::wstring(name) + L"=";
      if (!starts(s, k.c_str())) return false;
      out = s.size() > k.size() && s[k.size()] == L'1' ? 1 : 0;
      return true;
    };
    if (eq(s, L"/S")) a.silent = true;
    else if (eq(s, L"/UPDATE")) a.update = true;
    else if (eq(s, L"/RELAUNCH")) a.relaunch = true;
    else if (eq(s, L"/UNINSTALL")) a.uninstall = true;
    else if (eq(s, L"/PURGE")) a.purge = true;
    else if (flag(L"/AUTOSTART", a.autostart) || flag(L"/DESKTOP", a.desktop) || flag(L"/STARTMENU", a.start_menu) || flag(L"/CLAUDE", a.claude)) {}
    else if (starts(s, L"/LOG=")) a.log = s.substr(5);
    else if (starts(s, L"/FROMTEMP=")) a.from_temp = s.substr(10);
    else if (starts(s, L"/D=")) {
      // 路径里未加引号的空格会被拆成多个参数：只续接不以 '/' 开头的片段，所以 /D= 放在哪都行。
      std::wstring d = s.substr(3);
      while (i + 1 < n && v[i + 1][0] != L'/') d += L" " + std::wstring(v[++i]);
      a.dir = d;
    }
  }
  if (v) LocalFree(v);
  return a;
}

fs::path self() {
  wchar_t b[MAX_PATH * 2];
  const DWORD n = GetModuleFileNameW(nullptr, b, static_cast<DWORD>(std::size(b)));
  return fs::path(std::wstring(b, n));
}

bool same_dir(const fs::path& a, const fs::path& b) {
  std::error_code ec;
  return fs::equivalent(a, b, ec);
}

int exit_for(Fail f) {
  switch (f) {
    case Fail::Payload: return 3;
    case Fail::Dir: return 4;
    case Fail::Write: return 5;
    case Fail::Busy: return 6;
    default: return 1;
  }
}

// 目录里只剩 uninstall.exe 时连同目录一起清掉；随后让临时副本自删。
void finish_uninstall(const fs::path& dir, const fs::path& temp_self) {
  std::error_code ec;
  if (_wcsicmp(dir.filename().c_str(), L"Deixion") == 0) {
    fs::remove(dir / L"uninstall.exe", ec);
    fs::remove(dir, ec);
  }
  if (!temp_self.empty()) {
    sys::spawn(L"cmd.exe /d /c ping -n 3 127.0.0.1 >nul & del /f /q \"" + temp_self.wstring() + L"\"", {});
  }
}

int do_uninstall(HINSTANCE hi, const Args& a) {
  fs::path dir = !a.from_temp.empty() ? a.from_temp : sys::installed_dir();
  if (dir.empty()) dir = self().parent_path();
  dir = sys::normalize_dir(dir);

  // 运行的是安装目录里的 uninstall.exe：先复制到临时目录再从那里卸载，才能删掉自己所在的目录。
  if (a.from_temp.empty() && same_dir(self().parent_path(), dir)) {
    wchar_t t[MAX_PATH];
    GetTempPathW(MAX_PATH, t);
    const fs::path tmp = fs::path(t) / std::format(L"Deixion-uninstall-{}.exe", GetTickCount64());
    if (!CopyFileW(self().c_str(), tmp.c_str(), FALSE)) return 5;
    std::wstring cmd = L"\"" + tmp.wstring() + L"\" /UNINSTALL /FROMTEMP=\"" + dir.wstring() + L"\"";
    if (a.silent) cmd += L" /S";
    if (a.purge) cmd += L" /PURGE";
    sys::spawn(cmd, {});
    return 0;
  }

  Options o;
  o.dir = dir;
  o.purge = a.purge;
  int rc = 0;
  if (a.silent) {
    rc = uninstall(o, nullptr).ok ? 0 : 1;
  } else {
    rc = run_ui(hi, Mode::Uninstall, nullptr, o, true);
  }
  finish_uninstall(dir, a.from_temp.empty() ? fs::path() : self());
  return rc;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const Args a = parse();
  wchar_t t[MAX_PATH];
  GetTempPathW(MAX_PATH, t);
  sys::set_log_file(a.log.empty() ? fs::path(t) / L"Deixion-setup.log" : a.log);
  sys::log(std::format(L"setup {} start: silent={} update={} uninstall={}", DX_VERSION_W, a.silent, a.update, a.uninstall));

  if (a.uninstall) return do_uninstall(hi, a);

  Payload pl;
  if (!pl.load()) {
    sys::log(L"payload missing or invalid");
    if (!a.silent) MessageBoxW(nullptr, use_chinese() ? L"安装包已损坏" : L"Package is damaged", L"Deixion", MB_OK | MB_ICONERROR);
    return 3;
  }

  const fs::path before = sys::installed_dir();
  Options o;
  o.update = a.update;
  o.relaunch = a.relaunch;
  if (!a.dir.empty()) o.dir = sys::normalize_dir(a.dir);
  else if (!before.empty()) o.dir = sys::normalize_dir(before);
  else o.dir = sys::default_base() / L"Deixion";
  // 更新沿用现状；全新安装用默认值。
  const bool keep = a.update || !before.empty();
  o.start_menu = a.start_menu >= 0 ? a.start_menu : (keep ? fs::exists(sys::start_menu_link()) : true);
  o.desktop = a.desktop >= 0 ? a.desktop : (keep ? fs::exists(sys::desktop_link()) : false);
  o.autostart = a.autostart >= 0 ? a.autostart : sys::run_key_present();
  // 接入 Claude Code：全新安装默认接入；更新只在以前接入过（skill 已在）时刷新，不替用户重新接入。
  o.claude = a.claude >= 0 ? a.claude : (keep ? fs::exists(sys::home() / L".claude" / L"skills" / L"deixion") : true);

  if (a.silent) {
    const Result r = install(pl, o, nullptr);
    sys::log(std::format(L"silent install {} (why={}) note={}", r.ok ? L"ok" : L"failed", static_cast<int>(r.why), r.note));
    return r.ok ? 0 : exit_for(r.why);
  }
  return run_ui(hi, Mode::Install, &pl, o, !before.empty());
}
