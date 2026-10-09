#include "core/ipc/pipe.hpp"

#include <sddl.h>

#include "core/base/clock.hpp"
#include "core/base/log.hpp"
#include "core/base/text.hpp"
#include "core/win/com.hpp"

namespace dx::ipc {
namespace {
constexpr u32 kMaxFrame = 96u * 1024 * 1024;

bool token_user(HANDLE proc, std::vector<u8>& buf) {
  HANDLE tok = nullptr;
  if (!OpenProcessToken(proc, TOKEN_QUERY, &tok)) return false;
  DWORD n = 0;
  GetTokenInformation(tok, TokenUser, nullptr, 0, &n);
  buf.resize(n);
  const bool ok = n && GetTokenInformation(tok, TokenUser, buf.data(), n, &n);
  CloseHandle(tok);
  return ok;
}

// 连上的服务端必须属于当前账户：管道名全机共享，多用户机器上别的账户可以抢先建同名管道，
// 客户端不核对就会把要输入的文字交给它。
bool server_is_me(HANDLE pipe) {
  ULONG pid = 0;
  if (!GetNamedPipeServerProcessId(pipe, &pid) || !pid) return false;
  HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!p) return false;
  std::vector<u8> a, b;
  const bool ok = token_user(p, a) && token_user(GetCurrentProcess(), b) &&
                  EqualSid(reinterpret_cast<TOKEN_USER*>(a.data())->User.Sid, reinterpret_cast<TOKEN_USER*>(b.data())->User.Sid);
  CloseHandle(p);
  return ok;
}

std::wstring user_sid_string() {
  HANDLE tok = nullptr;
  std::wstring out;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
    DWORD n = 0;
    GetTokenInformation(tok, TokenUser, nullptr, 0, &n);
    std::vector<u8> buf(n);
    if (GetTokenInformation(tok, TokenUser, buf.data(), n, &n)) {
      LPWSTR s = nullptr;
      if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &s)) {
        out = s;
        LocalFree(s);
      }
    }
    CloseHandle(tok);
  }
  return out;
}

bool read_exact(HANDLE h, void* buf, size_t n) {
  u8* p = static_cast<u8*>(buf);
  while (n) {
    DWORD got = 0;
    if (!ReadFile(h, p, static_cast<DWORD>(std::min<size_t>(n, 1u << 20)), &got, nullptr) || !got) return false;
    p += got;
    n -= got;
  }
  return true;
}

bool write_all(HANDLE h, const void* buf, size_t n) {
  const u8* p = static_cast<const u8*>(buf);
  while (n) {
    DWORD w = 0;
    if (!WriteFile(h, p, static_cast<DWORD>(std::min<size_t>(n, 1u << 20)), &w, nullptr) || !w) return false;
    p += w;
    n -= w;
  }
  return true;
}

bool send_frame(HANDLE h, const std::string& s) {
  const u32 len = static_cast<u32>(s.size());
  return write_all(h, &len, 4) && write_all(h, s.data(), s.size());
}

bool recv_frame(HANDLE h, std::string& out) {
  u32 len = 0;
  if (!read_exact(h, &len, 4) || len > kMaxFrame) return false;
  out.resize(len);
  return len == 0 || read_exact(h, out.data(), len);
}
}  // namespace

std::wstring pipe_name() {
  wchar_t user[256] = {};
  DWORD n = 256;
  GetUserNameW(user, &n);
  return L"\\\\.\\pipe\\Deixion-v1-" + std::wstring(user);
}

Res<void> Server::start(Handler h) {
  if (running_) return {};
  handler_ = std::move(h);
  const std::wstring sddl = L"D:P(A;;GA;;;" + user_sid_string() + L")(A;;GA;;;SY)";
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, FALSE};
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sa.lpSecurityDescriptor, nullptr)) return fail(E_WIN32, "cannot build pipe security descriptor");
  first_ = CreateNamedPipeW(pipe_name().c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                            PIPE_UNLIMITED_INSTANCES, 1 << 20, 1 << 20, 0, &sa);
  LocalFree(sa.lpSecurityDescriptor);
  if (first_ == INVALID_HANDLE_VALUE) return fail(E_BUSY, "another Deixion engine already owns the pipe");
  running_ = true;
  accept_ = std::thread([this] { accept_loop(); });
  return {};
}

