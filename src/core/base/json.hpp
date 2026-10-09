#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "core/base/types.hpp"

namespace dx {

class Json {
 public:
  using Arr = std::vector<Json>;
  using Obj = std::vector<std::pair<std::string, Json>>;
  enum class T : u8 { Null, Bool, Int, Num, Str, Arr, Obj };

  Json() = default;
  Json(std::nullptr_t) {}
  Json(bool v) : v_(v) {}
  Json(int v) : v_(static_cast<i64>(v)) {}
  Json(unsigned v) : v_(static_cast<i64>(v)) {}
  Json(long v) : v_(static_cast<i64>(v)) {}
  Json(long long v) : v_(static_cast<i64>(v)) {}
  Json(unsigned long v) : v_(static_cast<i64>(v)) {}
  Json(unsigned long long v) : v_(static_cast<i64>(v)) {}
  Json(double v) : v_(v) {}
  Json(const char* s) : v_(std::string(s)) {}
  Json(std::string s) : v_(std::move(s)) {}
  Json(std::string_view s) : v_(std::string(s)) {}
  Json(Arr a) : v_(std::move(a)) {}
  Json(Obj o) : v_(std::move(o)) {}

  static Json array() { return Json(Arr{}); }
  static Json object() { return Json(Obj{}); }

  T type() const { return static_cast<T>(v_.index()); }
  bool is_null() const { return type() == T::Null; }
  bool is_bool() const { return type() == T::Bool; }
  bool is_num() const { return type() == T::Int || type() == T::Num; }
  bool is_int() const { return type() == T::Int; }
  bool is_str() const { return type() == T::Str; }
  bool is_arr() const { return type() == T::Arr; }
  bool is_obj() const { return type() == T::Obj; }

  bool as_bool(bool d = false) const;
  i64 as_int(i64 d = 0) const;
  double as_num(double d = 0) const;
  const std::string& as_str() const;
  std::string str_or(std::string_view d) const { return is_str() ? as_str() : std::string(d); }

  const Json& operator[](std::string_view key) const;
  Json& operator[](std::string_view key);
  const Json& operator[](size_t i) const;
  bool has(std::string_view key) const;
  size_t size() const;
  const Arr& arr() const;
  const Obj& obj() const;
  Arr& arr_mut();
  Obj& obj_mut();

  Json& push(Json v);
  Json& set(std::string_view key, Json v);

  std::string dump() const;
  void dump_to(std::string& out) const;
  static Res<Json> parse(std::string_view text);

 private:
  std::variant<std::monostate, bool, i64, double, std::string, Arr, Obj> v_;
};

void json_escape_to(std::string& out, std::string_view s);

}  // namespace dx
