#include "core/input/input.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <unordered_map>

#include "core/base/clock.hpp"
#include "core/win/window.hpp"

namespace dx::input {
namespace {
constexpr UINT kSmtoFlags = SMTO_ABORTIFHUNG | SMTO_NORMAL;
constexpr UINT kSmtoMs = 400;

bool smsg(HWND h, UINT m, WPARAM w, LPARAM l) {
  DWORD_PTR res = 0;
  return SendMessageTimeoutW(h, m, w, l, kSmtoFlags, kSmtoMs, &res) != 0;
}
bool pmsg(HWND h, UINT m, WPARAM w, LPARAM l) { return PostMessageW(h, m, w, l) != 0; }

LPARAM client_lp(HWND c, geo::PointI sp) {
  POINT p{sp.x, sp.y};
  ScreenToClient(c, &p);
  return MAKELPARAM(static_cast<short>(p.x), static_cast<short>(p.y));
}

struct BtnMsgs {
  UINT down, up, dbl;
  WPARAM mk;
};
BtnMsgs msgs_of(Button b) {
  switch (b) {
    case Button::Right: return {WM_RBUTTONDOWN, WM_RBUTTONUP, WM_RBUTTONDBLCLK, MK_RBUTTON};
    case Button::Middle: return {WM_MBUTTONDOWN, WM_MBUTTONUP, WM_MBUTTONDBLCLK, MK_MBUTTON};
    default: return {WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDBLCLK, MK_LBUTTON};
  }
}

LPARAM key_lp(u16 vk, bool up, bool ext) {
  LPARAM l = 1 | (static_cast<LPARAM>(scan_of(vk)) << 16);
  if (ext) l |= 1 << 24;
  if (up) l |= (1u << 30) | (1u << 31);
  return l;
}

// 后台点击 / 输入不激活目标窗口，系统因此不会给它登记键盘焦点（hwndFocus 为空）。
// 这里记下我们最后一次点击或输入的控件，当作“虚拟焦点”：真实焦点存在时以真实为准，为空时才用它。
std::mutex g_vf_mu;
std::unordered_map<HWND, HWND> g_vf;

void note_focus(HWND any, HWND ctl) {
  if (!any || !ctl) return;
  HWND root = GetAncestor(any, GA_ROOT);
  if (!root) return;
  std::lock_guard lk(g_vf_mu);
  if (g_vf.size() > 64) g_vf.clear();
  g_vf[root] = ctl;
}

HWND focus_target(HWND top) {
  DWORD pid = 0;
  const DWORD tid = GetWindowThreadProcessId(top, &pid);
  GUITHREADINFO gi{};
  gi.cbSize = sizeof gi;
  if (GetGUIThreadInfo(tid, &gi) && gi.hwndFocus) return gi.hwndFocus;
  HWND root = GetAncestor(top, GA_ROOT);
  if (root) {
    std::lock_guard lk(g_vf_mu);
    auto it = g_vf.find(root);
    if (it != g_vf.end() && IsWindow(it->second) && IsChild(root, it->second)) return it->second;
  }
  return top;
}

INPUT mouse_in(DWORD flags, DWORD data = 0) {
  INPUT in{};
  in.type = INPUT_MOUSE;
  in.mi.dwFlags = flags;
  in.mi.mouseData = data;
  return in;
}
INPUT key_in(u16 vk, bool up, bool ext) {
  INPUT in{};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = vk;
  in.ki.wScan = scan_of(vk);
  in.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (ext ? KEYEVENTF_EXTENDEDKEY : 0);
  return in;
}
Res<void> send(INPUT* in, UINT n) {
  const UINT r = SendInput(n, in, sizeof(INPUT));
  if (r != n) return fail(E_DENIED, "SendInput was blocked (target may run elevated)");
  return {};
}
}  // namespace

POINT cursor_pos() {
  POINT p{};
  GetCursorPos(&p);
  return p;
}

Res<void> msg_click(HWND top, geo::PointI sp, Button b, int count, bool hover_only) {
  HWND c = win::deepest_child_at(top, sp);
  const LPARAM lp = client_lp(c, sp);
  const BtnMsgs m = msgs_of(b);
  if (!smsg(c, WM_MOUSEMOVE, 0, lp)) return fail(E_TIMEOUT, "target window did not respond (hung?)");
  if (hover_only) return {};
  for (int i = 0; i < std::max(1, count); ++i) {
    if (!smsg(c, i == 0 ? m.down : m.dbl, m.mk, lp)) return fail(E_TIMEOUT, "target window did not respond (hung?)");
    smsg(c, m.up, 0, lp);
  }
  note_focus(top, c);
  return {};
}

// 经典 Win32 按钮（Button 类：按钮 / 复选 / 单选）的点击。按钮若弹出模态对话框，处理函数不会在超时内返回，
// 这时点击已经送达，不能当失败（否则梯子会再换一个通道点第二次）。
Res<void> bm_click(HWND button) {
  DWORD_PTR res = 0;
  SetLastError(0);
  if (SendMessageTimeoutW(button, BM_CLICK, 0, 0, kSmtoFlags, kSmtoMs, &res) == 0 && GetLastError() != ERROR_TIMEOUT)
    return fail(E_TIMEOUT, "target window did not respond (hung?)");
  return {};
}

Res<void> msg_drag(HWND top, geo::PointI from, geo::PointI to, Button b, int steps) {
  HWND c = win::deepest_child_at(top, from);
  const BtnMsgs m = msgs_of(b);
  steps = std::clamp(steps, 2, 200);
  smsg(c, WM_MOUSEMOVE, 0, client_lp(c, from));
  if (!smsg(c, m.down, m.mk, client_lp(c, from))) return fail(E_TIMEOUT, "target window did not respond (hung?)");
  for (int i = 1; i <= steps; ++i) {
    const double t = static_cast<double>(i) / steps;
    geo::PointI p{static_cast<i32>(std::lround(from.x + (to.x - from.x) * t)), static_cast<i32>(std::lround(from.y + (to.y - from.y) * t))};
    smsg(c, WM_MOUSEMOVE, m.mk, client_lp(c, p));
  }
  smsg(c, m.up, 0, client_lp(c, to));
  return {};
}

Res<void> msg_scroll(HWND top, geo::PointI sp, int v, int h) {
  HWND c = win::deepest_child_at(top, sp);
  const LPARAM lp = MAKELPARAM(static_cast<short>(sp.x), static_cast<short>(sp.y));
  if (v && !smsg(c, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<short>(v * WHEEL_DELTA)), lp)) return fail(E_TIMEOUT, "target window did not respond (hung?)");
  if (h) smsg(c, WM_MOUSEHWHEEL, MAKEWPARAM(0, static_cast<short>(h * WHEEL_DELTA)), lp);
  return {};
}

