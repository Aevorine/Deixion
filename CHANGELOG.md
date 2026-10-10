# Changelog

All notable changes to Deixion. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow [SemVer](https://semver.org/). Release assets: `Deixion-Setup-x64.exe` and `SHA256SUMS.txt`.

## [Unreleased]

## [1.0.7] - 2026-10-10

### Fixed
- Background typing into Chromium-based windows (Edge, Electron apps) reached the page only by luck. Keyboard messages are now delivered to the top-level `Chrome_WidgetWin_*` window (the content child window dropped them), a background click gives the page its focus first, `Enter` / `Tab` / `Space` are sent as real key events so a text area gets one newline instead of two, and `replace` selects all and types instead of writing through UI Automation (which fires no `input` event on `contenteditable`). Measured on an Edge test page: `contenteditable`, `textarea` and `input` receive ASCII, CJK, emoji and newlines; append and replace both work. ChatGPT desktop itself was not exercised in this release.
- The accessibility tree of a Chromium window is built asynchronously after the first client asks; the first query used to see only the browser chrome and `find` failed with `not_found`. The first snapshot now waits briefly for the web document, and `find` retries for up to 1.5 s on Chromium windows.
- `launch` of a path that does not exist returns `not_found` at once. It used to raise a modal Windows error box that held the call for about two minutes.
- The `launch` guard resolves `.lnk` shortcuts and `%VAR%` paths and recognizes more script and launcher extensions (`.py`, `.rb`, `.pl`, `.php`, `.lua`, `.sh`, `.ahk`, `.au3`, `.jnlp`, `.xll`, `.chm`, …), Windows Terminal, `ssh` / `scp` / `sftp`, `reg`, WSL distribution launchers and Chromium command-prefix flags (`--renderer-cmd-prefix` and similar). The guard remains a guard rail, not a sandbox.
- A crash at exit (`0xc0000409`) when the update worker was still running; the updater now cancels its in-flight request and joins before the process ends.
- Typed `\n` in a classic console window now commits the command as a real `Enter` key instead of leaving a stray character.
- `set_value` on a slider (`RangeValue`) is now undoable; `undo` of a window state change holds the foreground lock like the original operation.
- Non-finite coordinates (`nan`, `inf`) are rejected; lone UTF-16 surrogates in JSON no longer corrupt parsing.
- Window operations on Deixion's own windows are refused like other input actions.

### Security
- The installer keeps the WebView2 bootstrapper under an unpredictable name and holds it read-locked from the signature check until it has run, so the verified bytes are the executed bytes.
- Named pipe: frames are read incrementally with a size bound, at most 64 clients, a failure to build the security descriptor stops the listener instead of opening it wider, and handler exceptions are contained.
- WebView: navigation away from the application origin is cancelled and web messages from any other origin are ignored.
- Updater: the installer download is capped at 256 MiB and re-hashed right before it is started, then compared with the hash that was verified.
- `claude mcp` helper calls end after 30 s instead of waiting forever.
- npm launcher: `reg.exe` is called by absolute path and downloads are size-capped (256 MiB installer, 1 MiB checksum file).

### Added
- `tools/e2e/hardening-e2e.mjs`: end-to-end regression for the items above (isolated mode, Edge test page, slider target); `console-e2e.ps1` covers the typed newline; `mcp-e2e.mjs` covers refusal of window operations on Deixion's own windows.
- The test target has a slider (trackbar).

## [1.0.6] - 2026-10-09

### Fixed
- Checking again after a verified update is ready retains the installer and the install button.
- Update worker joins happen outside the state mutex to avoid blocking the worker's final notification.
- GitHub update host checks require an exact domain or a subdomain boundary.
- Background capture refuses a screen fallback when another window covers the target; unrelated pixels are never reported as its screenshot.
- The `set_value` message fallback only writes to native Edit / RichEdit controls and refuses read-only controls. It no longer changes a Chromium window caption when a web input rejects UI Automation.
- Minimized windows refuse coordinate input even when Windows reports a nonempty client rectangle. Restore operations remain supported.

### Added
- Persisted, bilingual model screenshot policy: `before_each` (default), `adaptive`, `off`. MCP status exposes it and the bundled skill describes the screenshot / inspect / action loop. The policy is model guidance, not proof of image comprehension.
- End-to-end regression coverage for update readiness, non-edit value writes and minimized input.
- Application compatibility and performance roadmap in `docs/CONTROL-ROADMAP.md`.

### Changed
- Dynamic application names and window titles are marked as user content so language checks do not mistake them for untranslated interface strings.

## [1.0.5] - 2026-10-09

### Fixed
- Typing into a classic console window (the conhost window that hosts PowerShell or cmd) no longer duplicates characters. The console merges adjacent identical key events, so `--` arrived as `---` and `ee` as `eee`; text sent to such a window is now followed by a key-up message per character. Measured: `'-- ee --ee aa 11 ;;'` typed into Windows PowerShell inside conhost came out verbatim 3 of 3 times (1.0.4: 3 of 3 corrupted). Windows Terminal windows were not tested.
- `window_op` works on minimized windows. `restore` and `maximize` used to fail with "window is minimized; restore it first". In background mode the foreground lock is held while the state changes, the window you were using is put back if the target activated itself, and a window restored from minimized is placed directly beneath yours. Measured on a console window: restore, maximize, minimize and restore again, with your window staying in front and the console never becoming the foreground window.

### Added
- Wiki sources in `docs/wiki/` and `tools/sync-wiki.mjs` to publish them to the GitHub Wiki.
- Open issues for the documented known limits (#4–#8).
- `brand/social-preview.png`, the 1280×640 repository card (Settings → Social preview).
- GitHub Package `@aevorine/deixion` (npm launcher, `packages/npm`): `install` downloads the release and verifies its SHA-256 against `SHA256SUMS.txt` before running it; `mcp` / `cli` forward to the installed `deixion-cli.exe`. Published by `.github/workflows/package.yml` when a release is published.
- `tools/e2e/console-e2e.ps1`: end-to-end check of console typing and of restoring / maximizing / minimizing a minimized console (always isolated; `-Cli` compares another build).

### Changed
- End-to-end scripts: `DX_E2E_ISOLATED=1` (`-Isolated` for `focus-e2e.ps1`) runs the freshly built engine inside the script's own process instead of connecting to whatever Deixion is already running, and `focus-e2e.ps1` only ends processes under the project folder.
- `DEIXION_NO_CLAUDE=1` makes `--connect-claude` / `--disconnect-claude` do nothing; the installer end-to-end script sets it and restores the real uninstall entry, so running it no longer disconnects your own Claude Code.

## [1.0.4] - 2026-10-09

### Added
- **Chinese and English UI.** The window, the tray menu and the native notices come in both languages. The default follows the Windows display language (Chinese systems get Chinese, everything else gets English); Settings → Appearance and system → Language switches it by hand and reloads the window at once. New setting `language` (`auto` / `zh` / `en`).
- `tools/i18n-check.mjs` (every Chinese string in the code needs an English entry) and `tools/e2e/i18n-e2e.mjs` (drives the real app through the WebView2 debugging port, page by page, in both languages).
- Community files, issue and pull request templates, security policy and a CI workflow.

## [1.0.3] - 2026-10-09

### Added
- Optional strict launch mode: `launch_strict` with `launch_allow` limits `launch` to programs the user lists (plus http/https/mailto/ms-settings links). Editable in Settings.

## [1.0.2] - 2026-10-09

### Security
- Closed bypasses in the `launch` guard: `file:` URLs and names with trailing dots started `cmd.exe` in 1.0.1. The guard now checks the resolved file name, covers interpreters, program-running system tools, script and shortcut types, refuses other link schemes and inspects the program named in `explorer.exe` arguments. Twelve bypass attempts are probed end to end.

## [1.0.1] - 2026-10-09

### Fixed
- Background actions keep the user's foreground window: a foreground lock is held during background channels and the user's window is put back if the target still took it; the experience store penalises channels that disturbed the foreground.
- Classic Win32 Button / Edit controls are driven with `BM_CLICK` / `WM_SETTEXT` (5–10× faster and without activating the target).
- Key chords and window-level typing reach the control last clicked or typed into.
- `launch` refuses Deixion's own programs and, unless enabled, command shells and script hosts.

## [1.0.0] - 2026-10-09

### Added
- C++23 core: UI Automation, capture, input, Meridian coordinates, experience store, rollback journal, IPC and an MCP (stdio) server.
- WebView2 desktop shell with ten pages, tray, hotkeys and single instance.
- `deixion-cli`, a per-user installer with two-phase commit and rollback, a GitHub-Release updater, and the Claude Code skill.

### Security
- `batch` is limited to the actions and queries MCP already exposes; input actions cannot operate Deixion's own windows; the pipe client verifies the server runs as the current account.

[1.0.5]: https://github.com/Aevorine/Deixion/releases/tag/v1.0.5
[1.0.6]: https://github.com/Aevorine/Deixion/releases/tag/v1.0.6
[1.0.7]: https://github.com/Aevorine/Deixion/releases/tag/v1.0.7
[1.0.4]: https://github.com/Aevorine/Deixion/releases/tag/v1.0.4
[1.0.3]: https://github.com/Aevorine/Deixion/releases/tag/v1.0.3
[1.0.2]: https://github.com/Aevorine/Deixion/releases/tag/v1.0.2
[1.0.1]: https://github.com/Aevorine/Deixion/commit/afe5468
[1.0.0]: https://github.com/Aevorine/Deixion/commit/634c489
