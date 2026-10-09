#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "core/base/json.hpp"

namespace dx::app {

enum HotkeyId : int { HK_TOGGLE = 1, HK_MODE, HK_PAUSE, HK_UNDO, HK_SHOT, HK_STOP };

struct HotkeyStatus {
  std::string name;
  std::string chord;
  bool ok{false};
};

// 全局快捷键。设置里改了组合键就整体重新注册，被占用的会在结果里标 ok=false。
class Hotkeys {
 public:
  std::vector<HotkeyStatus> apply(HWND hwnd, const Json& chords);
  void clear(HWND hwnd);
  Json status_json() const;

 private:
  std::vector<int> ids_;
  std::vector<HotkeyStatus> st_;
};

}  // namespace dx::app
