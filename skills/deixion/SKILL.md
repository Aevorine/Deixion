---
name: deixion
description: Operate Windows desktop applications in the background with millisecond latency — list windows, read UI Automation elements, click, type, press keys, scroll, drag, set values, take screenshots with a coordinate grid, launch programs, wait for the UI to settle, and undo. Use whenever a task needs to control, read, test or automate a native Windows app or any window on screen (Notepad, Calculator, Explorer, Office, browsers, installers, settings dialogs, games' launchers) without taking over the user's mouse, keyboard or focus. Requires the Deixion MCP server (tools named mcp__deixion__*) or the deixion-cli command.
---

# Deixion — background control of Windows apps

Deixion drives other applications through UI Automation, window messages and direct value setting. In **background mode** (default) it never moves the user's cursor and never steals focus, so the user can keep working while you operate. Each action takes milliseconds.

## Pick the target window

`window` is accepted by every tool: a title fragment, `exe:notepad.exe`, `class:Notepad`, `hwnd:0x1A2B`, `active`, or `screen`.

Start with `windows` (optionally `filter`) to see handles, exe names, titles and client sizes. Prefer `exe:` or `hwnd:` over a title fragment when several windows could match.

## Choose how to point at things (best first)

1. **Element id** — `elements` or `screenshot` with `elements:true` returns ids like `e12`. Exact, survives resizing, works while the window is hidden or covered. Use `element:"e12"`.
2. **`find`** — `find:{text:"Save", role:"Button"}` resolves the element at call time. Use it inside `batch` so ids are not stale.
3. **Meridian coordinates** — for custom-drawn UIs with no automation tree (games, canvases, some Electron views). `at:"0.48,0.13"` means λ (x) = 0.48, φ (y) = 0.13, both in [0,1], origin at the top-left of the window's client area. The same pair points at the same content at any resolution, DPI or window size.
4. **Meridian code** — `code:"k3m"` is a 1–6 character hierarchical cell (coarse to fine; a longer code is a smaller cell, and its prefix is the enclosing cell). The click goes to the cell centre.
5. **`px`** (client pixels) or **`screen`** (screen pixels) — last resort.

Reading a screenshot: the grid has lines every 0.1; labels give λ across the top and φ down the left side. To refine, call `screenshot` again with `region:{a:"0.4,0.1", b:"0.6,0.2"}` or `code:`; labels stay in full-window coordinates, so you can click straight from the zoomed image.

## The loop

Call `status` first and follow `capture_policy`. The default `before_each` requires a fresh screenshot, image inspection and target selection before each input action. Do not batch multiple input actions under this policy: the model must inspect each new image before deciding the next action. `adaptive` permits element-based batches but requires a new image after layout changes; `off` captures on demand. This is a model workflow preference, not an engine guarantee that the model viewed the image. Normalized coordinates describe the current client rectangle; responsive layouts still require fresh localization after resizing.

1. `windows` → pick the window.
2. `elements` (filtered with `query`/`role`, or `interactive:true`) → ids and centres. Use `screenshot` with `elements:true` only when you need to see the layout; the element list is cheaper and exact.
3. In `before_each`, inspect a fresh screenshot before each input and issue one input action at a time. With `adaptive` or `off`, use **one `batch`** for a stable sequence — one round trip, milliseconds per step:
   ```json
   {"defaults":{"window":"exe:notepad.exe"},
    "steps":[
      {"do":"click","find":{"text":"File","role":"MenuItem"}},
      {"do":"wait","for":"settle"},
      {"do":"type","text":"hello","replace":true},
      {"do":"key","keys":"ctrl+s"}
    ]}
   ```
4. **Wait on state, never on time.** Use `wait` with `for`: `settle` (UI stopped changing), `element` (appears), `gone` (disappears), `window` (appears). Do not insert fixed sleeps.
5. **Verify** with `read` (value, text or toggle state of an element) or a fresh `elements` call. Do not assume a click worked.
6. If it went wrong, `undo` rolls back the last N reversible actions (value changes, toggles, window moves; typed text is best effort). `status` with `detail:"journal"` lists what can be undone.

## Tool reference

| Tool | Use |
|---|---|
| `windows` | List top-level windows. Always the first call. |
| `elements` | UI Automation elements: id, role, name, centre `@λ,φ`, supported patterns. Cached 350 ms; `refresh:true` bypasses. |
| `find` | Ranked fuzzy matches by `text`, `role`, `aid` (exact AutomationId). |
| `locate` | What is at this point? Returns the element, child window class and Meridian codes. |
| `screenshot` | JPEG with Meridian grid. Works on background windows. `elements`, `marks`, `region`, `code`, `grid`, `max_dim`. |
| `click` | `button` left/right/middle, `count` 1–3, `hover` to move only. |
| `type` | Types into the target (focused first) or the focused control; `replace:true` overwrites the whole value. |
| `key` | `"enter"`, `"ctrl+s"`, `"f5"`, or a sequence `"ctrl+a ctrl+c"`; `repeat`. |
| `scroll` | `dy` > 0 down, `dx` > 0 right, in wheel notches; at a point or window centre. |
| `drag` | `from` / `to` accept the same target fields as `click`; `steps` for intermediate moves. |
| `set_value` | Set a field or slider directly through UI Automation, no typing. Undoable. Fastest way to fill fields. |
| `read` | Value, text or toggle state of an element. |
| `window_op` | `minimize`, `maximize`, `restore`, `close`, `move`, `resize`, `topmost`, `focus` (focus needs the user's permission). |
| `launch` | Start a program, document or URL without stealing focus; returns pid and the window once it appears. It refuses Deixion's own programs and, unless the user enabled `allow_shell_launch`, command shells, script hosts and interpreters (`cmd`, PowerShell, `wscript`, Python, Node, `.bat`, `.ps1`, `.url`, …) and link schemes other than `http`, `https`, `mailto` and `ms-settings`; when the user turned on strict mode, only the programs on their list can be started. If `launch` is refused, do not try to get around it; tell the user which program you need. |
| `wait` | `settle`, `element`, `gone`, `window`; `timeout_ms` (default 5000), `quiet_ms`. |
| `batch` | Many steps in one call; `defaults` merged into every step; `stop_on_error` (default true). |
| `undo` | Roll back the last `count` actions or a specific journal `id`. |
| `status` | Mode, paused flag, learned strategy statistics; `detail`: `journal`, `experience`, `perf`, `log`. |

## Rules that matter

- **Prefer values over keystrokes.** `set_value` and element-targeted `click` go through UI Automation and are exact; `type` and `key` go through the input path and can be disturbed if the user is typing elsewhere.
- **Key chords with modifiers** (`ctrl+…`, `alt+…`) may need a brief foreground fallback in background mode. If a chord does nothing, look for the equivalent menu item with `find` and click it instead.
- **Never move or focus anything just to look.** `screenshot`, `elements`, `read` and `locate` are passive and safe on hidden or covered windows.
- **Do not guess coordinates from memory of a previous layout.** Re-read `elements` or take a new screenshot after anything that can reshape the window.
- **`batch` is for actions and queries only.** Changing settings, clearing the journal, resetting experience and the emergency stop are the user's controls and are refused inside `batch`; do not try to route around the pause or the foreground switches.
- **Deixion's own windows are off limits to input actions.** `click`, `type`, `key` and the like are refused on them; reading them with `elements` or `screenshot` is fine. Do not try to operate Deixion's settings through its UI.
- **Respect pause and mode.** If `status` shows `paused: true`, the user has paused automation — stop and tell them rather than working around it. In foreground mode actions are visible; that is the user's choice.
- **The experience database learns which strategy works for each application and control.** You do not need to manage it; `status detail:"experience"` shows what it has learned if you are debugging a flaky target.
- **Destructive actions** (closing unsaved documents, deleting, sending, paying, installing, changing system settings) need the user's go-ahead first, exactly as if you were at their keyboard.

## If the MCP tools are not available

The same engine is reachable from a shell:

```
deixion-cli windows
deixion-cli elements "exe:notepad.exe"
deixion-cli shot "exe:notepad.exe" -o shot.jpg
deixion-cli call click '{"window":"exe:notepad.exe","find":{"text":"Save"}}'
deixion-cli status
deixion-cli doctor
```

`deixion-cli doctor` checks Windows, the CPU, the WebView2 runtime, engine and UI Automation reachability, and whether the `claude` CLI is on PATH. To (re)connect, open Deixion → the Claude Code page and use the one-click connect, or run `claude mcp add --scope user deixion -- "<install dir>\deixion-cli.exe" mcp`.
