#pragma once
#include <cmath>
#include <cstdio>
#include <string>

#include "core/base/json.hpp"
#include "core/base/text.hpp"
#include "core/geo/meridian.hpp"
#include "core/win/window.hpp"

namespace dx::eng {

inline std::string hwnd_str(HWND h) {
  char b[24];
  std::snprintf(b, sizeof b, "0x%llX", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(h)));
  return b;
}

// 坐标必须是有限数：stod 会接受 "nan" / "inf"，NaN 一路传下去会被当成 (0,0) 点下去。
inline bool parse_ll(const Json& j, geo::LatLon& out) {
  geo::LatLon v{};
  if (j.is_str()) {
    const std::string& s = j.as_str();
    const size_t c = s.find(',');
    if (c == std::string::npos) return false;
    try {
      v.lam = std::stod(s.substr(0, c));
      v.phi = std::stod(s.substr(c + 1));
    } catch (...) {
      return false;
    }
  } else if (j.is_arr() && j.size() == 2 && j[0].is_num() && j[1].is_num()) {
    v = {j[0].as_num(), j[1].as_num()};
  } else if (j.is_obj() && j.has("lam") && j.has("phi")) {
    v = {j["lam"].as_num(), j["phi"].as_num()};
  } else {
    return false;
  }
  if (!std::isfinite(v.lam) || !std::isfinite(v.phi)) return false;
  out = v;
  return true;
}

inline bool parse_xy(const Json& j, geo::PointI& out) {
  if (j.is_obj() && j["x"].is_num() && j["y"].is_num()) {
    out = {static_cast<i32>(j["x"].as_num()), static_cast<i32>(j["y"].as_num())};
    return true;
  }
  if (j.is_arr() && j.size() == 2 && j[0].is_num() && j[1].is_num()) {
    out = {static_cast<i32>(j[0].as_num()), static_cast<i32>(j[1].as_num())};
    return true;
  }
  return false;
}

inline Json rect_json(const geo::RectI& r) {
  Json j = Json::object();
  j.set("x", r.x).set("y", r.y).set("w", r.w).set("h", r.h);
  return j;
}

inline Json px_json(geo::PointI p) {
  Json a = Json::array();
  a.push(p.x).push(p.y);
  return a;
}

inline std::wstring wide_of(const Json& j) { return text::widen(j.as_str()); }

}  // namespace dx::eng
