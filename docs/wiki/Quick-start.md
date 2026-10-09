# Quick start / 快速开始

## 1. Install / 安装

1. Download `Deixion-Setup-x64.exe` from [Releases](https://github.com/Aevorine/Deixion/releases/latest). Compare it with `SHA256SUMS.txt` on the same page if you like.
2. Double-click. The install is per user. The default location is `%LOCALAPPDATA%\Programs\Deixion`; whatever location you pick, the program ends up in a folder named `Deixion`.
3. If the Microsoft WebView2 runtime is missing, the installer fetches it from Microsoft and checks its signature. Without it the window is unavailable, but MCP and the CLI still work.

The installer is not code-signed, so SmartScreen may warn on first run.

## 2. Connect Claude Code / 接入 Claude Code

The installer connects Claude Code for you (`/CLAUDE=1` is the default for a fresh install). You can also use the **Claude Code** page in the window, or run it by hand:

```text
claude mcp add --scope user deixion -- "%LOCALAPPDATA%\Programs\Deixion\deixion-cli.exe" mcp
```

Restart Claude Code afterwards.

## 3. Ask / 开始使用

Ask Claude in plain words, for example: “Open Notepad, write today's date and save it.” Deixion's tools list the windows, read their elements, click, type, press keys, scroll, drag, take screenshots with the Meridian grid, launch programs and wait for the UI to settle.

- **Overview** shows every action as it happens, with a landing radar.
- **Background mode** (default) works without disturbing you. **Foreground mode** shows the cursor trail so you can watch each step.
- Undo the last step from the window, the tray menu or `Ctrl+Alt+B`.

## 4. Update / 更新

*Settings → Check for updates* → *Download* → *Install and restart*. The installer is verified against `SHA256SUMS.txt` before it runs. A portable copy (an exe with `portable.flag` beside it) is updated by hand.
