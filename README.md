# Deixion

[English](README.en.md) · 中文

> **五岁小孩版**：电脑上有很多软件。Claude Code 想帮你点一点、填一填，但它平时只能“看截图、猜位置”，又慢又容易点错。Deixion 给 Claude Code 装上一双手：你照常看视频、刷网页，它在后台替 Claude 点按、输入、读取别的软件，**不移动你的鼠标，也不抢你的键盘焦点**。

Deixion 是一个 Windows 桌面工具，由三部分组成：

- **C++23 内核**：UI Automation 元素树、截图、输入注入、经验库、回滚日志。
- **WebView2 桌面界面**：托盘常驻，提供总览、定位、元素、操作、经验、日志、性能、接入、指南、设置等页面。
- **`deixion-cli`**：命令行工具，内含 MCP（stdio）服务，供 Claude Code 调用。

## 目录

- [工作方式](#工作方式)
- [两种模式](#两种模式)
- [策略与经验库](#策略与经验库)
- [经纬定位 Meridian](#经纬定位-meridian)
- [MCP 工具](#mcp-工具)
- [命令行 deixion-cli](#命令行-deixion-cli)
- [安装](#安装)
- [更新](#更新)
- [接入 Claude Code](#接入-claude-code)
- [设置项](#设置项)
- [托盘与快捷键](#托盘与快捷键)
- [数据、日志与崩溃恢复](#数据日志与崩溃恢复)
- [性能](#性能)
- [从源码构建](#从源码构建)
- [许可证](#许可证)
- [已知限制与待确认](#已知限制与待确认)

## 工作方式

```text
Claude Code ──stdio(MCP)──► deixion-cli.exe ──命名管道──► Deixion.exe（引擎）──► 目标窗口
                                 │                              ├─ UI Automation（元素树）
                                 │                              ├─ 窗口消息 / 真实输入
                                 └─ 未运行时：拉起托盘版          ├─ 截图（BitBlt / PrintWindow）
                                    或在进程内直接运行引擎         └─ 经验库 · 回滚 · 日志
```

- `deixion-cli mcp` 运行时，如果 Deixion 没有在运行，它会以 `Deixion.exe --tray` 拉起托盘版（设置环境变量 `DEIXION_NO_APP` 可关闭）。其他子命令在没有运行中的 Deixion 时，直接在进程内跑引擎（`--inproc` 可强制）。
- 管道名为 `\\.\pipe\Deixion-v1-<当前用户>`，安全描述符只放行当前用户与 SYSTEM。
- 单实例：同一登录会话内只能运行一个 Deixion；再次启动会唤出已有窗口。`--tray` 只驻留托盘，`--quit` 通知已运行的实例退出。

## 两种模式

| | 后台（默认） | 前台 |
|---|---|---|
| 真实光标 | 不动 | 会移动，动作可见 |
| 焦点 | 不抢 | 窗口会被前置 |
| 输入通道 | UI Automation 模式与窗口消息（按动作与窗口类型排序，见下节） | 真实鼠标 / 键盘（SendInput） |
| 操作可视化 | 无 | 涟漪、控件高亮框、动作标签（可关） |
| 鼠标移动速度 | 无 | 由 `speed` 决定 |

- **后台模式**下，部分窗口不接收普通窗口消息（例如 XAML / UWP 的内容窗口）。引擎会识别这类窗口，并把 UI Automation 排到消息通道前面。
- **前台守护**：控件处理点击时通常会自己调用 `SetFocus`，把整个窗口顶到最前。所以后台动作执行期间，Deixion 会持有 Windows 的前台锁（`LockSetForegroundWindow`），目标窗口无法越过你正在使用的窗口。经典 Win32 控件上的 UI Automation 模式是在目标进程内部激活窗口的，会绕过这把锁，所以对这类控件，引擎直接发等价的消息：`Button` 类的按钮、复选框、单选框用 `BM_CLICK`，`Edit` / `RichEdit` 编辑框用 `WM_SETTEXT`。如果某个程序仍然抢到了前台，动作返回后会立刻把你原来的窗口还回去，结果里带一条 `focus` 说明，经验库也会降低该通道的收益，之后更少选它。`status` 会给出计数（`focus_shield`：`locked`、`denied`、`restored`）。用测试靶子实测：完整 MCP 会话跑 5 次，同时以亚毫秒间隔轮询系统前台窗口，靶子从未成为前台窗口。其他框架（WPF、Electron、游戏）尚未实测。
- **允许短暂切前台兜底**（`allow_hop`，默认关）：只有开启后，后台动作才可能短暂把窗口切到前台。关闭时，需要前台的动作会直接返回错误，不会偷偷切换窗口。
- 单次调用可以用 `mode: "background"` 把前台模式降为后台，但不能在后台模式里用单次调用升为前台。

## 策略与经验库

每个动作有一条有序的**通道**（策略）列表。前台模式下，多数动作的首选通道是 `real`（真实输入）。后台模式下的排序如下（源码：`src/core/engine/actions.cpp`）：

| 动作 | 后台通道（靠前的先试） |
|---|---|
| `click` | `uia` → `msg`（窗口敌对时 `msg` 降为兜底）→ `uia_hit` → `hop` |
| `type` | `msg_char` → `uia_set` → `hop`（窗口敌对时前两者互换） |
| `set_value` | `uia_set` → `msg_settext` |
| `key` | `msg_key` 或 `msg_chord` → `hop` |
| `scroll` | `msg` → `uia_scroll` → `hop` |
| `drag` | `msg` → `hop` |
| `window` / `launch` | Win32 窗口 API / Shell 启动 |

对经典 Win32 控件，`uia` 点击与 `uia_set` / `set_value` 写入实际以 `BM_CLICK` 和 `WM_SETTEXT` 完成（见上面的前台守护）。`hop` 只在 `allow_hop` 开启时存在。非兜底通道会按下面的经验库重新排序；标记为兜底的通道只在前面的通道都失败后才会尝试。

**经验库**（`experience.dxl`）为每个（应用、角色、动作、通道）记一条统计，每次更新时旧统计乘以 0.985 衰减。候选通道用 UCB 排序：

$$
\text{score}=\frac{R+1.5\,p_i}{n+1.5}+0.35\sqrt{\frac{\ln(N+2)}{n+1.5}}
$$

其中 $n$ 为衰减后的样本数，$R$ 为衰减后的奖励和，$p_i=0.9-0.1\,i$ 是按默认顺序给的先验（$i$ 为候选序号），$N$ 为各候选样本数之和。奖励的取值：未进行校验时为 $0.9/(1+t/6)$；校验确认生效时为 $\max(0.3,\,1/(1+t/6))$；校验未能确认时为 $0.45$（$t$ 为耗时，毫秒）。经验库还会学习“反应时间”和“界面静止时长”，用于设定校验与 `wait` 的等待窗口。

## 经纬定位 Meridian

Meridian 把任意窗口的客户区当成一张单位正方形：横向坐标为经度 $\lambda$，纵向坐标为纬度 $\varphi$，左上角为 $(0,0)$，右下角为 $(1,1)$。同一个 $(\lambda,\varphi)$ 在任何窗口大小、任何 DPI 下指向同一处内容，所以模型只需要记住比例位置，不必记像素。

设客户区左上角为 $(x_0,y_0)$，宽高为 $W\times H$（物理像素），像素 $(x,y)$ 与比例坐标互换如下：

$$
\lambda=\operatorname{clamp}_{[0,1]}\!\left(\frac{x-x_0+\tfrac12}{W}\right),\qquad
\varphi=\operatorname{clamp}_{[0,1]}\!\left(\frac{y-y_0+\tfrac12}{H}\right)
$$

$$
x=x_0+\operatorname{clamp}_{[0,\,W-1]}\!\left(\lfloor \lambda W\rfloor\right),\qquad
y=y_0+\operatorname{clamp}_{[0,\,H-1]}\!\left(\lfloor \varphi H\rfloor\right)
$$

**格网编码。** 每个 $\lambda$ 和 $\varphi$ 先量化为 16 位整数 $q=\min(\lfloor 65536\cdot v\rfloor,\,65535)$，再把两者的位交织成 Morton 码 $K=(\operatorname{spread}(q_\lambda)\ll 1)\,|\,\operatorname{spread}(q_\varphi)$，左移 3 位得到 35 位的 $K_{35}$。编码 $L$ 位（$1\le L\le 6$）取 $K_{35}$ 的高 $5L$ 位，每 5 位映射为一个字符，字母表为 `0123456789ABCDEFGHJKMNPQRSTVWXYZ`（不含 I、L、O、U；解码时大小写不敏感，并把 I、L 当作 1、O 当作 0）。前缀就是外层格子，可以由粗到细逐级定位。

**精度。** 第 $L$ 级格子在横向有 $n_x=\lceil 5L/2\rceil$ 位、纵向有 $n_y=\lfloor 5L/2\rfloor$ 位，格子尺寸为 $2^{-n_x}\times 2^{-n_y}$。系统选取满足 $2^{n_x}\ge W$ 且 $2^{n_y}\ge H$ 的最小 $L$，此时一个格子不大于一个像素。按公式推算，1920×1080 的窗口需要 5 位编码（$n_x=13$、$n_y=12$，格子约 0.23 × 0.26 px）。这是公式推算结果，不是实测。

## MCP 工具

Claude Code 通过 `deixion-cli mcp` 调用以下 18 个工具。`window` 可以写窗口标题片段、`exe:name.exe`、`class:Name`、`pid:N`、`hwnd:0x…`、`active`，或 `screen`（整个屏幕）。点位可以用 `element`（如 `e12`，来自 `elements` 或带 `elements:true` 的截图）、`find`（模糊查找）、`at`（`"lam,phi"`）、`code`（格网编码）、`px`（客户区像素）或 `screen`（屏幕像素）。

| 工具 | 作用 | 关键参数 |
|---|---|---|
| `windows` | 列出顶层窗口（句柄、exe、标题、客户区大小） | `filter` |
| `screenshot` | 截图（JPEG，带经纬网格；可画编号框） | `window`、`region{a,b}`、`code`、`grid`、`elements`、`marks`、`max_dim` |
| `elements` | 列出 UI Automation 元素（id、角色、名称、中心点、可用模式） | `window`、`query`、`role`、`interactive`、`limit`（默认 150）、`refresh` |
| `find` | 按名称 / 角色 / AutomationId 模糊查找元素 | `window`、`text`、`role`、`aid`、`limit`（默认 5） |
| `locate` | 查询某一点上的元素与格网编码 | `window` + 点 |
| `click` | 点击（后台优先 UIA 模式，不动光标） | `window`、点、`button`、`count`（1–3）、`hover` |
| `type` | 输入文字（可整体替换） | `window`、`text`、`replace`、点 |
| `key` | 按键、组合键或空格分隔的序列 | `window`、`keys`、`repeat` |
| `scroll` | 滚动（`dy>0` 向下，`dx>0` 向右，单位为滚轮格） | `window`、`dy`、`dx`、点 |
| `drag` | 拖拽 | `window`、`from`、`to`、`button`、`steps` |
| `set_value` | 通过 UIA 直接设值，不打字（可撤销） | `window`、`value`、点 |
| `read` | 读取元素的值、文本或勾选状态 | `window`、点 |
| `window_op` | 窗口管理 | `window`、`op`（focus / minimize / maximize / restore / close / move / resize / topmost）、`rect` |
| `launch` | 启动程序、文档或网址（后台不抢焦点）。不会启动 Deixion 自己的程序；除非用户打开 `allow_shell_launch`，命令行、脚本宿主和解释器（`cmd`、PowerShell、`wscript`、Python、Node、`.bat`、`.ps1`、`.url` 等）以及 `http`、`https`、`mailto`、`ms-settings` 之外的链接协议（含 `file:`）也都不会启动。检查针对还原后的真实文件名：引号、`file:` 网址、短文件名、末尾的点会先还原，`explorer.exe` 还会检查参数里点名的程序 | `path`、`args`、`cwd`、`wait_window_ms`（默认 3000） |
| `wait` | 不靠固定 sleep 等待 | `for`（settle / element / gone / window）、`window`、`find`、`timeout_ms`（默认 5000）、`quiet_ms` |
| `batch` | 一次调用执行多步（一次往返） | `steps`、`defaults`、`stop_on_error`（默认 true，最多 200 步） |
| `undo` | 回滚最近 N 次可逆动作 | `count`（1–50）、`id` |
| `status` | 引擎状态；`detail` 可取 journal / experience / perf / log | `detail` |

动作参数写进日志与回滚记录时，输入的文字只记字符数。回滚 `set_value` 时需要保存旧值，这部分数据保存在回滚日志中。

**权限边界**：`batch` 的每一步只能是上表中的动作与查询方法；修改设置、清空日志、重置经验库、紧急停止这类用户专属的控制不能经由 `batch` 调用，所以暂停与「允许短暂切前台」这两个开关只有用户本人能改。输入类动作（点击、输入、按键、滚动、拖拽、设值）也不能操作 Deixion 自己的窗口，否则模型可以直接点界面里的开关；截图与读取元素这类被动查询不受限制。命名管道客户端连上后会核对服务端进程的账户，与当前账户不一致就断开，防止多用户机器上别的账户抢先建同名管道。该核对的失败分支需要第二个 Windows 账户才能验证，尚未实测。

## 命令行 deixion-cli

```text
deixion-cli mcp                          在 stdin/stdout 上运行 MCP 服务（供 Claude Code 调用）
deixion-cli call <方法> [json|@文件]      调用任意引擎方法
deixion-cli windows [过滤词]              列出窗口
deixion-cli shot [窗口] [-o 文件.jpg] [--no-grid] [--elements]
deixion-cli elements <窗口> [查询词]      列出 UI Automation 元素
deixion-cli bench <窗口> [轮数]           各主要路径的延迟基准
deixion-cli status | doctor | version
deixion-cli --inproc …                   不连接正在运行的 Deixion，在本进程运行引擎
```

`doctor` 检查 Windows 版本、CPU 指令集（BMI2 / AVX2 / SSE4.2）、WebView2 运行时、引擎连通性、UI Automation 是否可用，以及 PATH 上是否能找到 Claude Code CLI。WebView2 缺失时，界面不可用，但 MCP 与 CLI 仍可工作。

## 安装

> 安装包由 `tools/pack-payload.mjs` 把程序文件逐个压缩并附 CRC32，嵌入 `Deixion-Setup-x64.exe`；安装时先全部解压校验，再替换。

1. 到 [Releases](https://github.com/Aevorine/Deixion/releases) 下载 `Deixion-Setup-x64.exe`，同一页面的 `SHA256SUMS.txt` 可用来校验。
2. 双击安装，按用户安装，不需要管理员权限。可以选择安装位置，但程序最终一定放在名为 `Deixion` 的文件夹下：选 `D:\Tools` 则装到 `D:\Tools\Deixion`；选 `D:\Deixion` 则保持不变。
3. 默认位置为 `%LOCALAPPDATA%\Programs\Deixion`。已经安装过时，沿用原位置。
4. 缺少 WebView2 运行时时，安装器从 Microsoft 官方地址（`go.microsoft.com`）下载引导程序，校验其 Microsoft 签名后静默安装，需要联网。补装失败时安装仍会完成，但界面不可用；MCP 与 CLI 不受影响。
5. 安装前会先请求正在运行的 Deixion 退出，最多等 10 秒；仍未退出则强制结束它，未保存的内容可能丢失。
6. 安装采用两阶段：先把所有文件解压校验为临时文件，全部成功后才替换旧文件；中途失败则回滚，不会留下新旧混杂的程序。

静默安装参数（安装器源码 `src/setup/main.cpp`）：

| 参数 | 作用 |
|---|---|
| `/S` | 静默安装 |
| `/D=<目录>` | 安装位置（自动追加 `\Deixion`；已以 `Deixion` 结尾则不再追加） |
| `/UPDATE` | 沿用已有安装位置与选项；只有以前接入过 Claude Code 时才刷新接入 |
| `/RELAUNCH` | 安装后以托盘方式启动（`--tray`） |
| `/UNINSTALL` | 卸载 |
| `/PURGE` | 卸载时一并删除用户数据（`%LOCALAPPDATA%\Deixion`，以及便携版的 `data` 目录） |
| `/AUTOSTART=0\|1` | 开机自启；未指定时，全新安装为关，已安装过则沿用现状 |
| `/DESKTOP=0\|1` | 桌面快捷方式；未指定时，全新安装为关，已安装过则沿用现状 |
| `/STARTMENU=0\|1` | 开始菜单快捷方式；未指定时，全新安装为开，已安装过则沿用现状 |
| `/CLAUDE=0\|1` | 接入 Claude Code；未指定时，全新安装为开，更新则沿用现状（以前接入过才刷新） |
| `/LOG=<文件>` | 安装日志（默认 `%TEMP%\Deixion-setup.log`） |

退出码：`0` 成功；`3` 安装包损坏；`4` 目录不可写；`5` 写入失败；`6` 无法关闭正在运行的 Deixion；`1` 其他错误。

**升级与卸载的数据**：安装与升级不删除用户数据。卸载会删除程序文件、快捷方式、自启项、卸载登记和 Claude Code 中的 MCP 注册与 skill，但默认保留用户数据；加 `/PURGE` 才会删除。

## 更新

应用内更新的流程如下（代码：`src/app/updater.cpp`）：

1. **检查**：启动约 20 秒后自动检查（设置项 `check_updates` 默认开），也可在托盘或设置页手动检查。请求 GitHub Releases 的最新版并比较版本号。
2. **下载**：在设置页点「下载」，把 `Deixion-Setup-x64.exe` 下载到数据目录下的 `update\`。只接受 `https` 且域名为 `github.com` 或 `githubusercontent.com` 的地址。
3. **校验**：再下载 `SHA256SUMS.txt`，计算安装包的 SHA-256 并与清单比对。不一致则删除安装包并报错。
4. **安装**：校验通过后，点「安装并重启」（会弹出确认框），以 `/S /UPDATE /RELAUNCH` 启动安装器。安装器会先让正在运行的 Deixion 退出（见上文安装第 5 条），再替换程序文件，最后以托盘方式启动新版。

便携版（exe 旁有 `portable.flag` 文件）不做原地更新，需要手动下载新的压缩包。

## 接入 Claude Code

「接入」页的功能：

- **安装 skill**：把 skill 复制到 `~/.claude/skills/deixion`。skill 文件来自仓库的 `skills/deixion/`，构建时复制到程序旁，并打包进安装器。
- **注册 MCP**：先移除旧的 `deixion` 注册，再执行：

  ```text
  claude mcp add --scope user deixion -- "<安装目录>\deixion-cli.exe" mcp
  ```

- **状态检查**：显示是否找到 Claude Code CLI、MCP 是否已注册、注册路径是否指向当前安装、skill 是否已安装且为最新版。
- **移除**：取消 MCP 注册并删除 skill 目录。

安装器在 `/CLAUDE=1` 时会运行 `Deixion.exe --connect-claude`，卸载时运行 `--disconnect-claude`，执行的是同一套接入逻辑。

如果找不到 `claude` 命令，页面会给出手动注册所需的 JSON 与命令行。注册后需要重启 Claude Code。

## 设置项

设置保存在数据目录的 `settings.json`，写入时先写临时文件再替换，并保留 `.bak` 备份。读取失败时使用备份。

| 键 | 取值（默认） | 含义 |
|---|---|---|
| `mode` | `background` / `foreground`（`background`） | 后台或前台模式，见上文 |
| `allow_hop` | 布尔（`false`） | 允许短暂切前台兜底 |
| `allow_shell_launch` | 布尔（`false`） | 允许 `launch` 启动命令行与脚本宿主。只有用户能改：MCP 工具和 `batch` 都改不了 |
| `speed` | `instant` / `fast` / `smooth`（`fast`） | 前台模式鼠标移动时长：0 ms / 120 ms / 380 ms（拖拽另计） |
| `overlay` | 布尔（`true`） | 前台操作显示轨迹 |
| `verify` | `auto` / `off`（`auto`） | 动作后是否核对界面变化，见下文「校验」 |
| `jpeg_quality` | 30–100（78） | 截图 JPEG 质量 |
| `max_image_dim` | 400–4096（1568） | 截图最长边（像素） |
| `grid_default` | 布尔（`true`） | 截图默认画经纬网格 |
| `log_level` | `debug` / `info` / `warn` / `error`（`info`） | 日志级别 |
| `autostart` | 布尔（`false`） | 开机自启 |
| `check_updates` | 布尔（`true`） | 启动后自动检查更新 |
| `close_to_tray` | 布尔（`true`） | 关闭窗口时缩到托盘 |
| `paused` | 布尔（`false`） | 暂停接收操作 |
| `theme` | `auto` / `light` / `dark`（`auto`） | 界面主题，`auto` 跟随系统 |
| `density` | `compact` / `standard` / `relaxed`（`standard`） | 界面密度 |
| `hotkeys` | 见下表 | 全局快捷键 |

**校验（`verify`）**：开启时，动作成功后引擎会在时限内等待目标窗口的界面事件（由 WinEvent 钩子统计）。如果界面发生了变化，结果记为 `confirmed: true`；否则记为未确认。结果中的 `confirmed` 与 `reaction_us` 字段即来源于此。调用方可以据此判断动作是否真的生效。

**界面风格**：汉字用宋体，西文与标点用 Times New Roman，正文小四（12 pt），标题四号（14 pt）。公式由内置的 KaTeX 渲染。主题、密度见上表；配色为低饱和的青灰蓝，亮色与暗色各一套。

## 托盘与快捷键

**托盘**：左键切换显示与最小化；右键菜单包括：显示 / 最小化窗口、后台模式、前台模式、允许短暂切前台兜底、前台操作显示轨迹、暂停 / 继续接收操作、撤销上一步操作、截图（含经纬网格）到剪贴板、紧急停止当前批处理、接入 Claude Code、检查更新、开机自启、打开数据目录、退出 Deixion。

**默认全局快捷键**（可在设置中修改；若与其他程序冲突，注册会失败并写入日志）：

| 名称 | 默认键 | 作用 |
|---|---|---|
| `toggle` | `Ctrl+Alt+D` | 显示 / 最小化窗口 |
| `mode` | `Ctrl+Alt+M` | 切换后台 / 前台模式 |
| `pause` | `Ctrl+Alt+U` | 暂停 / 继续接收操作 |
| `undo` | `Ctrl+Alt+B` | 撤销上一步 |
| `shot` | `Ctrl+Alt+S` | 截图到剪贴板 |
| `stop` | `Ctrl+Alt+X` | 紧急停止当前批处理 |

## 数据、日志与崩溃恢复

数据目录为 `%LOCALAPPDATA%\Deixion`。exe 旁放一个 `portable.flag` 文件时，数据改放到 `<exe 目录>\data`（便携模式）。

| 路径 | 内容 |
|---|---|
| `settings.json`（及 `.bak`） | 设置 |
| `window.json` | 窗口位置与大小 |
| `store\experience.dxl` | 经验库：每个（应用、角色、动作、通道）一条统计，以及反应时间、界面静止时长等学习到的数值 |
| `store\journal.dxl` | 回滚日志：每个可逆动作的记录与逆操作 |
| `logs\deixion-YYYYMMDD.jsonl` | 日志（JSON Lines），单文件 8 MB 滚动，保留 14 天 |
| `webview\` | WebView2 用户数据 |
| `update\` | 更新下载目录 |

**存储格式与崩溃恢复**（`src/core/store/logstore.*`）：`experience.dxl` 与 `journal.dxl` 都是追加式记录文件。每条记录包含长度、类型、时间戳、载荷和 CRC-32C 校验。打开文件时逐条校验，遇到第一条损坏或截断的记录，就把文件截断到那里（即崩溃或断电留下的“撕裂尾巴”），之前的记录全部保留。压缩时先写临时文件、再 `FlushFileBuffers`，然后原子替换原文件，磁盘上任何时刻都有一份完整可读的文件。回滚日志在内存中保留最近 3000 条。

## 性能

以下为 2026-10-09 在 Intel Core i5-1155G7（Windows 11，构建 26300）上用 `deixion-cli bench` 实测的往返延迟（经命名管道，300 轮，p50）：

| 路径 | p50 |
|---|---|
| `geo`（坐标换算） | 34 µs |
| `locate`（点位反查） | 28 µs |
| `elements`（缓存命中） | 121 µs |
| `hover`（窗口消息） | 152 µs |
| `capture`（截图，不画网格） | 16.5 ms |

窗口消息通道与缓存查询在亚毫秒量级；点击经典按钮（用 UI Automation 找到元素，以 `BM_CLICK` 送达）在端到端运行中为 4 到 10 ms，截图主要耗在抓取与编码上。数字随机器而异，请在你的机器上运行实测：

```text
deixion-cli bench "<窗口标题片段>" 200
```

它依次测量 `geo`（纯计算）、`elements`（缓存命中）、`locate`、`hover`（只移动）和 `capture`（不画网格），输出每项的 p50、p90、p99 与最小值（微秒）。延迟直方图的相对误差不超过约 6.25%。

## 从源码构建

环境：llvm-mingw 提供的 clang（本仓库的开发环境为 clang 22.1.8）、Ninja、CMake ≥ 3.25、Node。工具链为 UCRT，静态链接，运行时不需要安装 VC++ 运行库。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_RC_COMPILER=llvm-windres
cmake --build build
```

- 目标：`Deixion.exe`（界面）、`deixion-cli.exe`（命令行与 MCP）、`dx-testapp.exe`（测试靶子，不在默认目标内）。
- `deixion-setup.exe`（安装器，输出名 `Deixion-Setup-x64.exe`）只在检测到 Node 时生成，因为安装包的打包脚本 `tools/pack-payload.mjs` 需要 Node 运行。
- 有 Node 时，`ui/` 目录会被 `tools/pack-ui.mjs` 打包进 exe。没有 Node 时，界面不会打进 exe，需要设置环境变量 `DEIXION_UI_DIR` 指向 `ui/` 目录（仅用于开发）。
- 上述步骤已在干净的 `build/` 目录上完整执行过（2026-10-09，零警告零错误）。
- 端到端验证脚本在 `tools/e2e/`（只有端到端，没有单元测试）：`e2e-setup.ps1` 覆盖安装、带占用的升级、损坏安装包的回滚、卸载；`mcp-e2e.mjs` 用真实 MCP 会话驱动测试靶子并核对靶子自己写出的状态，同时检查权限边界；`mcp-stress.mjs <次数> [通道] [chord]` 反复压测文字替换与 `Ctrl+A`；`focus-e2e.ps1 [-Runs N]` 在跑 MCP 会话的同时用一个高优先级线程轮询系统前台窗口，只要测试靶子成为过前台窗口就判失败。运行前需要先完整构建，脚本会结束正在运行的 Deixion 进程。

第三方组件只有两个：WebView2 SDK 与 KaTeX，许可证和核验记录见 `third_party/AUDIT.md`。

## 许可证

[MIT](LICENSE)。

## 已知限制与待确认

- 安装、带占用的升级、损坏安装包的拒绝与回滚、卸载自删已做端到端验证。**WebView2 缺失时的自动补装需要联网且本机已有运行时，未实测**；快捷方式的创建在界面流程中验证过，未逐项核对 `.lnk` 内容。
- `launch` 的护栏与 `batch` 白名单限制的是模型能通过 MCP 工具做什么，它们是护栏，不是沙箱。同一个 Windows 用户下的其他程序（包括模型自己能运行的 shell）可以直接连命名管道或改 `settings.json`，Deixion 防不了这一点；模型被允许启动的程序本身也可能再起一个 shell。
- 后台模式对部分程序无效，例如不处理窗口消息、也不暴露 UI Automation 模式的程序。失败时会返回原因，可以更换目标，或开启 `allow_hop`。游戏等使用自绘或独占输入的程序是否可用，待确认。
- 断电时，最近写入的少量记录可能丢失。进程崩溃时，经验库中尚未落盘的更新最多 11 次。
- 日志异步写入。进程崩溃时，队列中尚未写出的日志（约 500 ms 内）可能丢失。
- 界面页面以代码为准，共 10 个页面（总览、定位、元素、操作、经验、日志、性能、接入、指南、设置）。
