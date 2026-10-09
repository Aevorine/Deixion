#include "core/base/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace dx {
namespace {
const Json kNull;
const std::string kEmptyStr;
const Json::Arr kEmptyArr;
const Json::Obj kEmptyObj;
}  // namespace

bool Json::as_bool(bool d) const { return is_bool() ? std::get<bool>(v_) : d; }
i64 Json::as_int(i64 d) const {
  if (type() == T::Int) return std::get<i64>(v_);
  if (type() == T::Num) return static_cast<i64>(std::get<double>(v_));
  return d;
}
double Json::as_num(double d) const {
  if (type() == T::Num) return std::get<double>(v_);
  if (type() == T::Int) return static_cast<double>(std::get<i64>(v_));
  return d;
}
const std::string& Json::as_str() const { return is_str() ? std::get<std::string>(v_) : kEmptyStr; }
const Json::Arr& Json::arr() const { return is_arr() ? std::get<Arr>(v_) : kEmptyArr; }
const Json::Obj& Json::obj() const { return is_obj() ? std::get<Obj>(v_) : kEmptyObj; }
Json::Arr& Json::arr_mut() {
  if (!is_arr()) v_ = Arr{};
  return std::get<Arr>(v_);
}
Json::Obj& Json::obj_mut() {
  if (!is_obj()) v_ = Obj{};
  return std::get<Obj>(v_);
}

const Json& Json::operator[](std::string_view key) const {
  if (is_obj())
    for (const auto& kv : std::get<Obj>(v_))
      if (kv.first == key) return kv.second;
  return kNull;
}
Json& Json::operator[](std::string_view key) {
  auto& o = obj_mut();
  for (auto& kv : o)
    if (kv.first == key) return kv.second;
  o.emplace_back(std::string(key), Json());
  return o.back().second;
}
const Json& Json::operator[](size_t i) const {
  if (is_arr() && i < std::get<Arr>(v_).size()) return std::get<Arr>(v_)[i];
  return kNull;
}
bool Json::has(std::string_view key) const {
  if (is_obj())
    for (const auto& kv : std::get<Obj>(v_))
      if (kv.first == key) return true;
  return false;
}
size_t Json::size() const {
  if (is_arr()) return std::get<Arr>(v_).size();
  if (is_obj()) return std::get<Obj>(v_).size();
  return 0;
}
Json& Json::push(Json v) {
  arr_mut().push_back(std::move(v));
  return *this;
}
Json& Json::set(std::string_view key, Json v) {
  (*this)[key] = std::move(v);
  return *this;
}

void json_escape_to(std::string& out, std::string_view s) {
  static const char* hexd = "0123456789abcdef";
  out.push_back('"');
  size_t run = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c >= 0x20 && c != '"' && c != '\\') continue;
    out.append(s.data() + run, i - run);
    run = i + 1;
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        out += "\\u00";
        out.push_back(hexd[c >> 4]);
        out.push_back(hexd[c & 15]);
    }
  }
  out.append(s.data() + run, s.size() - run);
  out.push_back('"');
}

void Json::dump_to(std::string& out) const {
  switch (type()) {
    case T::Null: out += "null"; break;
    case T::Bool: out += std::get<bool>(v_) ? "true" : "false"; break;
    case T::Int: {
      char b[24];
      auto r = std::to_chars(b, b + sizeof b, std::get<i64>(v_));
      out.append(b, r.ptr);
      break;
    }
    case T::Num: {
      const double d = std::get<double>(v_);
      if (!std::isfinite(d)) {
        out += "null";
        break;
      }
      char b[40];
      auto r = std::to_chars(b, b + sizeof b, d);
      out.append(b, r.ptr);
      break;
    }
    case T::Str: json_escape_to(out, std::get<std::string>(v_)); break;
    case T::Arr: {
      out.push_back('[');
      bool first = true;
      for (const auto& e : std::get<Arr>(v_)) {
        if (!first) out.push_back(',');
        first = false;
        e.dump_to(out);
      }
      out.push_back(']');
      break;
    }
    case T::Obj: {
      out.push_back('{');
      bool first = true;
      for (const auto& kv : std::get<Obj>(v_)) {
        if (!first) out.push_back(',');
        first = false;
        json_escape_to(out, kv.first);
        out.push_back(':');
        kv.second.dump_to(out);
      }
      out.push_back('}');
      break;
    }
  }
}

std::string Json::dump() const {
  std::string s;
  dump_to(s);
  return s;
}

namespace {
struct Parser {
  const char* p;
  const char* e;
  int depth{0};
  std::string err;