void remember_focus(HWND ctl) { note_focus(ctl, ctl); }

HWND focus_hwnd(HWND top) { return focus_target(top); }

Res<void> msg_text(HWND dest, const std::wstring& text, bool direct) {
  HWND f = direct ? dest : focus_target(dest);
  for (wchar_t ch : text) {
    if (ch == L'\r') continue;
    if (ch == L'\n') {
      if (!smsg(f, WM_CHAR, L'\r', 1)) return fail(E_TIMEOUT, "target window did not respond (hung?)");
      continue;
    }
    if (!smsg(f, WM_CHAR, ch, 1)) return fail(E_TIMEOUT, "target window did not respond (hung?)");
  }
  if (direct) note_focus(dest, dest);
  return {};
}

Res<void> msg_key(HWND top, const KeyChord& c) {
  HWND f = focus_target(top);
  if (!pmsg(f, WM_KEYDOWN, c.vk, key_lp(c.vk, false, c.ext))) return fail(E_WIN32, "cannot post key message");
  pmsg(f, WM_KEYUP, c.vk, key_lp(c.vk, true, c.ext));
  return {};
}

// 标准编辑框 / RichEdit 的 Ctrl+A/C/X/V/Z 直接发等价消息：同步、确定，不依赖键盘状态与目标线程的处理时机。
static bool edit_chord(HWND w, const KeyChord& c) {
  if (c.mods.size() != 1 || c.mods[0] != VK_CONTROL) return false;
  wchar_t cls[64]{};
  GetClassNameW(w, cls, 64);
  if (_wcsnicmp(cls, L"edit", 4) != 0 && _wcsnicmp(cls, L"richedit", 8) != 0) return false;
  UINT msg = 0;
  LPARAM lp = 0;
  switch (c.vk) {
    case 'A': msg = EM_SETSEL; lp = -1; break;
    case 'C': msg = WM_COPY; break;
    case 'X': msg = WM_CUT; break;
    case 'V': msg = WM_PASTE; break;
    case 'Z': msg = WM_UNDO; break;
    default: return false;
  }
  DWORD_PTR rc = 0;
  return SendMessageTimeoutW(w, msg, 0, lp, SMTO_ABORTIFHUNG, 200, &rc) != 0;
}

