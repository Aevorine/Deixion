# Deixion

**Deixion lets any Claude Code model operate Windows apps in the background, in milliseconds**, without moving your mouse and without taking your keyboard focus. 让 Claude Code 的任何模型在后台、毫秒级操控 Windows 应用，不动你的鼠标、不抢你的键盘焦点。

| | |
|---|---|
| **Install** | Download `Deixion-Setup-x64.exe` from [Releases](https://github.com/Aevorine/Deixion/releases/latest) and double-click. Per user, no administrator rights. |
| **Connect** | The installer registers the MCP server and the skill for Claude Code. Restart Claude Code, then just ask. |
| **Languages** | Chinese and English. Follows the Windows display language; change it in **Settings → Appearance and system → Language**. |
| **Updates** | Built in: *Settings → Check for updates → Download → Install and restart*. |

## Pages

- [Quick start](Quick-start): from download to your first command.
- [Settings, hotkeys and language](Settings-and-Language)
- [Troubleshooting](Troubleshooting)
- [Development](Development): build, layout, end-to-end tests, translations, releases.

## The idea in one paragraph

Every window is treated as a unit square: λ is the horizontal position and φ the vertical one, both from 0 to 1 (**Meridian coordinates**). A model that remembers “λ = 0.42, φ = 0.31” hits the same spot at any window size and any DPI. Deixion reads the UI Automation tree, picks the quietest input channel for each action (UI Automation patterns, window messages, or — only if you allow it — a brief foreground hop), learns which channel works best for each app, and records every action in a journal so it can be undone.

The full reference is the [README](https://github.com/Aevorine/Deixion#readme) ([中文](https://github.com/Aevorine/Deixion/blob/main/README.md)). Changes are listed in the [changelog](https://github.com/Aevorine/Deixion/blob/main/CHANGELOG.md).
