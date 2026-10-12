# Accessibility / 无障碍

Deixion is two things: a desktop window (WebView2) with a tray icon, and an engine that drives other Windows programs through UI Automation, the same interface assistive technology uses. This page says what the window supports today, what is not there yet, and how to report a barrier.

Deixion 有两部分：一个桌面窗口（WebView2）加托盘图标，以及通过 UI Automation（辅助技术使用的同一套接口）操控其他 Windows 程序的引擎。本页说明窗口目前支持什么、还缺什么，以及遇到障碍时怎么反馈。

## Supported environments / 支持的环境

Windows 10 and 11, x64, with the Microsoft WebView2 runtime. The interface comes in Chinese and English and sets the page language (`lang`) to match, so a screen reader can pick the right voice.

Windows 10 / 11 x64，需要 Microsoft WebView2 运行时。界面有中文和英文，并把页面语言（`lang`）设成对应值，读屏软件可据此选择语音。

## What the window does / 窗口已经做到的

- **Keyboard.** Every control is a native button, input or select and is reachable with `Tab`. `Ctrl+1` … `Ctrl+0` switch between the main pages. `Esc` closes a dialog; a dialog moves focus to its first button when it opens. Global hotkeys can be re-recorded in Settings.
- **Visible focus.** A 2 px outline marks the focused control (`:focus-visible`).
- **Names for icons.** The pages show icons instead of text labels. Each icon button carries its function name as `aria-label`, and the same name appears as a tooltip on hover and on keyboard focus. Decorative icons are hidden from assistive technology (`aria-hidden`).
- **Roles and states.** Switches are `role="switch"` with `aria-checked`; dialogs are `role="dialog"`; the navigation rail is labelled.
- **Motion.** With “reduce motion” enabled in Windows (`prefers-reduced-motion`), transitions are turned off.
- **Colour and size.** Light and dark themes, following Windows by default; three density levels (compact, standard, relaxed).
- **Your pointer and focus stay yours.** In background mode the engine does not move the real cursor or take keyboard focus, so it does not pull a screen reader or a switch device away from what you are doing. Foreground mode shows visible actions and does take focus.

- **键盘。** 所有控件都是原生按钮、输入框或下拉框，可用 `Tab` 到达。`Ctrl+1` … `Ctrl+0` 切换主页面。`Esc` 关闭对话框；对话框打开时焦点移到第一个按钮。全局热键可在设置里重新录制。
- **可见焦点。** 当前焦点控件有 2 px 轮廓（`:focus-visible`）。
- **图标的名称。** 页面用图标而不是文字标签。每个图标按钮都带功能名（`aria-label`），鼠标悬停或键盘聚焦时以提示显示同一名称；装饰性图标对辅助技术隐藏（`aria-hidden`）。
- **角色与状态。** 开关是 `role="switch"` 并带 `aria-checked`；对话框是 `role="dialog"`；导航栏有标签。
- **动效。** Windows 开启“减少动效”时（`prefers-reduced-motion`），过渡动画关闭。
- **颜色与大小。** 浅色 / 深色主题，默认跟随 Windows；三档密度（紧凑、标准、宽松）。
- **不动你的光标和焦点。** 后台模式下引擎不移动真实光标、不抢键盘焦点，不会把读屏软件或开关设备从你正在做的事情上拉走。前台模式会显示可见操作，也会取得焦点。

## Known limitations / 已知限制

- **Not tested with a screen reader.** Narrator, NVDA and JAWS have not been run against the window. The names and roles above are in the code; how they are announced is unverified.
- **No high-contrast styling.** Windows contrast themes (`forced-colors`) have no dedicated styles, and colour contrast has not been measured against WCAG.
- **Dialogs do not trap focus.** `Tab` can leave an open dialog and reach the page behind it.
- **Segmented switches show their selection visually only.** The selected option is not exposed as a state (`aria-selected` / `aria-pressed`) yet.
- **Images and charts.** Screenshots (with the Meridian grid on the Locate page) and the charts on the Overview, Experience, Logs and Performance pages have no text alternative; the same data is available as text from `deixion-cli` and the MCP tools.
- **Fixed typefaces.** The interface uses SimSun and Times New Roman; there is no font or font-size setting apart from density.
- **The installer** is a native Win32 window (buttons, an edit box, a progress bar; some of them owner-drawn); it has not been checked with assistive technology either.
- **Programs that Deixion drives.** The engine depends on what the target program exposes through UI Automation. A program without automation providers can only be reached by coordinates.

- **没有用读屏软件测过。** 没有在讲述人、NVDA、JAWS 下运行过本窗口。上面的名称和角色写在代码里，实际如何播报未验证。
- **没有高对比样式。** 没有为 Windows 对比度主题（`forced-colors`）单独写样式，颜色对比度也没有按 WCAG 测量。
- **对话框不锁定焦点。** `Tab` 可以离开打开的对话框，到达后面的页面。
- **分段开关的选中状态只有视觉表现。** 选中项还没有以状态（`aria-selected` / `aria-pressed`）暴露给辅助技术。
- **图像与图表。** 截图（定位页带 Meridian 网格）以及总览、经验、日志、性能页的图表没有文字替代；同样的数据可以通过 `deixion-cli` 和 MCP 工具以文本取得。
- **固定字体。** 界面使用宋体与 Times New Roman；除密度外没有字体或字号设置。
- **安装器** 是原生 Win32 窗口（按钮、编辑框、进度条，其中部分为自绘），同样没有用辅助技术检查过。
- **被操控的程序。** 引擎依赖目标程序通过 UI Automation 暴露的内容；没有自动化接口的程序只能按坐标操作。

## Reporting a barrier / 反馈障碍

Open an [issue](https://github.com/Aevorine/Deixion/issues/new/choose) and say which assistive technology and version you use, the Windows version, the Deixion version, the page and control, and what you expected. Accessibility reports are handled like bugs, on a best-effort basis with no guaranteed response time.

请开一个 [issue](https://github.com/Aevorine/Deixion/issues/new/choose)，写明使用的辅助技术及版本、Windows 版本、Deixion 版本、所在页面与控件，以及期望的行为。无障碍问题按缺陷处理，尽力而为，不承诺响应时限。
