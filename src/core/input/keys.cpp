#include "core/input/keys.hpp"

#include <windows.h>

#include <unordered_map>

#include "core/base/text.hpp"

namespace dx::input {
namespace {
const std::unordered_map<std::string, u16>& names() {
  static const std::unordered_map<std::string, u16> m = {
      {"enter", VK_RETURN},   {"return", VK_RETURN}, {"tab", VK_TAB},        {"esc", VK_ESCAPE},      {"escape", VK_ESCAPE},
      {"space", VK_SPACE},    {"backspace", VK_BACK}, {"bksp", VK_BACK},     {"delete", VK_DELETE},   {"del", VK_DELETE},
      {"insert", VK_INSERT},  {"ins", VK_INSERT},     {"home", VK_HOME},     {"end", VK_END},         {"pageup", VK_PRIOR},
      {"pgup", VK_PRIOR},     {"pagedown", VK_NEXT},  {"pgdn", VK_NEXT},     {"up", VK_UP},           {"down", VK_DOWN},
      {"left", VK_LEFT},      {"right", VK_RIGHT},    {"capslock", VK_CAPITAL}, {"numlock", VK_NUMLOCK}, {"scrolllock", VK_SCROLL},
      {"printscreen", VK_SNAPSHOT}, {"prtsc", VK_SNAPSHOT}, {"pause", VK_PAUSE}, {"apps", VK_APPS},     {"menu", VK_APPS},
      {"plus", VK_OEM_PLUS},  {"minus", VK_OEM_MINUS}, {"comma", VK_OEM_COMMA}, {"period", VK_OEM_PERIOD}, {"slash", VK_OEM_2},
      {"backslash", VK_OEM_5}, {"semicolon", VK_OEM_1}, {"quote", VK_OEM_7}, {"backtick", VK_OEM_3}, {"lbracket", VK_OEM_4},
      {"rbracket", VK_OEM_6}, {"volumeup", VK_VOLUME_UP}, {"volumedown", VK_VOLUME_DOWN}, {"mute", VK_VOLUME_MUTE},
      {"playpause", VK_MEDIA_PLAY_PAUSE}, {"nexttrack", VK_MEDIA_NEXT_TRACK}, {"prevtrack", VK_MEDIA_PREV_TRACK},
  };
  return m;
}

bool mod_of(const std::string& t, u16& vk) {
  if (t == "ctrl" || t == "control") vk = VK_CONTROL;
  else if (t == "shift") vk = VK_SHIFT;
  else if (t == "alt" || t == "option") vk = VK_MENU;
  else if (t == "win" || t == "meta" || t == "cmd" || t == "super" || t == "windows") vk = VK_LWIN;
  else return false;
  return true;
}
}  // namespace

bool is_extended(u16 vk) {
  switch (vk) {
    case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_INSERT: case VK_DELETE: case VK_LWIN: case VK_RWIN: case VK_APPS: case VK_DIVIDE: case VK_NUMLOCK: case VK_SNAPSHOT:
      return true;
    default: return false;
  }
}

u16 scan_of(u16 vk) { return static_cast<u16>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)); }

Res<KeyChord> parse_chord(std::string_view spec) {
  KeyChord c;
  std::vector<std::string> parts;
  std::string cur;
  for (char ch : spec) {
    if (ch == '+' && !cur.empty()) {
      parts.push_back(cur);
      cur.clear();
    } else if (ch != ' ') {
      cur.push_back(ch);
    }
  }
  if (!cur.empty()) parts.push_back(cur);
  if (parts.empty()) return fail(E_BAD_ARG, "empty key spec");
  for (size_t i = 0; i < parts.size(); ++i) {
    const std::string t = text::lower(parts[i]);
    u16 vk = 0;
    if (i + 1 < parts.size()) {
      if (!mod_of(t, vk)) return fail(E_BAD_ARG, "unknown modifier: " + parts[i]);
      c.mods.push_back(vk);
      continue;
    }
    if (mod_of(t, vk)) {
      c.vk = vk;
    } else if (auto it = names().find(t); it != names().end()) {
      c.vk = it->second;
    } else if (t.size() >= 2 && t[0] == 'f' && std::isdigit(static_cast<unsigned char>(t[1]))) {
      const int n = std::atoi(t.c_str() + 1);
      if (n < 1 || n > 24) return fail(E_BAD_ARG, "bad function key: " + parts[i]);
      c.vk = static_cast<u16>(VK_F1 + n - 1);
    } else {
      const std::wstring w = text::widen(parts[i]);
      if (w.size() != 1) return fail(E_BAD_ARG, "unknown key: " + parts[i]);
      const SHORT r = VkKeyScanW(w[0]);
      if (r == -1) return fail(E_BAD_ARG, "no key produces: " + parts[i]);
      c.vk = static_cast<u16>(r & 0xFF);
      if ((r >> 8) & 1) c.mods.push_back(VK_SHIFT);
      if ((r >> 8) & 2) c.mods.push_back(VK_CONTROL);
      if ((r >> 8) & 4) c.mods.push_back(VK_MENU);
      c.text = parts[i];
    }
  }
  c.ext = is_extended(c.vk);
  return c;
}

}  // namespace dx::input
