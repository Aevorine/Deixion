#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

#include "core/base/clock.hpp"
#include "core/base/cpu.hpp"
#include "core/base/hist.hpp"
#include "core/base/paths.hpp"
#include "core/base/text.hpp"
#include "core/engine/engine.hpp"
#include "core/ipc/pipe.hpp"
#include "core/mcp/mcp.hpp"
#include "core/win/com.hpp"

using namespace dx;

namespace {

class Backend {
 public:
  Res<Json> call(std::string_view method, const Json& params) {
    std::lock_guard lk(mu_);
    if (client_) {
      auto r = client_->call(method, params);
      if (r || r.error().code != E_IO) return r;
      client_.reset();
      if (!connect(1500)) return fail(E_IO, "Deixion stopped responding");
      return client_->call(method, params);
    }
    return eng::Engine::get().call(method, params);
  }

  bool connect(unsigned ms) {
    auto c = ipc::Client::connect(ms);
    if (!c) return false;
    client_ = std::move(*c);
    return true;
  }

  bool start(bool allow_spawn, bool force_inproc) {
    if (!force_inproc) {
      if (connect(0)) return true;
      if (allow_spawn && !getenv("DEIXION_NO_APP")) {
        const auto app = paths::exe_dir() / L"Deixion.exe";
        if (std::filesystem::exists(app)) {
          STARTUPINFOW si{sizeof si};
          PROCESS_INFORMATION pi{};
          std::wstring cmd = L"\"" + app.wstring() + L"\" --tray";
          if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, app.parent_path().c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            if (connect(8000)) return true;
          }
        }
      }
    }
    com_init_thread();
    if (auto r = eng::Engine::get().start(); !r) {
      std::fprintf(stderr, "engine start failed: %s\n", r.error().msg.c_str());
      return false;
    }
    inproc_ = true;
    return true;
  }
  bool inproc() const { return inproc_; }
  void shutdown() {
    if (inproc_) eng::Engine::get().shutdown();
  }

 private:
  std::mutex mu_;
  std::unique_ptr<ipc::Client> client_;
  bool inproc_{false};
};

void out(const std::string& s) {
  std::fwrite(s.data(), 1, s.size(), stdout);
  std::fflush(stdout);
}

int print_result(const Res<Json>& r) {
  if (!r) {
    std::fprintf(stderr, "error [%s]: %s\n", err_name(r.error().code), r.error().msg.c_str());
    return 1;
  }
  Json j = *r;
  if (j["image"].is_obj()) j["image"].set("b64", "<omitted>");
  out(j.dump() + "\n");
  return 0;
}

bool read_text(const std::string& path, std::string& s) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  s = ss.str();
  return true;
}