  void ws() {
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  }
  bool fail(const char* m) {
    if (err.empty()) err = m;
    return false;
  }
  static void put_utf8(std::string& s, u32 cp) {
    if (cp < 0x80) s.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
      s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  bool hex4(u32& v) {
    if (e - p < 4) return fail("bad \\u escape");
    v = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = *p++;
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<u32>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<u32>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<u32>(c - 'A' + 10);
      else return fail("bad \\u escape");
    }
    return true;
  }
  bool str(std::string& out) {
    ++p;
    while (p < e) {
      const char* run = p;
      while (p < e && *p != '"' && *p != '\\' && static_cast<unsigned char>(*p) >= 0x20) ++p;
      out.append(run, p);
      if (p >= e) break;
      const char c = *p;
      if (c == '"') {
        ++p;
        return true;
      }
      if (c != '\\') return fail("control char in string");
      ++p;
      if (p >= e) break;
      const char x = *p++;
      switch (x) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          u32 cp;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
            p += 2;
            u32 lo;
            if (!hex4(lo)) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          put_utf8(out, cp);
          break;
        }
        default: return fail("bad escape");
      }
    }
    return fail("unterminated string");
  }
  bool value(Json& out) {
    ws();
    if (p >= e) return fail("unexpected end");
    if (++depth > 128) return fail("too deep");
    Defer d([&] { --depth; });
    const char c = *p;
    if (c == '{') {
      ++p;
      Json::Obj o;
      ws();
      if (p < e && *p == '}') {
        ++p;
        out = Json(std::move(o));
        return true;
      }
      for (;;) {
        ws();
        if (p >= e || *p != '"') return fail("expected key");
        std::string k;
        if (!str(k)) return false;
        ws();
        if (p >= e || *p != ':') return fail("expected ':'");
        ++p;
        Json v;
        if (!value(v)) return false;
        o.emplace_back(std::move(k), std::move(v));
        ws();
        if (p < e && *p == ',') {
          ++p;
          continue;
        }
        if (p < e && *p == '}') {
          ++p;
          break;
        }
        return fail("expected ',' or '}'");
      }
      out = Json(std::move(o));
      return true;
    }
    if (c == '[') {
      ++p;
      Json::Arr a;
      ws();
      if (p < e && *p == ']') {
        ++p;
        out = Json(std::move(a));
        return true;
      }
      for (;;) {
        Json v;
        if (!value(v)) return false;
        a.push_back(std::move(v));
        ws();
        if (p < e && *p == ',') {
          ++p;
          continue;
        }
        if (p < e && *p == ']') {
          ++p;
          break;
        }
        return fail("expected ',' or ']'");
      }
      out = Json(std::move(a));
      return true;
    }
    if (c == '"') {
      std::string s;
      if (!str(s)) return false;
      out = Json(std::move(s));
      return true;
    }
    if (e - p >= 4 && std::memcmp(p, "true", 4) == 0) {
      p += 4;
      out = Json(true);
      return true;
    }
    if (e - p >= 5 && std::memcmp(p, "false", 5) == 0) {
      p += 5;
      out = Json(false);
      return true;
    }
    if (e - p >= 4 && std::memcmp(p, "null", 4) == 0) {
      p += 4;
      out = Json();
      return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      const char* s = p;
      bool is_int = true;
      if (*p == '-') ++p;
      while (p < e && *p >= '0' && *p <= '9') ++p;
      if (p < e && *p == '.') {
        is_int = false;
        ++p;
        while (p < e && *p >= '0' && *p <= '9') ++p;
      }
      if (p < e && (*p == 'e' || *p == 'E')) {
        is_int = false;
        ++p;
        if (p < e && (*p == '+' || *p == '-')) ++p;
        while (p < e && *p >= '0' && *p <= '9') ++p;
      }
      if (is_int) {
        i64 v = 0;
        auto r = std::from_chars(s, p, v);
        if (r.ec == std::errc()) {
          out = Json(v);
          return true;
        }
      }
      const std::string tmp(s, p);
      char* endp = nullptr;
      const double d = std::strtod(tmp.c_str(), &endp);
      if (endp == tmp.c_str()) return fail("bad number");
      out = Json(d);
      return true;
    }
    return fail("unexpected character");
  }
};
}  // namespace

Res<Json> Json::parse(std::string_view text) {
  Parser ps{text.data(), text.data() + text.size()};
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
      static_cast<unsigned char>(text[2]) == 0xBF)
    ps.p += 3;
  Json v;
  if (!ps.value(v)) return fail(E_BAD_ARG, "json: " + ps.err);
  ps.ws();
  if (ps.p != ps.e) return fail(E_BAD_ARG, "json: trailing data");
  return v;
}

}  // namespace dx
