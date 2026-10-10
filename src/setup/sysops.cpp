#define _CRT_SECURE_NO_WARNINGS
#include "sysops.hpp"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <softpub.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <wintrust.h>

#include <cstdint>
#include <cstdio>
#include <format>
#include <vector>

#ifndef DX_VERSION
#define DX_VERSION "0"
#endif

namespace dxsetup::sys {
namespace {

fs::path g_log;
constexpr wchar_t kUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Deixion";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kMutex[] = L"Local\\Deixion.SingleInstance";
constexpr wchar_t kWv2Guid[] = L"{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";

bool set_str(HKEY k, const wchar_t* name, const std::wstring& v) {
  return RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()), static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

std::wstring get_str(HKEY root, const wchar_t* sub, const wchar_t* name) {
  wchar_t buf[2048];
  DWORD n = sizeof buf;
  if (RegGetValueW(root, sub, name, RRF_RT_REG_SZ, nullptr, buf, &n) != ERROR_SUCCESS) return {};
  return buf;
}

// 只信微软签名的引导程序：先验证 Authenticode，再核对签名人。
bool signed_by_microsoft(const wchar_t* file) {
  WINTRUST_FILE_INFO fi{sizeof fi};
  fi.pcwszFilePath = file;
  WINTRUST_DATA wd{sizeof wd};
  wd.dwUIChoice = WTD_UI_NONE;
  wd.fdwRevocationChecks = WTD_REVOKE_NONE;
  wd.dwUnionChoice = WTD_CHOICE_FILE;
  wd.pFile = &fi;
  wd.dwStateAction = WTD_STATEACTION_VERIFY;
  GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  const LONG st = WinVerifyTrust(nullptr, &action, &wd);
  wd.dwStateAction = WTD_STATEACTION_CLOSE;
  WinVerifyTrust(nullptr, &action, &wd);
  if (st != 0) return false;

  HCERTSTORE store = nullptr;
  HCRYPTMSG msg = nullptr;
  if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, file, CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY, 0, nullptr, nullptr, nullptr, &store, &msg, nullptr)) return false;
  bool ok = false;
  DWORD sz = 0;
  if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &sz)) {
    std::vector<BYTE> buf(sz);
    if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, buf.data(), &sz)) {
      auto* si = reinterpret_cast<CMSG_SIGNER_INFO*>(buf.data());
      CERT_INFO ci{};
      ci.Issuer = si->Issuer;
      ci.SerialNumber = si->SerialNumber;
      if (PCCERT_CONTEXT cc = CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_CERT, &ci, nullptr)) {
        wchar_t name[256]{};
        CertGetNameStringW(cc, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 256);
        ok = wcscmp(name, L"Microsoft Corporation") == 0;
        CertFreeCertificateContext(cc);
      }
    }
  }
  if (store) CertCloseStore(store, 0);
  if (msg) CryptMsgClose(msg);
  return ok;
}

bool download(const wchar_t* host, const wchar_t* path, const fs::path& out) {
  HINTERNET s = WinHttpOpen(L"Deixion-Setup/" L"1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
  if (!s) return false;
  WinHttpSetTimeouts(s, 8000, 8000, 15000, 30000);
  bool ok = false;
  if (HINTERNET c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0)) {
    if (HINTERNET r = WinHttpOpenRequest(c, L"GET", path, nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE)) {
      if (WinHttpSendRequest(r, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
        DWORD code = 0, n = sizeof code;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &n, nullptr);
        if (code == 200) {
          if (FILE* f = _wfopen(out.c_str(), L"wb")) {
            std::vector<char> buf(64 * 1024);
            DWORD got = 0;
            size_t total = 0;
            ok = true;
            while (WinHttpReadData(r, buf.data(), static_cast<DWORD>(buf.size()), &got) && got) {
              if (fwrite(buf.data(), 1, got, f) != got || (total += got) > (64u << 20)) { ok = false; break; }
            }
            fclose(f);
            ok = ok && total > 0;
          }
        }
      }
      WinHttpCloseHandle(r);
    }
    WinHttpCloseHandle(c);
  }
  WinHttpCloseHandle(s);
  return ok;
}

}  // namespace

