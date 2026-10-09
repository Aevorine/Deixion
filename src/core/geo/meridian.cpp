#include "core/geo/meridian.hpp"

#include <immintrin.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/base/cpu.hpp"

namespace dx::geo {
namespace {
constexpr char kAlpha[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

int alpha_index(char c) {
  if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
  if (c == 'I' || c == 'L') c = '1';
  if (c == 'O') c = '0';
  for (int i = 0; i < 32; ++i)
    if (kAlpha[i] == c) return i;
  return -1;
}

u32 spread_soft(u32 v) {
  v &= 0xFFFF;
  v = (v | (v << 8)) & 0x00FF00FF;
  v = (v | (v << 4)) & 0x0F0F0F0F;
  v = (v | (v << 2)) & 0x33333333;
  v = (v | (v << 1)) & 0x55555555;
  return v;
}
__attribute__((target("bmi2"))) u32 spread_hw(u32 v) { return _pdep_u32(v & 0xFFFF, 0x55555555u); }

u32 spread(u32 v) {
  static const bool hw = cpu().bmi2;
  return hw ? spread_hw(v) : spread_soft(v);
}

u32 quant16(double v) {
  const double c = std::clamp(v, 0.0, 1.0);
  return static_cast<u32>(std::min(65535.0, std::floor(c * 65536.0)));
}
}  // namespace

RectI RectI::intersect(const RectI& o) const {
  const i32 l = std::max(x, o.x), t = std::max(y, o.y), r = std::min(right(), o.right()), b = std::min(bottom(), o.bottom());
  return r > l && b > t ? RectI{l, t, r - l, b - t} : RectI{};
}

LatLon clamp01(LatLon p) { return {std::clamp(p.lam, 0.0, 1.0), std::clamp(p.phi, 0.0, 1.0)}; }

PointI to_px(const Frame& f, LatLon p) {
  p = clamp01(p);
  const i32 x = f.r.x + std::clamp(static_cast<i32>(std::floor(p.lam * f.r.w)), 0, std::max(0, f.r.w - 1));
  const i32 y = f.r.y + std::clamp(static_cast<i32>(std::floor(p.phi * f.r.h)), 0, std::max(0, f.r.h - 1));
  return {x, y};
}

LatLon from_px(const Frame& f, PointI p) {
  if (f.r.w <= 0 || f.r.h <= 0) return {};
  return clamp01({(p.x - f.r.x + 0.5) / f.r.w, (p.y - f.r.y + 0.5) / f.r.h});
}

Frame sub_frame(const Frame& f, const Region& g) {
  const double l0 = std::min(g.a.lam, g.b.lam), l1 = std::max(g.a.lam, g.b.lam);
  const double p0 = std::min(g.a.phi, g.b.phi), p1 = std::max(g.a.phi, g.b.phi);
  const i32 x0 = f.r.x + static_cast<i32>(std::floor(l0 * f.r.w)), x1 = f.r.x + static_cast<i32>(std::ceil(l1 * f.r.w));
  const i32 y0 = f.r.y + static_cast<i32>(std::floor(p0 * f.r.h)), y1 = f.r.y + static_cast<i32>(std::ceil(p1 * f.r.h));
  Frame o = f;
  o.r = RectI{x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)}.intersect(f.r);
  return o;
}

LatLon compose(const Region& parent, LatLon local) {
  return {parent.a.lam + (parent.b.lam - parent.a.lam) * local.lam, parent.a.phi + (parent.b.phi - parent.a.phi) * local.phi};
}

u32 morton_key(LatLon p) { return (spread(quant16(p.lam)) << 1) | spread(quant16(p.phi)); }

std::string code_of(LatLon p, int level) {
  level = std::clamp(level, 1, kMaxLevel);
  const u64 key35 = static_cast<u64>(morton_key(p)) << 3;
  std::string s;
  s.reserve(static_cast<size_t>(level));
  for (int i = 0; i < level; ++i) s.push_back(kAlpha[(key35 >> (35 - 5 * (i + 1))) & 31]);
  return s;
}

CodeCell decode(std::string_view code) {
  CodeCell out;
  if (code.empty() || code.size() > static_cast<size_t>(kMaxLevel)) return out;
  u64 bits = 0;
  for (char c : code) {
    const int v = alpha_index(c);
    if (v < 0) return out;
    bits = (bits << 5) | static_cast<u64>(v);
  }
  const int n = static_cast<int>(code.size()) * 5;
  // 前缀 n 位：高位在前，x 占奇数位（先 x 后 y）。
  u32 xv = 0, yv = 0;
  int xb = 0, yb = 0;
  for (int i = 0; i < n; ++i) {
    const u32 bit = static_cast<u32>((bits >> (n - 1 - i)) & 1);
    if (i % 2 == 0) {
      xv = (xv << 1) | bit;
      ++xb;
    } else {
      yv = (yv << 1) | bit;
      ++yb;
    }
  }
  const double wx = 1.0 / static_cast<double>(1ull << xb), wy = 1.0 / static_cast<double>(1ull << yb);
  out.cell.a = {xv * wx, yv * wy};
  out.cell.b = {(xv + 1) * wx, (yv + 1) * wy};
  out.center = {(out.cell.a.lam + out.cell.b.lam) / 2, (out.cell.a.phi + out.cell.b.phi) / 2};
  out.level = static_cast<int>(code.size());
  out.valid = true;
  return out;
}

std::vector<std::string> neighbors(std::string_view code) {
  std::vector<std::string> r;
  const CodeCell c = decode(code);
  if (!c.valid) return r;
  const double w = c.cell.b.lam - c.cell.a.lam, h = c.cell.b.phi - c.cell.a.phi;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
      if (!dx && !dy) continue;
      const double l = c.center.lam + dx * w, p = c.center.phi + dy * h;
      if (l < 0 || l >= 1 || p < 0 || p >= 1) continue;
      r.push_back(code_of({l, p}, c.level));
    }
  return r;
}

int level_for(const Frame& f) {
  const double need = std::max(f.r.w, f.r.h);
  for (int lv = 1; lv <= kMaxLevel; ++lv) {
    const int xb = (5 * lv + 1) / 2, yb = (5 * lv) / 2;
    if (std::min(static_cast<double>(1ull << xb) / std::max(1, f.r.w), static_cast<double>(1ull << yb) / std::max(1, f.r.h)) >= 1.0 && need > 0) return lv;
  }
  return kMaxLevel;
}

std::string fmt(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.4f", v);
  return b;
}
std::string fmt(LatLon p) { return fmt(p.lam) + "," + fmt(p.phi); }

}  // namespace dx::geo
