#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dx {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

enum ErrCode : int {
  E_OK = 0,
  E_BAD_ARG = 1,
  E_NOT_FOUND = 2,
  E_UNSUPPORTED = 3,
  E_TIMEOUT = 4,
  E_WIN32 = 5,
  E_COM = 6,
  E_IO = 7,
  E_DENIED = 8,
  E_BUSY = 9,
  E_STALE = 10,
  E_CANCELLED = 11,
  E_INTERNAL = 99,
};

struct Err {
  int code{E_OK};
  std::string msg;
};

template <class T>
using Res = std::expected<T, Err>;

inline std::unexpected<Err> fail(int code, std::string msg) { return std::unexpected(Err{code, std::move(msg)}); }

inline const char* err_name(int code) {
  switch (code) {
    case E_OK: return "ok";
    case E_BAD_ARG: return "bad_arg";
    case E_NOT_FOUND: return "not_found";
    case E_UNSUPPORTED: return "unsupported";
    case E_TIMEOUT: return "timeout";
    case E_WIN32: return "win32";
    case E_COM: return "com";
    case E_IO: return "io";
    case E_DENIED: return "denied";
    case E_BUSY: return "busy";
    case E_STALE: return "stale";
    case E_CANCELLED: return "cancelled";
    default: return "internal";
  }
}

template <class F>
struct Defer {
  F f;
  bool armed{true};
  explicit Defer(F fn) : f(std::move(fn)) {}
  Defer(Defer&& o) noexcept : f(std::move(o.f)), armed(o.armed) { o.armed = false; }
  Defer(const Defer&) = delete;
  ~Defer() {
    if (armed) f();
  }
};
template <class F>
Defer(F) -> Defer<F>;

}  // namespace dx
