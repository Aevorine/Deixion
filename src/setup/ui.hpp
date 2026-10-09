#pragma once
#include <windows.h>

#include "installer.hpp"

namespace dxsetup {

enum class Mode { Install, Uninstall };

// 图形界面：选位置、选项、进度、完成。返回进程退出码。
int run_ui(HINSTANCE inst, Mode mode, const Payload* pl, Options opt, bool installed_before);

// 界面语言：中文系统用中文，其余用英文。
bool use_chinese();

}  // namespace dxsetup
