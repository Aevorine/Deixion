#pragma once
#include "core/base/json.hpp"

namespace dx::app {

// 接入 Claude Code：检测 CLI、是否已注册 MCP、技能是否已安装；一键注册 / 移除。
namespace claude {
Json status(size_t ipc_clients);
Res<Json> install();
Res<Json> remove();
Json config_snippet();
}  // namespace claude

}  // namespace dx::app
