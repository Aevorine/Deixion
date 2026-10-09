#pragma once
#include <windows.h>

#include <memory>

#include "core/geo/meridian.hpp"

namespace dx::cap {

// 32 位 BGRA 自上而下的 DIB，既能直接读写像素，也能用 GDI 往上画网格与标记。
class Image {
 public:
  static std::unique_ptr<Image> create(int w, int h);
  ~Image();
  Image(const Image&) = delete;
  Image& operator=(const Image&) = delete;

  int w{0}, h{0};
  u8* bits{nullptr};
  HDC dc() const { return dc_; }
  size_t stride() const { return static_cast<size_t>(w) * 4; }

 private:
  Image() = default;
  HBITMAP bmp_{nullptr};
  HDC dc_{nullptr};
  HGDIOBJ old_{nullptr};
};

enum class Method : u8 { PrintWindow, ScreenBlt };
const char* method_name(Method m);

struct Shot {
  std::unique_ptr<Image> img;
  geo::Frame frame;
  Method method{Method::PrintWindow};
  u64 us{0};
};

Res<Shot> shoot_window(HWND h);
Res<Shot> shoot_screen(const geo::RectI& r);
Res<std::unique_ptr<Image>> crop(const Image& src, geo::RectI r);
std::unique_ptr<Image> scaled(const Image& src, int max_dim, double* factor);
bool looks_blank(const Image& im);

struct GridSpec {
  geo::Region region{};
  int cols{10};
  int rows{10};
  bool labels{true};
};
void draw_grid(Image& im, const GridSpec& g);
void draw_marker(Image& im, geo::PointI px, const std::string& label);
void draw_box(Image& im, geo::RectI r, const std::string& label);

enum class Fmt { Jpeg, Png };
Res<std::vector<u8>> encode(const Image& im, Fmt f, int quality);

struct TileMap {
  int tile{32};
  int cols{0}, rows{0};
  std::vector<u64> h;
};
TileMap tiles(const Image& im, int tile = 32);
struct Diff {
  int changed_tiles{0};
  int total_tiles{0};
  double fraction{0};
  geo::RectI bbox;
};
Diff diff(const TileMap& a, const TileMap& b, int w, int h);

}  // namespace dx::cap
