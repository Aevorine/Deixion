# Application control roadmap / 应用控制路线

Deixion uses a C++23 engine and a local stdio MCP interface. A model must support tool calling to operate it. Model-independent transport does not mean every model or every Windows application has been tested.

Deixion 使用 C++23 引擎与本地 stdio MCP。支持工具调用的 Claude Code 模型可以接入；协议不绑定模型不代表已经测试了所有模型与 Windows 应用。

## Screenshot policy / 截图策略

Settings → Screenshots and logs → Model screenshot policy. 设置 → 截图与日志 → 模型截图策略。

| Setting | Model workflow / 模型流程 |
| --- | --- |
| `before_each` (default) | Fresh target screenshot → inspect → select coordinates or element → one input action → repeat. 每一步先截图、看图定位，再执行一个输入动作。 |
| `adaptive` | Use fresh accessibility elements for stable layouts; inspect a new screenshot after layout changes. 稳定布局可用元素与批处理，布局变化后重新看图。 |
| `off` | Capture on demand; still verify meaningful results. 按需截图，仍检查实际执行结果。 |

The engine exposes the setting through `status`; the skill instructs the model to follow it. It cannot prove that a model viewed or understood an image. Meridian coordinates map the current client rectangle, not a semantic control across responsive layouts. A resize requires new localization.

设置通过 `status` 暴露，由 skill 指导模型遵守。引擎无法证明模型看懂了图片。经纬度坐标对应当前客户区，响应式布局变化后必须重新定位。

## Channel choices / 控制通道比较

Hard constraints: no unrequested foreground input, no fabricated success, no unrelated screenshot, and no hidden permissions escalation. Development effort is excluded from ranking. Choose verified result accuracy first, foreground isolation second, then measured latency.

硬约束：不擅自抢前台、不虚报成功、不返回其他窗口画面、不绕过权限。比较不计开发成本，优先结果准确、前台隔离，再看实测速度。

| Channel | Coverage and limits / 覆盖与限制 | State / 状态 |
| --- | --- | --- |
| UI Automation | Semantic controls; provider support varies. 语义控件，依赖应用提供模式。 | Implemented / 已实现 |
| Native Win32 messages | Very fast on classic controls; unsuitable for custom web inputs. 经典控件快，网页式输入不通用。 | Implemented / 已实现 |
| Chromium CDP adapter | DOM targeting; needs an explicitly enabled local debugging endpoint. DOM 精确定位，需要明确启用本地调试。 | Proposed / 待实现 |
| ConPTY session adapter | Own CLI sessions, structured output and cancellation; does not attach arbitrary existing terminals. 自有 CLI 会话，可读取输出和取消，不能接管任意已有终端。 | Proposed / 待实现 |
| OCR + image anchors | Custom rendered surfaces; must reject low confidence and stale images. 自绘界面，需拒绝低置信度和过期画面。 | Proposed / 待实现 |
| Screenshot + model reasoning | Broad visual interpretation; model inference dominates latency. 视觉理解广，耗时取决于模型。 | Skill workflow / skill 流程 |
| Separate Windows desktop | Better input isolation; app compatibility and display rendering need validation. 输入隔离更好，应用与渲染兼容待验证。 | Proposed / 待实现 |
| VM / remote session | Strong isolation; needs a provisioned environment and more resources. 隔离强，需要环境和更多资源。 | Proposed / 待实现 |

## Priorities / 创新功能优先级

1. Evidence-based channel learning: score each strategy by verified application output, latency, and foreground disturbance. Do not learn success from message delivery alone. 根据真实结果、耗时和前台干扰评分，不能只根据消息送达学习。
2. Frame-bound action tickets: bind selected coordinates to HWND, process lifetime, size, DPI and frame revision, then reject stale tickets. 把坐标绑定到窗口、进程、尺寸、DPI 与画面版本，拒绝过期操作。
3. CLI sessions with incremental output and interruption recovery. CLI 增量输出、取消与中断恢复。
4. Resource budgets: pause speculative capture under CPU pressure, encode only changed regions where supported, and keep one serialized mutation queue per target. CPU 紧张时降低预抓图，按能力编码变化区域，每个目标串行操作。

These items are proposals. Existing experience storage, cache, CRC recovery, journal, tray and hotkeys remain implemented features. The strongest algorithm cannot eliminate model inference, network latency or application-specific restrictions; report p50/p90/p99 separately for dispatch, capture, action and completed task.

以上为待实现方向。现有经验库、缓存、CRC 恢复、回滚日志、托盘和快捷键保持有效功能。分别报告调度、截图、动作和任务完成的 p50/p90/p99，不能承诺所有任务亚毫秒完成。

## Verified example and limits / 示例与限制

On 2026-10-09 a hidden classic PowerShell console received a Codex CLI weather query via Deixion's `type` and `key enter`, ran it in the requested working directory, and produced a result file with source links. Its background screenshot was unsupported: screen-copy fallback must refuse when the console is hidden or covered. CLI output is the verification path for that case.

2026-10-09 已通过 Deixion 的 `type` 与 `key enter`，在指定工作目录的隐藏 PowerShell 控制台执行 Codex CLI 天气查询并取得带来源结果。该控制台不支持后台截图，此时通过 CLI 输出验收，不能把覆盖窗口截图充作控制台截图。

ChatGPT desktop screenshot and accessibility targeting worked on the same machine, but background text input did not reach its web editor in 1.0.6. Version 1.0.7 changes the Chromium input path (top-level keyboard messages, a background focus click, real Enter / Tab / Space) and it works on an Edge test page; ChatGPT desktop itself has not been re-run, so this example stays open until it is. A delivered message is not accepted as proof. Games, protected applications, elevated windows and apps without automation providers may require foreground mode or isolation.

同机 ChatGPT 桌面版截图与可访问性定位可用，1.0.6 的后台文字输入未进入网页编辑器；1.0.7 改了 Chromium 输入路径（键盘消息发顶层窗口、后台点击取得页面焦点、回车 / 制表 / 空格按真实按键），在 Edge 测试页上已通过，但 ChatGPT 桌面版本身还没重跑，因此该示例仍未完成。游戏、受保护应用、提权窗口和缺少自动化接口的应用可能需要前台模式或隔离环境。