Res<void> msg_chord_attached(HWND top, const KeyChord& c) {
  DWORD pid = 0;
  const DWORD tid = GetWindowThreadProcessId(top, &pid);
  const DWORD me = GetCurrentThreadId();
  HWND f = GetParent(top) || GetAncestor(top, GA_ROOT) != top ? top : focus_target(top);
  if (edit_chord(f, c)) return {};
  if (!AttachThreadInput(me, tid, TRUE)) return fail(E_DENIED, "cannot attach to the target input queue");
  BYTE state[256]{};
  GetKeyboardState(state);
  BYTE saved[256];
  std::memcpy(saved, state, sizeof state);
  for (u16 m : c.mods) {
    state[m] |= 0x80;
    if (m == VK_CONTROL) state[VK_LCONTROL] |= 0x80;
    if (m == VK_SHIFT) state[VK_LSHIFT] |= 0x80;
    if (m == VK_MENU) state[VK_LMENU] |= 0x80;
  }
  SetKeyboardState(state);
  const UINT down = (std::find(c.mods.begin(), c.mods.end(), VK_MENU) != c.mods.end()) ? WM_SYSKEYDOWN : WM_KEYDOWN;
  const UINT up = down == WM_SYSKEYDOWN ? WM_SYSKEYUP : WM_KEYUP;
  const bool ok = pmsg(f, down, c.vk, key_lp(c.vk, false, c.ext));
  pmsg(f, up, c.vk, key_lp(c.vk, true, c.ext));
  // 按键消息是投递的：要等目标线程取走并处理完（含 TranslateMessage 派生的字符）再还原键盘状态，
  // 否则它翻译时修饰键已松开，Ctrl+A 会变成字母 a。每次同步往返都意味着线程回到了消息循环。
  for (int i = 0; i < 4; ++i) {
    DWORD_PTR r = 0;
    if (!SendMessageTimeoutW(f, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &r)) break;
  }
  sleep_us(500);
  SetKeyboardState(saved);
  AttachThreadInput(me, tid, FALSE);
  return ok ? Res<void>{} : fail(E_WIN32, "cannot post chord");
}

Res<void> si_move(geo::PointI p) {
  if (!SetCursorPos(p.x, p.y)) return fail(E_WIN32, "SetCursorPos failed");
  return {};
}

Res<void> si_move_smooth(geo::PointI from, geo::PointI to, int duration_ms) {
  if (duration_ms <= 0) return si_move(to);
  const u64 t0 = now_us(), total = static_cast<u64>(duration_ms) * 1000;
  for (;;) {
    const u64 el = now_us() - t0;
    if (el >= total) break;
    double t = static_cast<double>(el) / static_cast<double>(total);
    t = t * t * (3 - 2 * t);
    SetCursorPos(static_cast<int>(std::lround(from.x + (to.x - from.x) * t)), static_cast<int>(std::lround(from.y + (to.y - from.y) * t)));
    sleep_us(4000);
  }
  return si_move(to);
}

Res<void> si_click(geo::PointI p, Button b, int count) {
  if (auto r = si_move(p); !r) return r;
  const DWORD dn = b == Button::Right ? MOUSEEVENTF_RIGHTDOWN : b == Button::Middle ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_LEFTDOWN;
  const DWORD up = b == Button::Right ? MOUSEEVENTF_RIGHTUP : b == Button::Middle ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_LEFTUP;
  for (int i = 0; i < std::max(1, count); ++i) {
    INPUT in[2] = {mouse_in(dn), mouse_in(up)};
    if (auto r = send(in, 2); !r) return r;
    if (i + 1 < count) sleep_us(30000);
  }
  return {};
}

Res<void> si_drag(geo::PointI from, geo::PointI to, Button b, int duration_ms) {
  if (auto r = si_move(from); !r) return r;
  const DWORD dn = b == Button::Right ? MOUSEEVENTF_RIGHTDOWN : b == Button::Middle ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_LEFTDOWN;
  const DWORD up = b == Button::Right ? MOUSEEVENTF_RIGHTUP : b == Button::Middle ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_LEFTUP;
  INPUT d = mouse_in(dn);
  if (auto r = send(&d, 1); !r) return r;
  sleep_us(8000);
  (void)si_move_smooth(from, to, std::max(duration_ms, 30));
  sleep_us(8000);
  INPUT u = mouse_in(up);
  return send(&u, 1);
}

Res<void> si_scroll(geo::PointI p, int v, int h) {
  if (auto r = si_move(p); !r) return r;
  if (v) {
    INPUT in = mouse_in(MOUSEEVENTF_WHEEL, static_cast<DWORD>(v * WHEEL_DELTA));
    if (auto r = send(&in, 1); !r) return r;
  }
  if (h) {
    INPUT in = mouse_in(MOUSEEVENTF_HWHEEL, static_cast<DWORD>(h * WHEEL_DELTA));
    if (auto r = send(&in, 1); !r) return r;
  }
  return {};
}

