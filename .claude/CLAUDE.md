# Deixion — 项目规则
> 只放本项目专属且稳定的内容。现在做到哪 → `.claude/SPEC.md`；背景与用法 → `README.md`。

## 这是什么
Windows 上让 Claude Code 任何模型后台、毫秒级操控各类应用的工具：C++23 内核（UIA/截图/输入/经验库/回滚）+ WebView2 界面 + `deixion-cli`（含 MCP stdio 服务）。

## 命令
| 做什么 | 命令 |
|---|---|
| 配置 | `cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_RC_COMPILER=llvm-windres` |
| 构建 | `cmake --build build` |
| 目标 | `Deixion.exe`（界面）、`deixion-cli.exe`、`deixion-setup.exe`（安装器）、`dx-testapp.exe`（测试靶子，`EXCLUDE_FROM_ALL`） |

## 硬约束
- 工具链是 llvm-mingw clang（UCRT、静态链接），没有 MSVC；不引入需要运行库安装的依赖，因为目标是任何 Windows 机器下载即用。
- 更新器契约固定：Release 必须有 `Deixion-Setup-x64.exe` 与 `SHA256SUMS.txt`，安装器必须接受 `/S /UPDATE /RELAUNCH`；改名会让已发布版本无法自更新。
- 安装位置可选，但最终一定落在名为 `Deixion` 的子目录下（用户要求）。
- 界面：中文宋体小四/四号，英文与标点 Times New Roman，公式 KaTeX；页面里不写功能名、说明、提醒文字；图标悬停显示功能名；全站风格统一、护眼、内容填满。
- 界面与托盘、原生提示都是中文 / English 双语：设置 `language`（auto / zh / en），auto 跟随 Windows 显示语言（中文系统显示中文，其余显示英文）。界面代码里直接写中文并包 `t('…')`，英文在 `ui/js/i18n/en/*.js`（以中文原文为键）；模块顶层不得调用 `t()`；原生用 `i18n::pick(lang, zh, en)`。改界面后 `node tools/i18n-check.mjs` 必须通过。
- 后台模式不得抢焦点、不得移动用户的真实光标；前台模式才显示可见操作。
- 密码、密钥、签名材料不进仓库、不进日志，只放在仓库之外（本机位置见 `CLAUDE.local.md`）。
- 只做端到端验证，不写单元测试/集成测试（用户要求）。

## 明确不做的范围
- 不改原有有效功能的行为；增强只增不改。

## Agent skills

### Issue tracker

Issues and specs are tracked as GitHub Issues via the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Default five-role vocabulary (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `CONTEXT.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.
