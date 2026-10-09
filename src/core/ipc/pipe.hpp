#pragma once
#include <windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <set>
#include <thread>

#include "core/base/json.hpp"

namespace dx::ipc {

// 命名管道，仅当前用户可连（DACL 只给本用户与 SYSTEM，拒绝远程客户端）。
// 帧：u32 小端长度 + UTF-8 JSON。请求 {id,method,params}，响应 {id,ok,result|error}。
std::wstring pipe_name();

using Handler = std::function<Res<Json>(std::string_view method, const Json& params)>;

class Server {
 public:
  ~Server() { stop(); }
  Res<void> start(Handler h);
  void stop();
  size_t clients() const { return clients_.load(); }

 private:
  void accept_loop();
  void serve(HANDLE pipe);
  Handler handler_;
  std::atomic<bool> running_{false};
  std::atomic<size_t> clients_{0};
  std::thread accept_;
  std::mutex mu_;
  std::set<HANDLE> open_;
  HANDLE first_{INVALID_HANDLE_VALUE};
};

class Client {
 public:
  static Res<std::unique_ptr<Client>> connect(unsigned timeout_ms);
  ~Client();
  Res<Json> call(std::string_view method, const Json& params);

 private:
  HANDLE h_{INVALID_HANDLE_VALUE};
  u64 next_id_{1};
};

}  // namespace dx::ipc