void set_log_file(const fs::path& p) { g_log = p; }

void log(const std::wstring& line) {
  if (g_log.empty()) return;
  SYSTEMTIME t;
  GetLocalTime(&t);
  if (FILE* f = _wfopen(g_log.c_str(), L"ab")) {
    std::wstring s = std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} {}\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, line);
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string u(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), u.data(), n, nullptr, nullptr);
    fwrite(u.data(), 1, u.size(), f);
    fclose(f);
  }
}

fs::path known_folder(int csidl) {
  wchar_t b[MAX_PATH]{};
  return SUCCEEDED(SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, b)) ? fs::path(b) : fs::path();
}
fs::path local_appdata() { return known_folder(CSIDL_LOCAL_APPDATA); }
fs::path home() { return known_folder(CSIDL_PROFILE); }
fs::path default_base() { return local_appdata() / L"Programs"; }

fs::path normalize_dir(fs::path chosen) {
  while (!chosen.empty() && (chosen.native().back() == L'\\' || chosen.native().back() == L'/')) {
    std::wstring s = chosen.native();
    if (s.size() <= 3) break;
    s.pop_back();
    chosen = s;
  }
  const std::wstring leaf = chosen.filename().wstring();
  if (_wcsicmp(leaf.c_str(), L"Deixion") == 0) return chosen;
  return chosen / L"Deixion";
}

bool app_running() {
  if (HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, kMutex)) {
    CloseHandle(h);
    return true;
  }
  return false;
}

bool stop_app(unsigned wait_ms) {
  if (!app_running()) return true;
  if (HWND h = FindWindowW(L"DeixionHost", nullptr)) PostMessageW(h, RegisterWindowMessageW(L"Deixion.Quit"), 0, 0);
  const ULONGLONG end = GetTickCount64() + wait_ms;
  while (app_running() && GetTickCount64() < end) Sleep(50);
  if (!app_running()) return true;
  log(L"graceful quit timed out, terminating");
  PROCESSENTRY32W pe{sizeof pe};
  if (HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)) {
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
      if (_wcsicmp(pe.szExeFile, L"Deixion.exe") != 0) continue;
      if (HANDLE p = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID)) {
        TerminateProcess(p, 0);
        WaitForSingleObject(p, 3000);
        CloseHandle(p);
      }
    }
    CloseHandle(snap);
  }
  Sleep(200);
  return !app_running();
}

