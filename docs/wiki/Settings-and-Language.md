# Settings, hotkeys and language / 设置、快捷键与语言

## Language / 语言

The window, the tray menu and the native notices come in Chinese and English.

| `language` | Result |
|---|---|
| `auto` (default) | Follows the Windows display language: Chinese systems get Chinese, everything else gets English. |
| `zh` | Always Chinese. |
| `en` | Always English. |

Change it in **Settings → Appearance and system → Language**. The window reloads at once and the tray menu follows without a restart. The installer chooses its own language from the Windows display language.

## Global hotkeys / 全局快捷键

Editable in Settings (click *Record*, press the new chord). If another program already owns a chord, Deixion shows “In use by another program”.

| Action | Default |
|---|---|
| Show / hide the window | `Ctrl+Alt+D` |
| Switch background / foreground | `Ctrl+Alt+M` |
| Pause / resume accepting actions | `Ctrl+Alt+U` |
| Undo the last step | `Ctrl+Alt+B` |
| Screenshot with grid to the clipboard | `Ctrl+Alt+S` |
| Emergency stop of the running batch | `Ctrl+Alt+X` |

Tray icon: **left click** shows the window, and clicking again while it is in front minimises it; **right click** opens the menu (mode, pause, undo, screenshot, connect Claude Code, update, start at sign-in, data folder, quit). `Ctrl+1 … 8` jump between the eight main pages.

## Settings worth knowing / 值得了解的设置

| Setting | What it does |
|---|---|
| `mode` | `background` (never moves your cursor or takes focus) or `foreground` (shows the trail). |
| `allow_hop` | Allows a brief foreground hop only when every background channel failed. |
| `allow_shell_launch` | Lets the model's `launch` open shells and script hosts. Off by default; only you can turn it on. |
| `launch_strict` + `launch_allow` | When on, `launch` can only start the programs on your list. |
| `verify` | Confirms an action took effect by watching window events. |
| `theme`, `density`, `language` | Look and feel. |
| `check_updates` | Check for a new release about 20 seconds after launch. |

Every setting and its default is listed in the [README](https://github.com/Aevorine/Deixion#settings). Settings can be exported and imported as JSON from the Settings page.
