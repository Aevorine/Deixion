#pragma once
#include <windows.h>

#include "core/geo/meridian.hpp"
#include "core/input/keys.hpp"

namespace dx::input {

enum class Button : u8 { Left, Right, Middle };

// 后台通道：直接向目标窗口的消息队列发鼠标/键盘消息，不动真实光标、不抢焦点。
Res<void> msg_click(HWND top, geo::PointI screen, Button b, int count, bool hover_only = false);
Res<void> msg_drag(HWND top, geo::PointI from, geo::PointI to, Button b, int steps);
Res<void> msg_scroll(HWND top, geo::PointI screen, int v_notches, int h_notches);
// direct=true 时 dest 就是接收字符的控件；否则取 dest 所在线程的焦点控件。
Res<void> msg_text(HWND dest, const std::wstring& text, bool direct = false);
HWND focus_hwnd(HWND top);
Res<void> msg_key(HWND top, const KeyChord& c);
// 带修饰键的后台快捷键：临时把目标线程的输入队列并到本线程，写入共享键盘状态后再投递。
Res<void> msg_chord_attached(HWND top, const KeyChord& c);

// 前台通道：真实输入（SendInput）。
Res<void> si_move(geo::PointI p);
Res<void> si_move_smooth(geo::PointI from, geo::PointI to, int duration_ms);
Res<void> si_click(geo::PointI p, Button b, int count);
Res<void> si_drag(geo::PointI from, geo::PointI to, Button b, int duration_ms);
Res<void> si_scroll(geo::PointI p, int v_notches, int h_notches);
Res<void> si_text(const std::wstring& text);
Res<void> si_key(const KeyChord& c);

// 快速跳跃：记住前台窗口、光标、焦点，激活目标，作用域结束时还原。
class Hop {
 public:
  explicit Hop(HWND target, bool restore_cursor = true);
  ~Hop();
  bool ok() const { return ok_; }
  Hop(const Hop&) = delete;
  Hop& operator=(const Hop&) = delete;

 private:
  HWND prev_fg_{nullptr};
  POINT prev_cursor_{};
  bool restore_cursor_{true};
  bool changed_{false};
  bool ok_{false};
};

bool force_foreground(HWND h);
POINT cursor_pos();

}  // namespace dx::input
