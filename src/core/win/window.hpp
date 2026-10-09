#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "core/base/json.hpp"
#include "core/geo/meridian.hpp"

namespace dx::win {

struct WinInfo {
  u64 hwnd{0};
  u32 pid{0};
  u32 tid{0};
  std::string title;
  std::string cls;
  std::string exe;
  geo::RectI frame;
  geo::RectI client;
  u32 dpi{96};
  bool visible{false};
  bool minimized{false};
  bool maximized{false};
  bool cloaked{false};
  bool topmost{false};
  bool foreground{false};
  int z{0};
};

struct ListOpts {
  bool include_hidden{false};
  bool include_untitled{false};
  std::string filter;
};

HWND to_hwnd(u64 v);
std::vector<WinInfo> list_windows(const ListOpts& o = {});
std::optional<WinInfo> info(HWND h);
std::string exe_name_of_pid(u32 pid);
Json to_json(const WinInfo& w);

// 目标说明：hwnd:0x1A2B | title:记事本 | exe:chrome.exe | class:Notepad | pid:1234 | active | screen；
// 纯文本按标题模糊匹配，其次按进程名。
Res<HWND> resolve(std::string_view spec);
HWND resolve_or_screen(std::string_view spec, bool& is_screen);

geo::Frame client_frame(HWND h);
geo::Frame window_frame(HWND h);
geo::Frame screen_frame();
geo::RectI virtual_screen();
u32 dpi_of(HWND h);

HWND deepest_child_at(HWND top, geo::PointI screen_pt);
bool is_own_process_window(HWND h);

}  // namespace dx::win
