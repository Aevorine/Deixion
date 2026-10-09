#pragma once
#include <string>

#include "core/base/types.hpp"

// Meridian：把任意矩形（屏幕、窗口客户区、控件）归一化成 [0,1]² 的“经纬度”平面。
// λ（lam）是横向经度，φ（phi）是纵向纬度，左上 (0,0)，右下 (1,1)。同一个 (λ,φ) 在任何分辨率、
// DPI、窗口缩放下指向同一处内容。另给每个位置一个层级化的短码（Morton 交织 + Base32），
// 码越长格子越小，前缀就是外层格子，可以由粗到细逐级定位。
namespace dx::geo {

struct PointI {
  i32 x{0}, y{0};
};

struct RectI {
  i32 x{0}, y{0}, w{0}, h{0};
  i32 right() const { return x + w; }
  i32 bottom() const { return y + h; }
  bool empty() const { return w <= 0 || h <= 0; }
  bool contains(i32 px, i32 py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  i64 area() const { return static_cast<i64>(w) * h; }
  RectI intersect(const RectI& o) const;
  PointI center() const { return {x + w / 2, y + h / 2}; }
};

struct LatLon {
  double lam{0}, phi{0};
};

struct Region {
  LatLon a{0, 0}, b{1, 1};
};

struct Frame {
  RectI r;
  u32 dpi{96};
  u64 owner{0};
};

PointI to_px(const Frame& f, LatLon p);
LatLon from_px(const Frame& f, PointI p);
Frame sub_frame(const Frame& f, const Region& g);
LatLon compose(const Region& parent, LatLon local);
LatLon clamp01(LatLon p);

constexpr int kMaxLevel = 6;
u32 morton_key(LatLon p);
std::string code_of(LatLon p, int level);
struct CodeCell {
  Region cell;
  LatLon center;
  int level{0};
  bool valid{false};
};
CodeCell decode(std::string_view code);
std::vector<std::string> neighbors(std::string_view code);
// 满足像素精度所需的最短层级。
int level_for(const Frame& f);

std::string fmt(LatLon p);
std::string fmt(double v);

}  // namespace dx::geo
