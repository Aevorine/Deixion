# Accessibility / 无障碍

Deixion is two things: a desktop window (WebView2) with a tray icon, and an engine that drives other Windows programs through UI Automation, the same interface assistive technology uses. This page says what the window supports today, what is not there yet, and how to report a barrier.

Deixion 有两部分：一个桌面窗口（WebView2）加托盘图标，以及通过 UI Automation（辅助技术使用的同一套接口）操控其他 Windows 程序的引擎。本页说明窗口目前支持什么、还缺什么，以及遇到障碍时怎么反馈。

## Supported environments / 支持的环境

Windows 10 and 11, x64, with the Microsoft WebView2 runtime. The interface comes in Chinese and English and sets the page language (`lang`) to match, so a screen reader can pick the right voice.

Windows 10 / 11 x64，需要 Microsoft WebView2 运行时。界面有中文和英文，并把页面语言（`lang`）设成对应值，读屏软件可据此选择语音。

## What the window does / 窗口已经做到的

- **Keyboard.** Every control is a native button, input or select and is reachable with `Tab`. `Ctrl+1` … `Ctrl+0` switch between the main pages. Global hotkeys can be re-recorded in Settings.
- **Dialogs.** A dialog moves focus to its first button when it opens, keeps `Tab` inside itself, closes on `Esc` and returns focus to the control that opened it. It is `role="dialog"` with `aria-modal` and is named by its title.
- **Visible focus.** A 2 px outline marks the focused control (`:focus-visible`).
- **Names for icons.** The pages show icons instead of text labels. Each icon button carries its function name as `aria-label`, and the same name appears as a tooltip on hover and on keyboard focus. Decorative icons are hidden from assistive technology (`aria-hidden`).
- **Roles and states.** Switches are `role="switch"` with `aria-checked`; segmented switches are `role="tablist"` whose options are `role="tab"` with `aria-selected`; the navigation rail is labelled.
- **Charts and screenshots.** Each chart is `role="img"` with a text summary that follows the data (series name, latest, lowest and highest value). Screenshots carry alternative text.
- **Contrast themes.** Under a Windows contrast theme (`forced-colors`), selected and checked states, control borders, sliders and the focus ring are drawn with system colours.
- **Motion.** With “reduce motion” enabled in Windows (`prefers-reduced-motion`), transitions are turned off.
- **Colour and size.** Light and dark themes, following Windows by default; three density levels (compact, standard, relaxed).
- **Your pointer and focus stay yours.** In background mode the engine does not move the real cursor or take keyboard focus, so it does not pull a screen reader or a switch device away from what you are doing. Foreground mode shows visible actions and does take focus.

- **键盘。** 所有控件都是原生按钮、输入框或下拉框，可用 `Tab` 到达。`Ctrl+1` … `Ctrl+0` 切换主页面。全局热键可在设置里重新录制。
- **对话框。** 打开时焦点移到第一个按钮，`Tab` 不会离开对话框，`Esc` 关闭，关闭后焦点回到打开它的控件。对话框是 `role="dialog"` 并带 `aria-modal`，以标题命名。
- **可见焦点。** 当前焦点控件有 2 px 轮廓（`:focus-visible`）。
- **图标的名称。** 页面用图标而不是文字标签。每个图标按钮都带功能名（`aria-label`），鼠标悬停或键盘聚焦时以提示显示同一名称；装饰性图标对辅助技术隐藏（`aria-hidden`）。
- **角色与状态。** 开关是 `role="switch"` 并带 `aria-checked`；分段开关是 `role="tablist"`，其中每个选项是 `role="tab"` 并带 `aria-selected`；导航栏有标签。
- **图表与截图。** 每张图表是 `role="img"`，带随数据更新的文字概述（序列名称、最新值、最低值、最高值）。截图带替代文字。
- **对比度主题。** 在 Windows 对比度主题（`forced-colors`）下，选中 / 开启状态、控件边框、滑块和焦点轮廓用系统颜色绘制。
- **动效。** Windows 开启“减少动效”时（`prefers-reduced-motion`），过渡动画关闭。
- **颜色与大小。** 浅色 / 深色主题，默认跟随 Windows；三档密度（紧凑、标准、宽松）。
- **不动你的光标和焦点。** 后台模式下引擎不移动真实光标、不抢键盘焦点，不会把读屏软件或开关设备从你正在做的事情上拉走。前台模式会显示可见操作，也会取得焦点。

## Known limitations / 已知限制

- **Not tested with a screen reader.** Narrator, NVDA and JAWS have not been run against the window. The names and roles above are in the code; how they are announced is unverified.
- **Contrast themes are not verified.** The `forced-colors` styles were written from the specification and have not been looked at under a real Windows contrast theme. Charts are drawn on a canvas and keep the colours of the light or dark theme. Colour contrast of the normal themes has not been measured against WCAG.
- **Chart and screenshot text is short.** A chart summary gives numbers, not the shape of the curve; the alternative text of a screenshot says that it is a screenshot of the target, not what is on it. The same data is available as text from `deixion-cli` and the MCP tools.
- **Segmented switches are not arrow-key tabs.** Each option is its own `Tab` stop; the arrow-key pattern of a tab list is not implemented.
- **Fixed typefaces.** The interface uses SimSun and Times New Roman; there is no font or font-size setting apart from density.
- **The installer** is a native Win32 window (buttons, an edit box, a progress bar; some of them owner-drawn); it has not been checked with assistive technology either.
- **Programs that Deixion drives.** The engine depends on what the target program exposes through UI Automation. A program without automation providers can only be reached by coordinates.

- **没有用读屏软件测过。** 没有在讲述人、NVDA、JAWS 下运行过本窗口。上面的名称和角色写在代码里，实际如何播报未验证。
- **对比度主题未经验证。** `forced-colors` 样式是按规范写的，没有在真实的 Windows 对比度主题下看过。图表画在画布上，仍用浅色 / 深色主题自己的颜色。普通主题的颜色对比度没有按 WCAG 测量。
- **图表与截图的文字很简短。** 图表概述给的是数值，不描述曲线形状；截图的替代文字只说明这是目标的截图，不描述画面内容。同样的数据可以通过 `deixion-cli` 和 MCP 工具以文本取得。
- **分段开关不是方向键标签页。** 每个选项各占一个 `Tab` 停靠点，没有实现标签列表的方向键操作。
- **固定字体。** 界面使用宋体与 Times New Roman；除密度外没有字体或字号设置。
- **安装器** 是原生 Win32 窗口（按钮、编辑框、进度条，其中部分为自绘），同样没有用辅助技术检查过。
- **被操控的程序。** 引擎依赖目标程序通过 UI Automation 暴露的内容；没有自动化接口的程序只能按坐标操作。

## Reporting a barrier / 反馈障碍

Open an [issue](https://github.com/Aevorine/Deixion/issues/new/choose) and say which assistive technology and version you use, the Windows version, the Deixion version, the page and control, and what you expected. Accessibility reports are handled like bugs, on a best-effort basis with no guaranteed response time.

请开一个 [issue](https://github.com/Aevorine/Deixion/issues/new/choose)，写明使用的辅助技术及版本、Windows 版本、Deixion 版本、所在页面与控件，以及期望的行为。无障碍问题按缺陷处理，尽力而为，不承诺响应时限。