void Server::accept_loop() {
  const std::wstring sddl = L"D:P(A;;GA;;;" + user_sid_string() + L")(A;;GA;;;SY)";
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, FALSE};
  ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sa.lpSecurityDescriptor, nullptr);
  HANDLE pipe = first_;
  first_ = INVALID_HANDLE_VALUE;
  while (running_) {
    if (pipe == INVALID_HANDLE_VALUE) {
      pipe = CreateNamedPipeW(pipe_name().c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES, 1 << 20,
                              1 << 20, 0, &sa);
      if (pipe == INVALID_HANDLE_VALUE) {
        sleep_us(50000);
        continue;
      }
    }
    const BOOL ok = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
    if (!running_) {
      CloseHandle(pipe);
      break;
    }
    if (!ok) {
      CloseHandle(pipe);
      pipe = INVALID_HANDLE_VALUE;
      continue;
    }
    {
      std::lock_guard lk(mu_);
      open_.insert(pipe);
    }
    clients_.fetch_add(1);
    std::thread([this, pipe] { serve(pipe); }).detach();
    pipe = INVALID_HANDLE_VALUE;
  }
  if (sa.lpSecurityDescriptor) LocalFree(sa.lpSecurityDescriptor);
}

void Server::serve(HANDLE pipe) {
  com_init_thread();
  std::string frame;
  while (running_ && recv_frame(pipe, frame)) {
    auto req = Json::parse(frame);
    Json resp = Json::object();
    if (!req || !req->is_obj()) {
      resp.set("ok", false).set("error", Json::object().set("code", "bad_arg").set("message", "malformed request"));
    } else {
      resp.set("id", (*req)["id"]);
      auto r = handler_((*req)["method"].as_str(), (*req)["params"]);
      if (r) {
        resp.set("ok", true).set("result", std::move(*r));
      } else {
        resp.set("ok", false).set("error", Json::object().set("code", err_name(r.error().code)).set("message", r.error().msg));
      }
    }
    if (!send_frame(pipe, resp.dump())) break;
  }
  {
    std::lock_guard lk(mu_);
    open_.erase(pipe);
  }
  DisconnectNamedPipe(pipe);
  CloseHandle(pipe);
  clients_.fetch_sub(1);
}

void Server::stop() {
  if (!running_.exchange(false)) return;
  HANDLE h = CreateFileW(pipe_name().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
  {
    std::lock_guard lk(mu_);
    for (HANDLE p : open_) CancelIoEx(p, nullptr);
  }
  if (accept_.joinable()) accept_.join();
  const u64 end = now_us() + 1500000;
  while (clients_.load() && now_us() < end) sleep_us(2000);
}

Res<std::unique_ptr<Client>> Client::connect(unsigned timeout_ms) {
  const u64 end = now_us() + static_cast<u64>(timeout_ms) * 1000;
  const std::wstring name = pipe_name();
  for (;;) {
    HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
      if (!server_is_me(h)) {
        CloseHandle(h);
        LOGW("ipc", "the Deixion pipe is served by another account; refusing to connect");
        return fail(E_DENIED, "the Deixion pipe is owned by a different account");
      }
      DWORD mode = PIPE_READMODE_BYTE;
      SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
      auto c = std::unique_ptr<Client>(new Client());
      c->h_ = h;
      return c;
    }
    const DWORD e = GetLastError();
    if (now_us() >= end) return fail(E_NOT_FOUND, "Deixion is not running");
    if (e == ERROR_PIPE_BUSY) WaitNamedPipeW(name.c_str(), 200);
    else sleep_us(60000);
  }
}

Client::~Client() {
  if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
}

Res<Json> Client::call(std::string_view method, const Json& params) {
  Json req = Json::object();
  const u64 id = next_id_++;
  req.set("id", id).set("method", std::string(method)).set("params", params);
  if (!send_frame(h_, req.dump())) return fail(E_IO, "connection to Deixion was lost");
  std::string frame;
  if (!recv_frame(h_, frame)) return fail(E_IO, "connection to Deixion was lost");
  auto r = Json::parse(frame);
  if (!r) return fail(E_IO, "bad response from Deixion");
  if ((*r)["ok"].as_bool()) return (*r)["result"];
  int code = E_INTERNAL;
  const std::string cn = (*r)["error"]["code"].as_str();
  for (int c : {E_BAD_ARG, E_NOT_FOUND, E_UNSUPPORTED, E_TIMEOUT, E_WIN32, E_COM, E_IO, E_DENIED, E_BUSY, E_STALE, E_CANCELLED})
    if (cn == err_name(c)) code = c;
  return fail(code, (*r)["error"]["message"].as_str());
}

}  // namespace dx::ipc
