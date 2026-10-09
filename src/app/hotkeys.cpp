#include "app/hotkeys.hpp"

#include "core/input/keys.hpp"

namespace dx::app {

namespace {
struct Def {
  const char* name;
  int id;
};
constexpr Def kDefs[] = {{"toggle", HK_TOGGLE}, {"mode", HK_MODE}, {"pause", HK_PAUSE}, {"undo", HK_UNDO}, {"shot", HK_SHOT}, {"stop", HK_STOP}};
}  // namespace

void Hotkeys::clear(HWND hwnd) {
  for (int id : ids_) UnregisterHotKey(hwnd, id);
  ids_.clear();
  st_.clear();
}

std::vector<HotkeyStatus> Hotkeys::apply(HWND hwnd, const Json& chords) {
  clear(hwnd);
  for (const auto& d : kDefs) {
    HotkeyStatus s;
    s.name = d.name;
    s.chord = chords[d.name].as_str();
    auto c = s.chord.empty() ? Res<input::KeyChord>(fail(E_BAD_ARG, "empty")) : input::parse_chord(s.chord);
    if (c && !c->mods.empty()) {
      UINT mods = MOD_NOREPEAT;
      for (u16 m : c->mods) {
        if (m == VK_CONTROL) mods |= MOD_CONTROL;
        else if (m == VK_SHIFT) mods |= MOD_SHIFT;
        else if (m == VK_MENU) mods |= MOD_ALT;
        else if (m == VK_LWIN) mods |= MOD_WIN;
      }
      s.ok = RegisterHotKey(hwnd, d.id, mods, c->vk) != 0;
      if (s.ok) ids_.push_back(d.id);
    }
    st_.push_back(s);
  }
  return st_;
}

Json Hotkeys::status_json() const {
  Json a = Json::array();
  for (const auto& s : st_) {
    Json j = Json::object();
    j.set("name", s.name).set("chord", s.chord).set("ok", s.ok);
    a.push(std::move(j));
  }
  return a;
}

}  // namespace dx::app
