#pragma once
#include <functional>

#include "core/base/json.hpp"

namespace dx::mcp {

using Call = std::function<Res<Json>(std::string_view method, const Json& params)>;

// 标准输入/输出上的 MCP 服务（换行分隔的 JSON-RPC 2.0）。阻塞到标准输入关闭。
int run_stdio(const Call& call);

Json tools_list();
std::string instructions();

}  // namespace dx::mcp
