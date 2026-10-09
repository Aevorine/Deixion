#pragma once
// 界面语言：中文 / English。设置里 language = auto | zh | en；auto 跟随系统界面语言：
// 系统语言是中文就显示中文，其余一律显示英文。托盘菜单、提示和原生对话框都从这里取语言。
#include <windows.h>

#include <string>

namespace dx::i18n {

enum class Lang { Zh, En };

inline Lang system() { return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE ? Lang::Zh : Lang::En; }

inline Lang resolve(const std::string& setting) {
  if (setting == "zh") return Lang::Zh;
  if (setting == "en") return Lang::En;
  return system();
}

inline const char* code(Lang l) { return l == Lang::Zh ? "zh" : "en"; }

inline const wchar_t* pick(Lang l, const wchar_t* zh, const wchar_t* en) { return l == Lang::Zh ? zh : en; }
inline const char* pick(Lang l, const char* zh, const char* en) { return l == Lang::Zh ? zh : en; }

}  // namespace dx::i18n