std::string base64_decode(const std::string& in) {
  static const std::string t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  int val = 0, bits = -8;
  for (unsigned char c : in) {
    const size_t p = t.find(static_cast<char>(c));
    if (p == std::string::npos) continue;
    val = (val << 6) + static_cast<int>(p);
    bits += 6;
    if (bits >= 0) {
      o.push_back(static_cast<char>((val >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return o;
}

int cmd_shot(Backend& be, std::vector<std::string>& a) {
  Json p = Json::object();
  std::string file = "deixion-shot.jpg";
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i] == "-o" && i + 1 < a.size()) file = a[++i];
    else if (a[i] == "--no-grid") p.set("grid", false);
    else if (a[i] == "--elements") p.set("elements", true);
    else if (!p.has("window")) p.set("window", a[i]);
  }
  auto r = be.call("capture", p);
  if (!r) return print_result(r);
  const std::string bytes = base64_decode((*r)["image"]["b64"].as_str());
  std::ofstream f(file, std::ios::binary);
  f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  Json j = *r;
  j["image"].set("b64", "<saved>");
  j.set("file", file);
  out(j.dump() + "\n");
  return 0;
}

int cmd_bench(Backend& be, std::vector<std::string>& a) {
  if (a.empty()) {
    std::fprintf(stderr, "usage: deixion-cli bench <window> [rounds]\n");
    return 2;
  }
  const int rounds = a.size() > 1 ? std::max(10, std::atoi(a[1].c_str())) : 200;
  auto wi = be.call("windows", Json::object().set("filter", a[0]));
  if (!wi || (*wi)["count"].as_int() == 0) {
    std::fprintf(stderr, "window not found\n");
    return 1;
  }
  Json base = Json::object();
  base.set("window", a[0]);
  struct Row {
    const char* name;
    const char* method;
    Json params;
  };
  std::vector<Row> rows;
  rows.push_back({"geo (pure math)", "geo", Json::object().set("window", a[0]).set("at", "0.5,0.5")});
  rows.push_back({"elements (cached)", "elements", Json::object().set("window", a[0]).set("limit", 50)});
  rows.push_back({"locate", "locate", Json::object().set("window", a[0]).set("at", "0.5,0.5")});
  rows.push_back({"hover (message)", "click", Json::object().set("window", a[0]).set("at", "0.5,0.5").set("hover", true)});
  rows.push_back({"capture (no grid)", "capture", Json::object().set("window", a[0]).set("grid", false).set("max_dim", 800)});
  out("round trips through " + std::string(be.inproc() ? "in-process engine" : "named pipe") + ", " + std::to_string(rounds) + " rounds each\n");
  for (const auto& row : rows) {
    Hist h;
    int fails = 0;
    (void)be.call(row.method, row.params);
    for (int i = 0; i < rounds; ++i) {
      Stopwatch sw;
      auto r = be.call(row.method, row.params);
      h.record(sw.ns());
      if (!r) ++fails;
    }
    char line[256];
    std::snprintf(line, sizeof line, "%-22s p50 %8.0f us   p90 %8.0f us   p99 %8.0f us   min %8.0f us   fails %d\n", row.name, static_cast<double>(h.quantile(0.5)) / 1000.0,
                  static_cast<double>(h.quantile(0.9)) / 1000.0, static_cast<double>(h.quantile(0.99)) / 1000.0, static_cast<double>(h.min()) / 1000.0, fails);
    out(line);
  }
  return 0;
}

std::string reg_string(HKEY root, const wchar_t* path, const wchar_t* name) {
  wchar_t buf[256] = {};
  DWORD n = sizeof buf;
  if (RegGetValueW(root, path, name, RRF_RT_REG_SZ, nullptr, buf, &n) == ERROR_SUCCESS) return text::narrow(buf);
  return {};
}

int cmd_doctor(Backend& be) {
  auto line = [](const char* st, const std::string& s) { out(std::string(st) + "  " + s + "\n"); };
  using Rtl = LONG(WINAPI*)(OSVERSIONINFOW*);
  OSVERSIONINFOW vi{sizeof vi};
  if (auto f = reinterpret_cast<Rtl>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")))) f(&vi);
  line(vi.dwBuildNumber >= 19041 ? "ok  " : "warn", "Windows build " + std::to_string(vi.dwBuildNumber));
  line("ok  ", std::string("CPU ") + cpu().brand + (cpu().bmi2 ? "  [bmi2]" : "") + (cpu().avx2 ? " [avx2]" : "") + (cpu().sse42 ? " [sse4.2]" : ""));
  const std::string wv = reg_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}", L"pv");
  line(wv.empty() ? "warn" : "ok  ", wv.empty() ? "WebView2 runtime not found (the window UI needs it; MCP/CLI work without)" : "WebView2 runtime " + wv);
  auto r = be.call("status", Json::object());
  line(r ? "ok  " : "fail", r ? std::string("engine reachable via ") + (be.inproc() ? "in-process" : "pipe") + ", mode " + (*r)["mode"].as_str() : "engine: " + r.error().msg);
  if (r) {
    line("ok  ", "UI Automation reachable: " + std::string(be.call("windows", Json::object()) ? "yes" : "no"));
    line("ok  ", "data dir " + (*r)["data_dir"].as_str() + ((*r)["portable"].as_bool() ? " (portable)" : ""));
    line((*r)["allow_hop"].as_bool() ? "info" : "ok  ", std::string("brief-foreground fallback: ") + ((*r)["allow_hop"].as_bool() ? "allowed" : "off"));
  }
  char buf[MAX_PATH];
  const bool claude = SearchPathW(nullptr, L"claude", L".cmd", MAX_PATH, reinterpret_cast<wchar_t*>(buf), nullptr) || SearchPathW(nullptr, L"claude", L".exe", MAX_PATH, reinterpret_cast<wchar_t*>(buf), nullptr);
  line(claude ? "ok  " : "info", claude ? "Claude Code CLI found on PATH" : "Claude Code CLI not found on PATH");
  return 0;
}

void usage() {
  out(
      "deixion-cli " DX_VERSION
      "\n\n"
      "  mcp                          run the MCP server on stdin/stdout (for Claude Code)\n"
      "  call <method> [json|@file]   call any engine method\n"
      "  windows [filter]             list windows\n"
      "  shot [window] [-o f.jpg] [--no-grid] [--elements]\n"
      "  elements <window> [query]    list UI Automation elements\n"
      "  bench <window> [rounds]      latency benchmark of the main paths\n"
      "  status | doctor | version\n\n"
      "  --inproc                     do not use a running Deixion; run the engine in this process\n");
}

}  // namespace

int main(int argc, char** argv) {
  SetConsoleOutputCP(CP_UTF8);
  std::vector<std::string> a(argv + 1, argv + argc);
  bool inproc = false;
  std::erase_if(a, [&](const std::string& s) {
    if (s == "--inproc") {
      inproc = true;
      return true;
    }
    return false;
  });
  if (a.empty() || a[0] == "help" || a[0] == "--help" || a[0] == "-h") {
    usage();
    return a.empty() ? 2 : 0;
  }
  const std::string cmd = a[0];
  a.erase(a.begin());
  if (cmd == "version" || cmd == "--version") {
    out("deixion-cli " DX_VERSION "\n");
    return 0;
  }
  Backend be;
  if (!be.start(cmd == "mcp", inproc)) return 3;
  int rc = 0;
  if (cmd == "mcp") {
    rc = mcp::run_stdio([&](std::string_view m, const Json& p) { return be.call(m, p); });
  } else if (cmd == "call") {
    if (a.empty()) {
      usage();
      rc = 2;
    } else {
      Json p = Json::object();
      if (a.size() > 1) {
        std::string txt = a[1];
        if (!txt.empty() && txt[0] == '@') read_text(txt.substr(1), txt);
        auto j = Json::parse(txt);
        if (!j) {
          std::fprintf(stderr, "bad json: %s\n", j.error().msg.c_str());
          be.shutdown();
          return 2;
        }
        p = *j;
      }
      rc = print_result(be.call(a[0], p));
    }
  } else if (cmd == "windows") {
    Json p = Json::object();
    if (!a.empty()) p.set("filter", a[0]);
    auto r = be.call("windows", p);
    if (r) out((*r)["text"].as_str());
    rc = r ? 0 : print_result(r);
  } else if (cmd == "elements") {
    if (a.empty()) {
      usage();
      rc = 2;
    } else {
      Json p = Json::object();
      p.set("window", a[0]);
      if (a.size() > 1) p.set("query", a[1]);
      auto r = be.call("elements", p);
      if (r) out((*r)["text"].as_str());
      rc = r ? 0 : print_result(r);
    }
  } else if (cmd == "shot") {
    rc = cmd_shot(be, a);
  } else if (cmd == "bench") {
    rc = cmd_bench(be, a);
  } else if (cmd == "status") {
    rc = print_result(be.call("status", Json::object()));
  } else if (cmd == "doctor") {
    rc = cmd_doctor(be);
  } else {
    usage();
    rc = 2;
  }
  be.shutdown();
  return rc;
}
