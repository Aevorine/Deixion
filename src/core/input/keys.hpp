#pragma once
#include <string>
#include <vector>

#include "core/base/types.hpp"

namespace dx::input {

struct KeyChord {
  std::vector<u16> mods;  // VK_CONTROL / VK_SHIFT / VK_MENU / VK_LWIN
  u16 vk{0};
  bool ext{false};
  std::string text;
  bool has_mods() const { return !mods.empty(); }
  bool only_shift() const { return mods.size() == 1 && mods[0] == 0x10; }
};

// "ctrl+shift+s" / "enter" / "f5" / "alt+tab" / "win+r"
Res<KeyChord> parse_chord(std::string_view s);
bool is_extended(u16 vk);
u16 scan_of(u16 vk);

}  // namespace dx::input