int run_wait(const std::wstring& cmdline, unsigned timeout_ms, const fs::path& cwd) {
  STARTUPINFOW si{sizeof si};
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  std::wstring c = cmdline;
  if (!CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) return -2;
  DWORD code = static_cast<DWORD>(-1);
  if (WaitForSingleObject(pi.hProcess, timeout_ms) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
  else TerminateProcess(pi.hProcess, 1);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return static_cast<int>(code);
}

void spawn(const std::wstring& cmdline, const fs::path& cwd) {
  STARTUPINFOW si{sizeof si};
  PROCESS_INFORMATION pi{};
  std::wstring c = cmdline;
  if (CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
}

bool webview2_present() {
  const std::wstring sub = std::wstring(L"Software\\Microsoft\\EdgeUpdate\\Clients\\") + kWv2Guid;
  const std::wstring sub32 = std::wstring(L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\") + kWv2Guid;
  for (auto [root, key] : {std::pair{HKEY_LOCAL_MACHINE, sub32}, std::pair{HKEY_LOCAL_MACHINE, sub}, std::pair{HKEY_CURRENT_USER, sub}}) {
    const std::wstring pv = get_str(root, key.c_str(), L"pv");
    if (!pv.empty() && pv != L"0.0.0.0") return true;
  }
  return false;
}

bool ensure_webview2(std::wstring* detail) {
  if (webview2_present()) return true;
  log(L"WebView2 runtime missing, fetching bootstrapper");
  wchar_t tmp[MAX_PATH];
  GetTempPathW(MAX_PATH, tmp);
  // 文件名不可预测（同用户其它进程不能提前占位），且校验之后一直持有一个只放行读取的句柄，直到引导程序跑完：
  // 这段时间里没人能改写或替换这个文件，签名校验与实际执行的是同一份字节。
  GUID g{};
  CoCreateGuid(&g);
  wchar_t gs[48]{};
  StringFromGUID2(g, gs, 48);
  const fs::path exe = fs::path(tmp) / std::format(L"dx-wv2-{}.exe", gs);
  bool ok = false;
  HANDLE hold = INVALID_HANDLE_VALUE;
  if (!download(L"go.microsoft.com", L"/fwlink/p/?LinkId=2124703", exe)) {
    if (detail) *detail = L"download";
  } else if ((hold = CreateFileW(exe.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) == INVALID_HANDLE_VALUE) {
    if (detail) *detail = L"download";
    log(L"cannot lock the bootstrapper file, not executed");
  } else if (!signed_by_microsoft(exe.c_str())) {
    if (detail) *detail = L"signature";
    log(L"bootstrapper signature check failed, not executed");
  } else {
    const int rc = run_wait(L"\"" + exe.wstring() + L"\" /silent /install", 300000);
    ok = rc == 0 && webview2_present();
    if (!ok && detail) *detail = L"install";
    log(std::format(L"bootstrapper exit {}", rc));
  }
  if (hold != INVALID_HANDLE_VALUE) CloseHandle(hold);
  std::error_code ec;
  fs::remove(exe, ec);
  return ok;
}

bool make_shortcut(const fs::path& link, const fs::path& target, const std::wstring& args, const fs::path& workdir) {
  std::error_code ec;
  fs::create_directories(link.parent_path(), ec);
  IShellLinkW* sl = nullptr;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&sl)))) return false;
  sl->SetPath(target.c_str());
  sl->SetArguments(args.c_str());
  sl->SetWorkingDirectory(workdir.c_str());
  sl->SetIconLocation(target.c_str(), 0);
  sl->SetDescription(L"Deixion");
  bool ok = false;
  IPersistFile* pf = nullptr;
  if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pf)))) {
    ok = SUCCEEDED(pf->Save(link.c_str(), TRUE));
    pf->Release();
  }
  sl->Release();
  return ok;
}

bool write_uninstall_entry(const UninstallInfo& i) {
  HKEY k;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
  const std::wstring un = L"\"" + (i.dir / L"uninstall.exe").wstring() + L"\"";
  bool ok = set_str(k, L"DisplayName", L"Deixion") && set_str(k, L"DisplayVersion", i.version) && set_str(k, L"Publisher", L"Deixion") &&
            set_str(k, L"InstallLocation", i.dir.wstring()) && set_str(k, L"DisplayIcon", (i.dir / L"Deixion.exe").wstring()) &&
            set_str(k, L"UninstallString", un + L" /UNINSTALL") && set_str(k, L"QuietUninstallString", un + L" /UNINSTALL /S") &&
            set_str(k, L"URLInfoAbout", L"https://github.com/Aevorine/Deixion");
  const DWORD one = 1, kb = static_cast<DWORD>(i.size_kb);
  RegSetValueExW(k, L"NoModify", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof one);
  RegSetValueExW(k, L"NoRepair", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof one);
  RegSetValueExW(k, L"EstimatedSize", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&kb), sizeof kb);
  RegCloseKey(k);
  return ok;
}

void remove_uninstall_entry() { RegDeleteKeyW(HKEY_CURRENT_USER, kUninstallKey); }

fs::path installed_dir() {
  const std::wstring v = get_str(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation");
  return v.empty() ? fs::path() : fs::path(v);
}

bool run_key_present() { return !get_str(HKEY_CURRENT_USER, kRunKey, L"Deixion").empty(); }

void set_run_key(const fs::path& exe) {
  HKEY k;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
  set_str(k, L"Deixion", L"\"" + exe.wstring() + L"\" --tray");
  RegCloseKey(k);
}

void clear_run_key() {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
  RegDeleteValueW(k, L"Deixion");
  RegCloseKey(k);
}

fs::path start_menu_link() { return known_folder(CSIDL_PROGRAMS) / L"Deixion.lnk"; }
fs::path desktop_link() { return known_folder(CSIDL_DESKTOPDIRECTORY) / L"Deixion.lnk"; }

}  // namespace dxsetup::sys
