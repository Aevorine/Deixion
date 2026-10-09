#include "installer.hpp"

#include <windows.h>

#include <format>

#include "sysops.hpp"

#ifndef DX_VERSION
#define DX_VERSION "0"
#endif
#define DX_W2(x) L##x
#define DX_W(x) DX_W2(x)

namespace dxsetup {
namespace {

struct Swap {
  fs::path final_path, fresh, old;
  bool had_old{false};
  bool committed{false};
};

void rm(const fs::path& p) {
  std::error_code ec;
  fs::remove(p, ec);
}

bool write_file(const fs::path& p, const void* data, size_t n) {
  HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  const auto* b = static_cast<const uint8_t*>(data);
  bool ok = true;
  while (n && ok) {
    DWORD w = 0;
    ok = WriteFile(h, b, static_cast<DWORD>(std::min<size_t>(n, 1u << 20)), &w, nullptr) && w;
    b += w;
    n -= w;
  }
  ok = ok && FlushFileBuffers(h);
  CloseHandle(h);
  return ok;
}

// 上次中断留下的 .dxnew / .dxold 先清掉，避免被误当成现有文件。
void sweep(const fs::path& dir) {
  std::error_code ec;
  if (!fs::exists(dir, ec)) return;
  for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    const auto ext = it->path().extension().wstring();
    if (ext == L".dxnew" || ext == L".dxold") rm(it->path());
  }
}

void rollback(std::vector<Swap>& sw) {
  for (auto it = sw.rbegin(); it != sw.rend(); ++it) {
    if (it->committed) {
      rm(it->final_path);
      if (it->had_old) MoveFileExW(it->old.c_str(), it->final_path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } else {
      if (it->had_old) MoveFileExW(it->old.c_str(), it->final_path.c_str(), MOVEFILE_REPLACE_EXISTING);
      rm(it->fresh);
    }
  }
}

fs::path self_path() {
  wchar_t b[MAX_PATH * 2];
  const DWORD n = GetModuleFileNameW(nullptr, b, static_cast<DWORD>(std::size(b)));
  return fs::path(std::wstring(b, n));
}

}  // namespace

Result install(const Payload& pl, const Options& o, const Progress& prog) {
  Result res;
  auto step = [&](int pct, int st) { if (prog) prog(pct, st); };
  step(1, StPrepare);
  std::error_code ec;
  fs::create_directories(o.dir, ec);
  const fs::path probe = o.dir / L".dxprobe";
  if (ec || !write_file(probe, "x", 1)) {
    sys::log(L"cannot write install dir " + o.dir.wstring());
    res.why = Fail::Dir;
    return res;
  }
  rm(probe);
  sweep(o.dir);

  step(5, StStop);
  if (!sys::stop_app()) {
    res.why = Fail::Busy;
    return res;
  }

  // 阶段 A：全部解压校验并落成 .dxnew；任何一步失败都不碰现有安装。
  std::vector<Swap> sw;
  std::vector<uint8_t> buf;
  const auto& es = pl.entries();
  for (size_t i = 0; i < es.size(); ++i) {
    step(10 + static_cast<int>(60 * i / std::max<size_t>(es.size(), 1)), StUnpack);
    Swap s;
    s.final_path = o.dir / es[i].rel;
    s.fresh = s.final_path;
    s.fresh += L".dxnew";
    s.old = s.final_path;
    s.old += L".dxold";
    fs::create_directories(s.final_path.parent_path(), ec);
    if (!pl.unpack(es[i], buf)) {
      sys::log(L"payload corrupt: " + es[i].rel);
      res.why = Fail::Payload;
    } else if (!write_file(s.fresh, buf.data(), buf.size())) {
      sys::log(L"write failed: " + s.fresh.wstring());
      res.why = Fail::Write;
    }
    sw.push_back(std::move(s));
    if (res.why != Fail::None) {
      for (auto& x : sw) rm(x.fresh);
      return res;
    }
  }
  {
    Swap s;
    s.final_path = o.dir / L"uninstall.exe";
    s.fresh = s.final_path;
    s.fresh += L".dxnew";
    s.old = s.final_path;
    s.old += L".dxold";
    if (!CopyFileW(self_path().c_str(), s.fresh.c_str(), FALSE)) {
      res.why = Fail::Write;
      for (auto& x : sw) rm(x.fresh);
      return res;
    }
    sw.push_back(std::move(s));
  }

  // 阶段 B：逐个换上；运行中的 exe 也能改名，所以不会被占用卡住。
  step(72, StCommit);
  for (auto& s : sw) {
    rm(s.old);
    if (fs::exists(s.final_path, ec)) {
      if (!MoveFileExW(s.final_path.c_str(), s.old.c_str(), 0)) {
        sys::log(L"cannot move aside " + s.final_path.wstring());
        res.why = Fail::Write;
        rollback(sw);
        return res;
      }
      s.had_old = true;
    }
    if (!MoveFileExW(s.fresh.c_str(), s.final_path.c_str(), 0)) {
      sys::log(L"cannot place " + s.final_path.wstring());
      res.why = Fail::Write;
      rollback(sw);
      return res;
    }
    s.committed = true;
  }
  for (auto& s : sw) if (s.had_old) rm(s.old);
  sys::log(std::format(L"committed {} files to {}", sw.size(), o.dir.wstring()));

  step(80, StRegister);
  if (!sys::ensure_webview2(&res.note) && res.note.empty()) res.note = L"webview2";
  if (sys::webview2_present()) res.note.clear();

  const fs::path app = o.dir / L"Deixion.exe";
  sys::UninstallInfo ui;
  ui.dir = o.dir;
  ui.version = DX_W(DX_VERSION);
  ui.size_kb = (pl.raw_total() + 1023) / 1024 + 2048;
  sys::write_uninstall_entry(ui);

  // 开机自启的最终意愿由调用方给出（参数未写时沿用现状），所以旧路径的自启项会被改指到新位置。
  if (o.start_menu) sys::make_shortcut(sys::start_menu_link(), app, L"", o.dir);
  else rm(sys::start_menu_link());
  if (o.desktop) sys::make_shortcut(sys::desktop_link(), app, L"", o.dir);
  if (o.autostart) sys::set_run_key(app);
  else sys::clear_run_key();

  if (o.claude) {
    step(90, StClaude);
    const int rc = sys::run_wait(L"\"" + app.wstring() + L"\" --connect-claude", 40000, o.dir);
    sys::log(std::format(L"connect-claude exit {}", rc));
    if (rc != 0 && res.note.empty()) res.note = L"claude";
  }
  step(100, StDone);
  res.ok = true;
  if (o.relaunch) sys::spawn(L"\"" + app.wstring() + L"\" --tray", o.dir);
  return res;
}

Result uninstall(const Options& o, const Progress& prog) {
  Result res;
  auto step = [&](int pct, int st) { if (prog) prog(pct, st); };
  step(5, StStop);
  const fs::path app = o.dir / L"Deixion.exe";
  sys::stop_app();
  if (fs::exists(app)) sys::run_wait(L"\"" + app.wstring() + L"\" --disconnect-claude", 20000, o.dir);

  step(30, StRegister);
  sys::clear_run_key();
  sys::remove_uninstall_entry();
  rm(sys::start_menu_link());
  rm(sys::desktop_link());

  step(55, StCommit);
  Payload pl;
  std::error_code ec;
  if (pl.load()) {
    for (const auto& e : pl.entries()) rm(o.dir / e.rel);
  }
  for (const wchar_t* n : {L"Deixion.exe", L"deixion-cli.exe", L"WebView2Loader.dll", L"LICENSE"}) rm(o.dir / n);
  fs::remove_all(o.dir / L"skills", ec);
  if (fs::exists(o.dir, ec)) sweep(o.dir);
  if (o.purge) {
    fs::remove_all(sys::local_appdata() / L"Deixion", ec);
    if (fs::exists(o.dir / L"portable.flag", ec)) fs::remove_all(o.dir / L"data", ec);
  }
  step(95, StCommit);
  // uninstall.exe 自己还在运行：由调用方稍后清理；目录里只剩它时由调用方一并删除。
  res.ok = true;
  step(100, StDone);
  return res;
}

}  // namespace dxsetup
