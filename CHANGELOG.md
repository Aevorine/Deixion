# Changelog

All notable changes to Deixion. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow [SemVer](https://semver.org/). Release assets: `Deixion-Setup-x64.exe` and `SHA256SUMS.txt`.

## [Unreleased]

### Added
- Wiki sources in `docs/wiki/` and `tools/sync-wiki.mjs` to publish them to the GitHub Wiki.
- Open issues for the documented known limits (#4–#8).

### Changed
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

[1.0.4]: https://github.com/Aevorine/Deixion/releases/tag/v1.0.4
[1.0.3]: https://github.com/Aevorine/Deixion/releases/tag/v1.0.3
[1.0.2]: https://github.com/Aevorine/Deixion/releases/tag/v1.0.2
[1.0.1]: https://github.com/Aevorine/Deixion/commit/afe5468
[1.0.0]: https://github.com/Aevorine/Deixion/commit/634c489
