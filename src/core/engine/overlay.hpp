#pragma once
#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "core/geo/meridian.hpp"

namespace dx::eng {

// 前台模式的操作可视化：点击处的涟漪、控件高亮框和动作标签。窗口是穿透点击、不抢焦点的分层窗口，
// 自己有消息循环线程，调用方只投递事件，不阻塞。
class Overlay {
 public:
  static Overlay& get();
  void start();
  void stop();
  void ripple(geo::PointI screen, const std::string& label);
  void highlight(geo::RectI screen_rect, const std::string& label);

 private:
  Overlay() = default;
  struct Item {
    int kind{0};
    geo::RectI r;
    std::string label;
    u64 born_ms{0};
    u32 life_ms{600};
  };
  void run();
  void draw();
  static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
  void push(Item it);

  std::thread th_;
  std::atomic<bool> running_{false};
  std::atomic<DWORD> tid_{0};
  HWND hwnd_{nullptr};
  std::mutex mu_;
  std::vector<Item> items_;
  geo::RectI vs_;
};

}  // namespace dx::eng
