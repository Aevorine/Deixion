#pragma once
#include <string>

#include "core/base/types.hpp"
#include "core/capture/capture.hpp"

namespace dx::app::sys {

Res<void> set_autostart(bool on);
bool autostart_enabled();
Res<void> copy_text(HWND owner, const std::string& utf8);
Res<void> copy_image(HWND owner, const cap::Image& im);
void open_path(const std::wstring& path);
void open_url(const std::string& url);
bool system_dark();
// 窗口标题栏随主题着色（Windows 11），旧系统只切深浅。
void style_caption(HWND hwnd, bool dark, u32 bg_rgb, u32 fg_rgb);
// 让原生弹出菜单也跟随系统深浅色。
void allow_dark_menus();

}  // namespace dx::app::sys
