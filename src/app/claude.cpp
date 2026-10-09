#include "app/claude.hpp"

#include <shlobj.h>
#include <windows.h>

#include <fstream>
#include <sstream>

#include "core/base/log.hpp"
#include "core/base/paths.hpp"
#include "core/base/text.hpp"

namespace dx::app::claude {
namespace fs = std::filesystem;
namespace {

fs::path home() {
  PWSTR w = nullptr;
  fs::path p;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &w))) {
    p = w;
    CoTaskMemFree(w);
  }
  return p;
}

fs::path cli_exe() { return paths::exe_dir() / L"deixion-cli.exe"; }
fs::path skill_src() { return paths::exe_dir() / L"skills" / L"deixion"; }
fs::path skill_dst() { return home() / L".claude" / L"skills" / L"deixion"; }

std::string read_all(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

fs::path find_claude() {
  wchar_t buf[MAX_PATH * 2];
  for (const wchar_t* ext : {L".exe", L".cmd", L".bat"}) {
    DWORD n = SearchPathW(nullptr, L"claude", ext, static_cast<DWORD>(std::size(buf)), buf, nullptr);
    if (n) return fs::path(std::wstring(buf, n));
  }
  return {};
}

// 运行命令并收集输出（隐藏窗口），最多等 30 秒。
Res<std::string> run(const std::wstring& cmdline) {
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE rd = nullptr, wr = nullptr;
  if (!CreatePipe(&rd, &wr, &sa, 0)) return fail(E_WIN32, "pipe failed");
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW si{sizeof si};
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.hStdOutput = wr;
  si.hStdError = wr;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  std::wstring cl = cmdline;
  const BOOL ok = CreateProcessW(nullptr, cl.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  CloseHandle(wr);
  if (!ok) {
    CloseHandle(rd);
    return fail(E_WIN32, "cannot start the command");
  }
  std::string out;
  char buf[4096];
  DWORD n = 0;
  while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n) out.append(buf, n);
  CloseHandle(rd);
  WaitForSingleObject(pi.hProcess, 30000);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  if (code != 0) return fail(E_INTERNAL, out.empty() ? "command failed" : out);
  return out;
}

bool copy_tree(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  fs::create_directories(to, ec);
  fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
  return !ec;
}

std::string registered_command() {
  auto j = Json::parse(read_all(home() / L".claude.json"));
  if (!j) return {};
  const Json& s = (*j)["mcpServers"]["deixion"];
  return s["command"].as_str();
}
}  // namespace

Json status(size_t clients) {
  const fs::path cli = cli_exe();
  const fs::path cc = find_claude();
  const std::string reg = registered_command();
  const bool reg_ok = !reg.empty();
  const bool path_ok = reg_ok && text::iequals(reg, text::narrow(cli.wstring()));
  const fs::path sk = skill_dst() / L"SKILL.md";
  const bool sk_ok = fs::exists(sk);
  const bool sk_cur = sk_ok && fs::exists(skill_src() / L"SKILL.md") && read_all(sk) == read_all(skill_src() / L"SKILL.md");
  Json j = Json::object();
  j.set("cli_found", !cc.empty()).set("cli_path", text::narrow(cc.wstring()));
  j.set("bridge_exe", text::narrow(cli.wstring())).set("bridge_exists", fs::exists(cli));
  j.set("mcp_registered", reg_ok).set("mcp_path_ok", path_ok).set("mcp_command", reg);
  j.set("skill_installed", sk_ok).set("skill_current", sk_cur).set("skill_available", fs::exists(skill_src() / L"SKILL.md")).set("skill_dir", text::narrow(skill_dst().wstring()));
  j.set("clients", clients);
  return j;
}

Json config_snippet() {
  Json srv = Json::object();
  srv.set("command", text::narrow(cli_exe().wstring()));
  srv.set("args", Json::array().push("mcp"));
  Json all = Json::object();
  all.set("mcpServers", Json::object().set("deixion", srv));
  Json j = Json::object();
  j.set("json", all.dump()).set("command_line", "claude mcp add --scope user deixion -- \"" + text::narrow(cli_exe().wstring()) + "\" mcp");
  return j;
}

// 端到端测试设 DEIXION_NO_CLAUDE=1：不碰真实的 Claude Code 配置（MCP 登记与 skill）。
bool skip_for_tests() { return GetEnvironmentVariableW(L"DEIXION_NO_CLAUDE", nullptr, 0) != 0; }

Res<Json> install() {
  Json out = Json::object();
  if (skip_for_tests()) return out.set("skill", "skipped").set("mcp", "skipped");
  if (fs::exists(skill_src() / L"SKILL.md")) {
    if (!copy_tree(skill_src(), skill_dst())) return fail(E_IO, "cannot copy the skill into ~/.claude/skills");
    out.set("skill", "installed");
  } else {
    out.set("skill", "not bundled");
  }
  const fs::path cc = find_claude();
  if (cc.empty()) {
    out.set("mcp", "manual").set("config", config_snippet());
    return out;
  }
  if (!fs::exists(cli_exe())) return fail(E_NOT_FOUND, "deixion-cli.exe is missing next to Deixion.exe");
  const std::wstring base = L"cmd.exe /d /s /c \"\"" + cc.wstring() + L"\" mcp ";
  (void)run(base + L"remove deixion --scope user\"");
  auto r = run(base + L"add --scope user deixion -- \"" + cli_exe().wstring() + L"\" mcp\"");
  if (!r) return fail(r.error().code, "claude mcp add failed: " + r.error().msg);
  LOGI("claude", "MCP server registered at user scope");
  out.set("mcp", "registered").set("output", *r);
  return out;
}

Res<Json> remove() {
  Json out = Json::object();
  if (skip_for_tests()) return out.set("skill", "skipped").set("mcp", "skipped");
  const fs::path cc = find_claude();
  if (!cc.empty()) {
    auto r = run(L"cmd.exe /d /s /c \"\"" + cc.wstring() + L"\" mcp remove deixion --scope user\"");
    out.set("mcp", r ? "removed" : "not registered");
  }
  std::error_code ec;
  fs::remove_all(skill_dst(), ec);
  out.set("skill", "removed");
  return out;
}

}  // namespace dx::app::claude