Res<void> si_text(const std::wstring& text) {
  std::vector<INPUT> in;
  in.reserve(text.size() * 2);
  for (wchar_t ch : text) {
    if (ch == L'\r') continue;
    if (ch == L'\n') {
      in.push_back(key_in(VK_RETURN, false, false));
      in.push_back(key_in(VK_RETURN, true, false));
      continue;
    }
    INPUT d{};
    d.type = INPUT_KEYBOARD;
    d.ki.wScan = ch;
    d.ki.dwFlags = KEYEVENTF_UNICODE;
    INPUT u = d;
    u.ki.dwFlags |= KEYEVENTF_KEYUP;
    in.push_back(d);
    in.push_back(u);
  }
  size_t off = 0;
  while (off < in.size()) {
    const UINT n = static_cast<UINT>(std::min<size_t>(in.size() - off, 256));
    if (auto r = send(in.data() + off, n); !r) return r;
    off += n;
  }
  return {};
}

Res<void> si_key(const KeyChord& c) {
  std::vector<INPUT> in;
  for (u16 m : c.mods) in.push_back(key_in(m, false, is_extended(m)));
  in.push_back(key_in(c.vk, false, c.ext));
  in.push_back(key_in(c.vk, true, c.ext));
  for (auto it = c.mods.rbegin(); it != c.mods.rend(); ++it) in.push_back(key_in(*it, true, is_extended(*it)));
  return send(in.data(), static_cast<UINT>(in.size()));
}

bool force_foreground(HWND h) {
  if (GetForegroundWindow() == h) return true;
  if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
  const DWORD me = GetCurrentThreadId();
  HWND fg = GetForegroundWindow();
  const DWORD fgt = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
  const DWORD tgt = GetWindowThreadProcessId(h, nullptr);
  if (fgt && fgt != me) AttachThreadInput(me, fgt, TRUE);
  if (tgt && tgt != me) AttachThreadInput(me, tgt, TRUE);
  AllowSetForegroundWindow(ASFW_ANY);
  BringWindowToTop(h);
  SetForegroundWindow(h);
  if (fgt && fgt != me) AttachThreadInput(me, fgt, FALSE);
  if (tgt && tgt != me) AttachThreadInput(me, tgt, FALSE);
  const u64 end = now_us() + 40000;
  while (GetForegroundWindow() != h && now_us() < end) sleep_us(300);
  return GetForegroundWindow() == h;
}

Hop::Hop(HWND target, bool restore_cursor) : restore_cursor_(restore_cursor) {
  prev_fg_ = GetForegroundWindow();
  GetCursorPos(&prev_cursor_);
  if (prev_fg_ == target) {
    ok_ = true;
    return;
  }
  changed_ = true;
  ok_ = force_foreground(target);
}

Hop::~Hop() {
  if (restore_cursor_) SetCursorPos(prev_cursor_.x, prev_cursor_.y);
  if (changed_ && prev_fg_ && IsWindow(prev_fg_)) force_foreground(prev_fg_);
}

// 控件处理 WM_LBUTTONDOWN / BM_CLICK 时会自己 SetFocus，把整个窗口顶成系统前台；
// 持有前台锁期间，这种由目标进程自己发起的激活会被系统拒绝，点击照常生效。
namespace {
std::atomic<u64> g_locked{0}, g_denied{0}, g_restored{0};
}

ShieldStats shield_stats() { return {g_locked.load(), g_denied.load(), g_restored.load()}; }

FocusShield::FocusShield(HWND target, bool active) {
  if (!active || !target) return;
  prev_ = GetForegroundWindow();
  if (!prev_) return;
  GetWindowThreadProcessId(target, &pid_);
  locked_ = LockSetForegroundWindow(LSFW_LOCK) != 0;
  (locked_ ? g_locked : g_denied).fetch_add(1);
}

bool FocusShield::disturbed() const {
  if (!prev_) return false;
  HWND now = GetForegroundWindow();
  if (!now || now == prev_ || !IsWindow(prev_)) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(now, &pid);
  return pid && pid == pid_;
}

FocusShield::~FocusShield() {
  if (!prev_) return;
  if (locked_) LockSetForegroundWindow(LSFW_UNLOCK);
  // 兜底：目标没被前台锁拦住（例如 UIA 在目标进程里自己激活），且用户当时的窗口还在，就还回去。
  if (disturbed() && force_foreground(prev_)) g_restored.fetch_add(1);
}

}  // namespace dx::input
