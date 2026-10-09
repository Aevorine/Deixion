# Deixion

English · [中文](README.md)

> **The five-year-old version**: a computer has many programs in it. Claude Code wants to click and type in them for you, but on its own it can only look at screenshots and guess where to click, which is slow and easy to get wrong. Deixion gives Claude Code a pair of hands: while you watch videos or browse the web as usual, it clicks, types and reads other programs in the background, **without moving your mouse and without taking your keyboard focus**.

Deixion is a Windows desktop tool made of three parts:

- **C++23 core**: UI Automation element tree, screen capture, input injection, experience store, rollback journal.
- **WebView2 desktop UI**: lives in the tray and offers the Overview, Locate, Elements, Actions, Experience, Logs, Performance, Claude Code, Guide and Settings pages.
- **`deixion-cli`**: a command-line tool with an MCP (stdio) server built in, for Claude Code to call.

## Contents

- [How it works](#how-it-works)
- [Two modes](#two-modes)
- [Strategies and the experience store](#strategies-and-the-experience-store)
- [Meridian coordinates](#meridian-coordinates)
- [MCP tools](#mcp-tools)
- [Command line: deixion-cli](#command-line-deixion-cli)
- [Installation](#installation)
- [Updates](#updates)
- [Connecting Claude Code](#connecting-claude-code)
- [Settings](#settings)
- [Tray and hotkeys](#tray-and-hotkeys)
- [Data, logs and crash recovery](#data-logs-and-crash-recovery)
- [Performance](#performance)
- [Building from source](#building-from-source)
- [License](#license)
- [Known limits and items to confirm](#known-limits-and-items-to-confirm)

## How it works

```text
Claude Code ──stdio (MCP)──► deixion-cli.exe ──named pipe──► Deixion.exe (engine) ──► target window
                                  │                              ├─ UI Automation (element tree)
                                  │                              ├─ window messages / real input
                                  └─ if not running: start the   ├─ capture (BitBlt / PrintWindow)
                                     tray build, or run the      └─ experience store · rollback · logs
                                     engine in this process
```

- While `deixion-cli mcp` runs, if Deixion is not already running it starts the tray build as `Deixion.exe --tray` (set the environment variable `DEIXION_NO_APP` to turn this off). Other sub-commands run the engine inside the CLI process when Deixion is not running (`--inproc` forces this).
- The pipe is named `\\.\pipe\Deixion-v1-<current user>`. Its security descriptor grants access only to the current user and SYSTEM.
- Single instance: only one Deixion can run per logon session. Starting a second copy brings up the window of the running one. `--tray` keeps only the tray icon; `--quit` asks the running instance to exit.

## Two modes

| | Background (default) | Foreground |
|---|---|---|
| Real cursor | Not moved | Moved, actions are visible |
| Focus | Not taken | The window is brought to the front |
| Input channels | UI Automation patterns and window messages, ordered per action and window type (see the next section) | Real mouse and keyboard (SendInput) |
| Action visualisation | None | Ripples, control highlight boxes, action labels (can be turned off) |
| Mouse travel time | Not applicable | Set by `speed` |

- **In background mode**, some windows ignore ordinary window messages (for example XAML / UWP content windows). The engine recognises these windows and moves UI Automation ahead of the message channels.
- **Classic console windows** (`ConsoleWindowClass`, the conhost window that hosts PowerShell / cmd) merge adjacent identical key events in the input buffer into one event with a repeat count, so text sent only as `WM_CHAR` turned `--` into `---` and `ee` into `eee`. When text goes to such a window the engine now follows every character with a key-up message, so neighbouring events are no longer identical and are not merged. Measured: typing `'-- ee --ee aa 11 ;;'` into Windows PowerShell inside conhost came out as `--- eee ---eee aaa 111 ;;;` all three times on v1.0.4 and verbatim all three times on v1.0.5. A Windows Terminal window is not such a window and was not tested.
- **Focus guard.** A control that handles a click usually calls `SetFocus` itself, which pulls the whole window to the front. While a background action runs, Deixion therefore holds the Windows foreground lock (`LockSetForegroundWindow`), so the target cannot move above the window you are using. UI Automation patterns on classic Win32 controls activate the target from inside its own process and slip past that lock, so for those controls the engine sends the equivalent message itself: `BM_CLICK` for `Button`-class buttons, check boxes and radio buttons, and `WM_SETTEXT` for `Edit` / `RichEdit` boxes. If a program still takes the foreground, the window you were using is put back as soon as the action returns, the result carries a `focus` note, and the experience store lowers the reward of that channel so it is chosen less often. `status` reports the counters (`focus_shield`: `locked`, `denied`, `restored`). Window state changes are covered too: in background mode `restore` / `maximize` / `minimize` also hold the foreground lock first and put your window back if the target activated itself, and a window restored from minimized is placed directly beneath the window you are using, so it can be driven and captured without covering yours. Measured with the test target (2026-10-09, v1.0.5, isolated mode): 27 full MCP sessions with the foreground window polled at sub-millisecond resolution. In 26 the target never became the foreground window; in one it held the foreground for about 40 ms right after it was launched and the user's window was put back at once; 24 further runs did not repeat it. Other frameworks (WPF, Electron, games) have not been measured.
- **Brief foreground fallback** (`allow_hop`, off by default): only when this is on can a background action briefly bring the window to the front. When it is off, actions that need the foreground return an error and never switch windows behind your back.
- A single call can downgrade foreground mode to background with `mode: "background"`, but a single call cannot upgrade background mode to foreground.

## Strategies and the experience store

Every action has an ordered list of **channels** (strategies). In foreground mode the first choice for most actions is `real` (real input). In background mode the order is as follows (source: `src/core/engine/actions.cpp`):

| Action | Background channels (earlier ones are tried first) |
|---|---|
| `click` | `uia` → `msg` (demoted to a fallback when the window is hostile) → `uia_hit` → `hop` |
| `type` | `msg_char` → `uia_set` → `hop` (the first two swap when the window is hostile) |
| `set_value` | `uia_set` → `msg_settext` |
| `key` | `msg_key` or `msg_chord` → `hop` |
| `scroll` | `msg` → `uia_scroll` → `hop` |
| `drag` | `msg` → `hop` |
| `window` / `launch` | Win32 window APIs / Shell launch |

For classic Win32 controls the `uia` click and the `uia_set` / `set_value` write are carried out as `BM_CLICK` and `WM_SETTEXT` (see the focus guard above). `hop` exists only when `allow_hop` is on. Non-fallback channels are re-ordered by the experience store described below. Channels marked as fallbacks are tried only after the earlier ones have failed.

**Experience store** (`experience.dxl`) keeps one statistics record for each (application, role, action, channel) tuple. On every update the old statistics are multiplied by 0.985 (decay). Candidate channels are ranked with UCB:

$$
\text{score}=\frac{R+1.5\,p_i}{n+1.5}+0.35\sqrt{\frac{\ln(N+2)}{n+1.5}}
$$

Here $n$ is the decayed sample count, $R$ is the decayed reward sum, $p_i=0.9-0.1\,i$ is the prior from the default order (with $i$ the candidate index), and $N$ is the sum of sample counts over all candidates. The reward is $0.9/(1+t/6)$ when no verification was done; $\max(0.3,\,1/(1+t/6))$ when verification confirmed the effect; and $0.45$ when verification could not confirm it ($t$ is the duration in milliseconds). The store also learns a reaction time and a UI-quiet time, which set the waiting windows for verification and for `wait`.

## Meridian coordinates

Meridian treats the client area of any window as a unit square. The horizontal coordinate is the longitude $\lambda$, the vertical coordinate is the latitude $\varphi$, the top-left corner is $(0,0)$ and the bottom-right corner is $(1,1)$. The same $(\lambda,\varphi)$ points at the same content at any window size and any DPI, so a model only needs to remember a proportional position, not pixels.

Let the client area's top-left corner be $(x_0,y_0)$ and its size $W\times H$ (physical pixels). A pixel $(x,y)$ converts to and from proportional coordinates as follows:

$$
\lambda=\operatorname{clamp}_{[0,1]}\!\left(\frac{x-x_0+\tfrac12}{W}\right),\qquad
\varphi=\operatorname{clamp}_{[0,1]}\!\left(\frac{y-y_0+\tfrac12}{H}\right)
$$

$$
x=x_0+\operatorname{clamp}_{[0,\,W-1]}\!\left(\lfloor \lambda W\rfloor\right),\qquad
y=y_0+\operatorname{clamp}_{[0,\,H-1]}\!\left(\lfloor \varphi H\rfloor\right)
$$

**Grid codes.** Each $\lambda$ and $\varphi$ is first quantised to a 16-bit integer $q=\min(\lfloor 65536\cdot v\rfloor,\,65535)$. The bits of the two values are then interleaved into a Morton code $K=(\operatorname{spread}(q_\lambda)\ll 1)\,|\,\operatorname{spread}(q_\varphi)$, and shifted left by 3 to give a 35-bit value $K_{35}$. A code of length $L$ ($1\le L\le 6$) takes the top $5L$ bits of $K_{35}$ and maps every 5 bits to one character. The alphabet is `0123456789ABCDEFGHJKMNPQRSTVWXYZ`, which leaves out I, L, O and U. Decoding is case-insensitive and treats I and L as 1 and O as 0. A prefix is the enclosing cell, so a position can be narrowed down level by level.

**Precision.** At level $L$ the horizontal axis has $n_x=\lceil 5L/2\rceil$ bits and the vertical axis has $n_y=\lfloor 5L/2\rfloor$ bits, so a cell measures $2^{-n_x}\times 2^{-n_y}$. The system picks the smallest $L$ with $2^{n_x}\ge W$ and $2^{n_y}\ge H$, which makes a cell no larger than one pixel. By this formula, a 1920×1080 window needs 5 characters ($n_x=13$, $n_y=12$; cells of about 0.23 × 0.26 px). This is a calculation from the formula, not a measurement.

## MCP tools

Claude Code calls these 18 tools through `deixion-cli mcp`. `window` accepts a title fragment, `exe:name.exe`, `class:Name`, `pid:N`, `hwnd:0x…`, `active`, or `screen` (the whole screen). A target point can be given as `element` (for example `e12`, from `elements` or from a screenshot with `elements:true`), `find` (fuzzy search), `at` (`"lam,phi"`), `code` (grid code), `px` (client pixels) or `screen` (screen pixels).

| Tool | What it does | Key parameters |
|---|---|---|
| `windows` | Lists top-level windows (handle, exe, title, client size) | `filter` |
| `screenshot` | Captures a window as JPEG with the Meridian grid; can draw numbered boxes | `window`, `region{a,b}`, `code`, `grid`, `elements`, `marks`, `max_dim` |
| `elements` | Lists UI Automation elements (id, role, name, centre point, usable patterns) | `window`, `query`, `role`, `interactive`, `limit` (default 150), `refresh` |
| `find` | Fuzzy-searches elements by name, role or AutomationId | `window`, `text`, `role`, `aid`, `limit` (default 5) |
| `locate` | Reports the element at a point and its grid code | `window` + point |
| `click` | Clicks (background uses UIA patterns first and does not move the cursor) | `window`, point, `button`, `count` (1–3), `hover` |
| `type` | Types text (can replace the whole value) | `window`, `text`, `replace`, point |
| `key` | Presses a key, a chord, or a space-separated sequence | `window`, `keys`, `repeat` |
| `scroll` | Scrolls (`dy>0` down, `dx>0` right, in wheel notches) | `window`, `dy`, `dx`, point |
| `drag` | Drags | `window`, `from`, `to`, `button`, `steps` |
| `set_value` | Sets a value directly through UIA without typing (undoable) | `window`, `value`, point |
| `read` | Reads an element's value, text or toggle state | `window`, point |
| `window_op` | Window management. A minimized window can be restored or maximized directly (every other action still asks you to restore it first); in background mode these state changes do not take your foreground | `window`, `op` (focus / minimize / maximize / restore / close / move / resize / topmost), `rect` |
| `launch` | Starts a program, document or URL (background does not steal focus). It refuses Deixion's own programs, and, unless the user turns on `allow_shell_launch`, command shells, script hosts and interpreters (`cmd`, PowerShell, `wscript`, Python, Node, `.bat`, `.ps1`, `.url`, …) and link schemes other than `http`, `https`, `mailto` and `ms-settings` (including `file:`). The check is made on the resolved file name: quotes, `file:` URLs, short names and trailing dots are undone first, and `explorer.exe` is checked for programs named in its arguments | `path`, `args`, `cwd`, `wait_window_ms` (default 3000) |
| `wait` | Waits without fixed sleeps | `for` (settle / element / gone / window), `window`, `find`, `timeout_ms` (default 5000), `quiet_ms` |
| `batch` | Runs many steps in one call (one round trip) | `steps`, `defaults`, `stop_on_error` (default true, at most 200 steps) |
| `undo` | Rolls back the most recent N reversible actions | `count` (1–50), `id` |
| `status` | Engine status; `detail` can be journal / experience / perf / log | `detail` |

When an action's parameters are written to the log or the rollback journal, typed text is recorded only as a character count. Rolling back `set_value` requires the previous value, which is kept in the rollback journal.

**Permission boundary**: every `batch` step must be one of the actions or queries in the table above. User-only controls (changing settings, clearing the journal, resetting the experience store, the emergency stop) cannot be reached through `batch`, so the pause and "allow brief foreground" switches can be changed only by the user. Input actions (click, type, key, scroll, drag, set value) also cannot operate Deixion's own windows, otherwise a model could simply click the switches in the UI; passive queries such as screenshots and reading elements are not restricted. After connecting, the named-pipe client checks which account the server process belongs to and disconnects if it is not the current one, so another account on a shared machine cannot pre-create the pipe. The failure branch of that check needs a second Windows account to exercise and has not been tested.

## Command line: deixion-cli

```text
deixion-cli mcp                          run the MCP server on stdin/stdout (for Claude Code)
deixion-cli call <method> [json|@file]   call any engine method
deixion-cli windows [filter]             list windows
deixion-cli shot [window] [-o file.jpg] [--no-grid] [--elements]
deixion-cli elements <window> [query]    list UI Automation elements
deixion-cli bench <window> [rounds]      latency benchmark of the main paths
deixion-cli status | doctor | version
deixion-cli --inproc …                   do not connect to a running Deixion; run the engine in this process
```

`doctor` checks the Windows version, the CPU instruction sets (BMI2 / AVX2 / SSE4.2), the WebView2 runtime, engine reachability, whether UI Automation is available, and whether the Claude Code CLI can be found on PATH. If WebView2 is missing, the UI is unavailable, but MCP and the CLI still work.

## Installation

> `tools/pack-payload.mjs` compresses each program file, adds a CRC32 and embeds the result in `Deixion-Setup-x64.exe`. At install time every file is unpacked and verified first, and only then swapped in.

1. Download `Deixion-Setup-x64.exe` from [Releases](https://github.com/Aevorine/Deixion/releases). The `SHA256SUMS.txt` on the same page can be used to verify it.
2. Double-click to install. The install is per user and needs no administrator rights. You can choose the install location, but the program always ends up in a folder named `Deixion`: choosing `D:\Tools` installs to `D:\Tools\Deixion`, and choosing `D:\Deixion` leaves the path as it is.
3. The default location is `%LOCALAPPDATA%\Programs\Deixion`. If Deixion is already installed, the existing location is kept.
4. If the WebView2 runtime is missing, the installer downloads the bootstrapper from Microsoft's official address (`go.microsoft.com`), checks that it carries a Microsoft signature, and installs it silently. This needs a network connection. If that step fails, the installation still completes, but the UI is unavailable; MCP and the CLI are not affected.
5. Before installing, the installer asks any running Deixion to quit and waits up to 10 seconds. If it is still running after that, the installer forcibly ends it, and unsaved content may be lost.
6. The installation is two-phase: every file is first unpacked and verified as a temporary file, and the old files are replaced only after all of them succeed. If something fails midway, the change is rolled back, so the program is never left as a mix of old and new files.

Silent install parameters (installer source `src/setup/main.cpp`):

| Parameter | Effect |
|---|---|
| `/S` | Silent install |
| `/D=<dir>` | Install location (`\Deixion` is appended automatically; not appended if the path already ends in `Deixion`) |
| `/UPDATE` | Keep the existing location and options; the Claude Code connection is refreshed only if it was connected before |
| `/RELAUNCH` | Start after installing, as the tray build (`--tray`) |
| `/UNINSTALL` | Uninstall |
| `/PURGE` | When uninstalling, also delete user data (`%LOCALAPPDATA%\Deixion`, and the `data` folder of a portable build) |
| `/AUTOSTART=0\|1` | Start at sign-in; if not given, a fresh install sets it off, and an existing install keeps its current setting |
| `/DESKTOP=0\|1` | Desktop shortcut; if not given, a fresh install sets it off, and an existing install keeps its current setting |
| `/STARTMENU=0\|1` | Start menu shortcut; if not given, a fresh install sets it on, and an existing install keeps its current setting |
| `/CLAUDE=0\|1` | Connect to Claude Code; if not given, a fresh install sets it on, and an update keeps the current state (refreshed only if connected before) |
| `/LOG=<file>` | Installer log (default `%TEMP%\Deixion-setup.log`) |

Exit codes: `0` success; `3` the installer package is damaged; `4` the folder is not writable; `5` a write failed; `6` the running Deixion could not be closed; `1` any other error.

**Data on upgrade and uninstall**: installing and upgrading never delete user data. Uninstalling removes the program files, the shortcuts, the sign-in entry, the uninstall registration, and the Claude Code MCP registration and skill, but keeps user data unless `/PURGE` is given.

### Install from the command line (GitHub Package)

The launcher package `@aevorine/deixion` on GitHub Packages has no program code of its own. `npx @aevorine/deixion install` downloads the release, **checks its SHA-256 against `SHA256SUMS.txt` and refuses to run it on a mismatch**, then starts the installer (`--silent`, `--version X.Y.Z` and `--dry-run` are supported). `npx @aevorine/deixion mcp` starts the MCP server of the installed `deixion-cli.exe`. GitHub's npm registry needs a token even for public packages; see `packages/npm/README.md` for the `.npmrc` lines.

## Updates

The in-app update flow works as follows (code: `src/app/updater.cpp`):

1. **Check**: about 20 seconds after launch, Deixion checks automatically (the `check_updates` setting is on by default). You can also check manually from the tray or the Settings page. It queries the latest GitHub Release and compares version numbers.
2. **Download**: on the Settings page, click "Download" to save `Deixion-Setup-x64.exe` into `update\` under the data folder. Only `https` addresses on `github.com` or `githubusercontent.com` are accepted.
3. **Verify**: `SHA256SUMS.txt` is downloaded too, and the SHA-256 of the installer is compared with the listed value. On a mismatch, the installer is deleted and an error is shown.
4. **Install**: once verification passes, click "Install and restart" (a confirmation dialog appears). The installer is started with `/S /UPDATE /RELAUNCH`. It first asks the running Deixion to quit (see installation step 5), then replaces the program files, and finally starts the new version as the tray build.

The portable build (an exe with a `portable.flag` file beside it) is not updated in place. Download the new archive by hand.

## Connecting Claude Code

What the Claude Code page does:

- **Install the skill**: copies the skill into `~/.claude/skills/deixion`. The skill files come from `skills/deixion/` in the repository; the build copies them next to the program and the installer packs them in.
- **Register the MCP server**: removes any previous `deixion` registration first, then runs:

  ```text
  claude mcp add --scope user deixion -- "<install dir>\deixion-cli.exe" mcp
  ```

- **Status**: shows whether the Claude Code CLI was found, whether the MCP server is registered, whether the registered path points to the current install, and whether the skill is installed and up to date.
- **Remove**: unregisters the MCP server and deletes the skill folder.

When `/CLAUDE=1` is used, the installer runs `Deixion.exe --connect-claude`, and uninstalling runs `--disconnect-claude`. These use the same connection logic as the page.

If the `claude` command cannot be found, the page shows the JSON and command line needed for a manual registration. Restart Claude Code after registering.

## Settings

Settings are stored in `settings.json` in the data folder. Writes go to a temporary file that then replaces the old one, and the previous file is kept as `.bak`. If the file cannot be read, the backup is used.

| Key | Values (default) | Meaning |
|---|---|---|
| `mode` | `background` / `foreground` (`background`) | Background or foreground mode, see above |
| `allow_hop` | boolean (`false`) | Allow a brief switch to the foreground as a fallback |
| `allow_shell_launch` | boolean (`false`) | Let `launch` start command shells, script hosts and interpreters. Only the user can change it: the MCP tools and `batch` cannot |
| `launch_strict` | boolean (`false`) | Strict mode: `launch` may start only the programs in `launch_allow` (plus `http`, `https`, `mailto` and `ms-settings` links). Anything else is refused, `explorer.exe` with arguments included. Listing a shell or interpreter there is an explicit permission |
| `launch_allow` | list of strings (`[]`) | Program names such as `notepad.exe`, or full paths. Edited on the Settings page ("Programs that may be started") |
| `speed` | `instant` / `fast` / `smooth` (`fast`) | Mouse travel time in foreground mode: 0 ms / 120 ms / 380 ms (drag is timed separately) |
| `overlay` | boolean (`true`) | Show the action trail in foreground mode |
| `verify` | `auto` / `off` (`auto`) | Whether to check the UI for a change after an action, see "Verification" below |
| `jpeg_quality` | 30–100 (78) | JPEG quality of screenshots |
| `max_image_dim` | 400–4096 (1568) | Longest side of a screenshot, in pixels |
| `grid_default` | boolean (`true`) | Draw the Meridian grid on screenshots by default |
| `log_level` | `debug` / `info` / `warn` / `error` (`info`) | Log level |
| `autostart` | boolean (`false`) | Start at sign-in |
| `check_updates` | boolean (`true`) | Check for updates after launch |
| `close_to_tray` | boolean (`true`) | Closing the window hides it to the tray |
| `paused` | boolean (`false`) | Stop accepting actions |
| `theme` | `auto` / `light` / `dark` (`auto`) | UI theme; `auto` follows the system |
| `density` | `compact` / `standard` / `relaxed` (`standard`) | UI density |
| `language` | `auto` / `zh` / `en` (`auto`) | UI language. `auto` follows the Windows display language: Chinese systems get Chinese, everything else gets English. Changing it in Settings reloads the window at once; the tray menu and notices switch with it |
| `hotkeys` | see the table below | Global hotkeys |

**Verification (`verify`)**: when it is on, after an action succeeds the engine waits, within a time limit, for interface events from the target window (counted by a WinEvent hook). If the interface changes in that time, the result is recorded as `confirmed: true`; otherwise it is recorded as not confirmed. The `confirmed` and `reaction_us` fields in the result come from this. Callers can use them to tell whether an action really took effect.

**Look and feel**: Chinese text uses SimSun (宋体), Western text and punctuation use Times New Roman, body text is 12 pt (小四) and titles are 14 pt (四号). Formulas are rendered by the bundled KaTeX. Theme, density and language are listed above; the window, the tray menu and the native notices all come in Chinese and English, defaulting to the system language and changeable by hand in Settings. The palette is a low-saturation blue-grey, with one set for light and one for dark.

## Tray and hotkeys

**Tray**: a left click toggles between showing and minimising the window. The right-click menu contains: show / minimise window, background mode, foreground mode, allow brief foreground fallback, show action trail in foreground mode, pause / resume accepting actions, undo last action, copy screenshot with grid to clipboard, emergency stop of the current batch, connect to Claude Code, check for updates, start at sign-in, open data folder, and quit Deixion.

**Default global hotkeys** (editable in Settings; if another program already uses a chord, registration fails and the failure is written to the log):

| Name | Default | Action |
|---|---|---|
| `toggle` | `Ctrl+Alt+D` | Show / minimise the window |
| `mode` | `Ctrl+Alt+M` | Switch background / foreground mode |
| `pause` | `Ctrl+Alt+U` | Pause / resume accepting actions |
| `undo` | `Ctrl+Alt+B` | Undo the last action |
| `shot` | `Ctrl+Alt+S` | Screenshot to clipboard |
| `stop` | `Ctrl+Alt+X` | Emergency stop of the current batch |

## Data, logs and crash recovery

The data folder is `%LOCALAPPDATA%\Deixion`. If a `portable.flag` file sits beside the exe, the data goes to `<exe folder>\data` instead (portable mode).

| Path | Contents |
|---|---|
| `settings.json` (and `.bak`) | Settings |
| `window.json` | Window position and size |
| `store\experience.dxl` | Experience store: one statistics record per (application, role, action, channel), plus learned values such as reaction time and UI-quiet time |
| `store\journal.dxl` | Rollback journal: a record of each reversible action and its inverse |
| `logs\deixion-YYYYMMDD.jsonl` | Logs (JSON Lines), rolled at 8 MB per file, kept for 14 days |
| `webview\` | WebView2 user data |
| `update\` | Download folder for updates |

**Storage format and crash recovery** (`src/core/store/logstore.*`): `experience.dxl` and `journal.dxl` are append-only record files. Each record holds a length, a type, a timestamp, a payload and a CRC-32C checksum. On open, every record is checked. At the first damaged or truncated record, the file is cut back to that point, which removes the "torn tail" left by a crash or power loss. Every record before it is kept. When a file is compacted, a temporary file is written and flushed with `FlushFileBuffers`, then atomically swapped in, so at every moment a complete, readable copy is on disk. The rollback journal keeps the latest 3000 entries in memory.

## Performance

Round-trip latency measured with `deixion-cli bench` on 2026-10-09 on an Intel Core i5-1155G7 (Windows 11, build 26300), through the named pipe, 300 rounds, p50:

| Path | p50 |
|---|---|
| `geo` (coordinate math) | 34 µs |
| `locate` (point lookup) | 28 µs |
| `elements` (cache hit) | 121 µs |
| `hover` (window message) | 152 µs |
| `capture` (screenshot, no grid) | 16.5 ms |

The window-message channel and cached queries are sub-millisecond. A click on a classic button (element found through UI Automation, delivered as `BM_CLICK`) took 4 to 10 ms in the end-to-end runs, and a screenshot is dominated by capture and encoding. Numbers vary by machine; measure on yours:

```text
deixion-cli bench "<window title fragment>" 200
```

It measures, in turn, `geo` (pure computation), `elements` (cache hit), `locate`, `hover` (move only) and `capture` (without the grid), and prints p50, p90, p99 and the minimum for each (in microseconds). The latency histogram has a relative error of at most about 6.25%.

## Building from source

Requirements: clang from llvm-mingw (the development environment uses clang 22.1.8), Ninja, CMake ≥ 3.25, and Node. The toolchain uses UCRT with static linking, so no VC++ runtime needs to be installed on the target machine.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_RC_COMPILER=llvm-windres
cmake --build build
```

- Targets: `Deixion.exe` (UI), `deixion-cli.exe` (CLI and MCP) and `dx-testapp.exe` (test target, not built by default).
- `deixion-setup.exe` (the installer, with output name `Deixion-Setup-x64.exe`) is generated only when Node is found, because the payload packer `tools/pack-payload.mjs` runs on Node.
- With Node present, the `ui/` folder is packed into the exe by `tools/pack-ui.mjs`. Without Node, the UI is not packed into the exe, and the environment variable `DEIXION_UI_DIR` must point at the `ui/` folder (for development only).
- The steps above were run in full on a clean `build/` folder (2026-10-09, zero warnings and zero errors).
- End-to-end scripts live in `tools/e2e/` (end to end only, no unit tests): `e2e-setup.ps1` covers install, upgrade over locked files, rollback on a damaged package and uninstall; `mcp-e2e.mjs` drives the test target through a real MCP session, checks the state the target itself wrote, and checks the permission boundaries; `mcp-stress.mjs <runs> [channel] [chord]` hammers text replacement and `Ctrl+A`; `focus-e2e.ps1 [-Runs N] [-Isolated]` runs the MCP session while a high-priority thread polls the system foreground window, and fails if the test target ever becomes the foreground window. `console-e2e.ps1 [-Runs N] [-Cli path]` starts PowerShell in a classic console window, types `-- ee --ee aa 11 ;;` in the background and checks that the console received it verbatim, then restores / maximizes / minimizes / restores the minimized console and asserts that your window stays in front and the console never becomes the foreground window (one control step shows that a plain `ShowWindow(SW_RESTORE)` outside Deixion does take the foreground, so the assertion can catch a steal); `-Cli` can point at another `deixion-cli.exe` for an old-versus-new comparison. `i18n-e2e.mjs` starts the real Deixion.exe (portable mode, in a temp folder) and reads every page through the WebView2 debugging port: in English no Chinese character may appear on the ten pages, in any segmented switch or in the Settings dialogs; Chinese stays as it was; clicking the language button reloads the window and writes `settings.json`. `node tools/i18n-check.mjs` statically checks that the dictionary covers every Chinese string in the code. Build everything first. The named pipe is one per user, so `mcp-e2e.mjs`, `mcp-stress.mjs` and `focus-e2e.ps1` connect by default to whatever Deixion is already running (for example an installed older version) and then do not test the new binary in `build/`. With the environment variable `DX_E2E_ISOLATED=1` (`-Isolated` for `focus-e2e.ps1`) they copy `deixion-cli.exe` under `.scratch/`, add a `portable.flag` and run the engine inside their own process with `--inproc`, which leaves a running Deixion and your real settings alone (the "own-window protection" and "strict launch mode" sections need the real `Deixion.exe` and are skipped then); `console-e2e.ps1` is always isolated. `focus-e2e.ps1` only ends processes whose executable is under the project folder; `e2e-setup.ps1` ends every Deixion / deixion-cli process by name (the installed copy included), so run it on a development machine after quitting the Deixion you use every day.

There are only two third-party components: the WebView2 SDK and KaTeX. Their licences and verification records are in `third_party/AUDIT.md`.

## License

[MIT](LICENSE).

## Known limits and items to confirm

- Install, upgrade over locked files, rejection and rollback of a damaged package, and self-deleting uninstall were verified end to end. **The automatic WebView2 download needs a network and a machine without the runtime, and was not exercised**; shortcut creation was exercised through the UI flow but the `.lnk` contents were not checked item by item.
- Background mode does not work with every program, for example programs that ignore window messages or do not expose UI Automation patterns. A failure returns its reason, and you can retarget or enable `allow_hop`. Whether games (programs that draw their own UI or take exclusive input) work is to be confirmed.
- State changes on minimized windows were measured only on a classic console window (restore / maximize / minimize); close / move / resize on a minimized window are allowed by the code path but not tested on their own. `undo` of a window state change does not take the foreground protection and was not tested.
- Typing into a console: a newline (`\n`) in the text does not submit the command in Windows PowerShell inside conhost and leaves a stray character at the end of the line (measured on v1.0.5); use `key` with `enter` to submit a command.
- The `launch` guard and the `batch` allow-list limit what a model can do through the MCP tools; they are guard rails, not a sandbox. Another program of the same Windows user (including a shell the model can run itself) can talk to the named pipe or edit `settings.json` directly, and Deixion cannot defend against that. A program the model is allowed to start can itself start a shell.
- On power loss, a few of the most recent records may be lost. On a crash, up to 11 experience-store updates may not yet be on disk.
- Logging is asynchronous. If the process crashes, log lines still in the queue (up to about 500 ms) may be lost.
- The page list follows the code: there are 10 pages (Overview, Locate, Elements, Actions, Experience, Logs, Performance, Claude Code, Guide, Settings).

## Update: 1.0.6

Settings → Screenshots and logs → Model screenshot policy defaults to a screenshot before each action, with adaptive and on-demand options. The Claude skill reads `status.capture_policy`; this guides the model and does not prove image comprehension.

Fixed verified-update state loss, unrelated pixels in occluded-window capture, window-caption writes on unsupported web editors, and input on minimized windows. See the [control roadmap](docs/CONTROL-ROADMAP.md) for channel tradeoffs and proposed improvements. ChatGPT desktop background input remains incomplete; the PowerShell→Codex CLI weather example produced a result.
