# 技术路线图

> 长期方向与已选架构。源码高于本文；已发布的能力以 README 与 Release 为准。

## 一、已选架构与理由

1. **C++23，llvm-mingw clang，UCRT 静态链接。** 目标是下载即用，不装运行库（CLAUDE.md 硬约束）。`-static` 见 CMakeLists.txt。
2. **引擎单入口。** `Engine::call(method, json)` 是界面、CLI、MCP、IPC 共用的唯一入口（`core/engine/api.cpp` 方法表）。新能力只在一处登记。
3. **后台优先的通道阶梯。** UIA 模式、窗口消息、兜底（`hop`，默认关）。不动真实光标、不抢焦点是硬要求。消息通道对 XAML/UWP 无效，所以按窗口类型调序（`msg_hostile`）。
4. **经验库 + UCB + 遗忘。** 按（应用、角色、动作、通道）学习成功率与延迟，衰减因子 0.985，先验保留成本低的通道优先。
5. **校验靠界面事件，不解析结果。** WinEvent 钩子统计窗口是否在变，动作后观察是否有变化（`core/engine/activity.*`）。
6. **自研追加式记录文件（LogStore）。** 每条带 CRC-32C；打开时在第一条坏记录处截断；压缩用临时文件加原子替换。理由：源码未写明，此为推断。
7. **Meridian 比例坐标。** 与分辨率、DPI 无关；格网编码支持由粗到细的层级定位（`core/geo/meridian.*`）。
8. **界面用 WebView2 加内嵌资源包。** 内核保持 C++，界面用 Web 技术；`DEIXION_UI_DIR` 用于开发时读盘。
9. **安装包自包含。** 程序文件经 `tools/pack-payload.mjs` 压缩打包进安装器（DXPL 格式，每项带 CRC）；安装采用临时文件加替换，失败回滚。
10. **更新器契约固定。** 资产名 `Deixion-Setup-x64.exe`、`SHA256SUMS.txt`、参数 `/S /UPDATE /RELAUNCH`；只信任 GitHub 域名；校验失败即删除安装包。
11. **IPC 用命名管道。** 4 字节长度前缀加 JSON 帧；SDDL 只放行当前用户与 SYSTEM；无运行实例时在进程内直接跑引擎。

## 二、被否决的备选

- **MSVC 工具链。** 否决：项目规则规定使用 llvm-mingw（CLAUDE.md）。
- **运行库依赖（VC++ 再发行包、.NET）。** 否决：目标是下载即用。
- **默认移动真实光标或前置窗口。** 否决：后台模式必须不抢焦点（`hop` 仅作兜底）。
- **只用 PrintWindow 截图。** 否决：未被遮挡时直接 BitBlt，省去约 16 ms 的合成（`capture.cpp` 注释）。
- **只用消息注入输入。** 否决：XAML/UWP 不处理 Win32 消息（`engine.cpp` 注释），UIA 必须并存。
- **经验用简单平均。** 否决：需要遗忘与先验（`experience.hpp` 注释）。
- **更新静默自动安装。** 否决：界面需用户确认才调用 `update.apply`（`pages/settings.js`）。

## 三、技术债（已由源码核实）

1. `q_perf` 返回的进程线程数 `threads` 固定为 0（`core/engine/queries.cpp`），性能页该项无意义。
2. CMake 链接了 `d3d11`、`dxgi`，源码中没有调用。
3. `paths::cache_dir()` 会被创建，但没有读写它的代码。
4. `Updater::check(bool)` 的参数未使用（`app/updater.cpp`）。
5. WebView2 运行库缺失时的自动补装（`setup/sysops.cpp`）需要联网与一台没有运行库的机器，尚未实测；其中下载与签名校验的代码路径存在，但只有「已安装则跳过」的分支被端到端验证过。
6. 安装器关闭正在运行的程序时，优雅退出超时（10 秒）后会 `TerminateProcess`，可能丢失未落盘的操作。
7. 崩溃时经验库最多丢 11 次未落盘更新；日志异步写，约 500 ms 内的记录可能丢失。
8. 游戏、自绘或独占输入的程序能否走后台模式，没有证据，需要逐个实测。

## 四、方向（不是排期）

- **覆盖面**：为自绘界面（游戏、画布、部分 Electron 视图）补充视觉定位辅助，让 Meridian 编码能与截图差分、模板匹配结合。
- **可观测**：性能页补上真实线程数；经验库提供导出与重置。
- **分发**：代码签名（密钥放在仓库之外）；Release 附带符号文件与第三方许可清单。
- **扩展点**：新增页面只需在 `ui/js/pages/` 加文件并在 `index.js` 加一行 import；新增引擎方法只需在 `core/engine/api.cpp` 的方法表登记一处。
