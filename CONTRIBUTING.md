# Contributing / 参与贡献

Thanks for helping. Deixion lets any Claude Code model operate Windows apps in the background, so a few constraints are firm. 感谢参与。下面几条是硬约束。

## Build / 构建

Toolchain: llvm-mingw (clang, UCRT, static linking), CMake ≥ 3.25, Ninja, Node.js. There is no MSVC and no runtime library to install; keep it that way, the goal is “download and run on any Windows machine”.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_RC_COMPILER=llvm-windres
cmake --build build
```

Targets: `Deixion.exe` (UI + tray), `deixion-cli.exe` (CLI and MCP stdio server), `Deixion-Setup-x64.exe` (installer), `dx-testapp.exe` (end-to-end test target, built on request). Stop any running `Deixion` / `deixion-cli` / `dx-testapp` before building, or the link step fails with “permission denied”.

## Layout / 目录

| Path | What |
|---|---|
| `src/core/` | Engine: UI Automation, capture, input, Meridian coordinates, experience store, journal, IPC, MCP |
| `src/app/` | Desktop shell: window, tray, hotkeys, WebView2 host, updater |
| `src/setup/` | Installer / uninstaller |
| `src/cli/` | `deixion-cli` |
| `ui/` | The WebView2 pages (plain ES modules, no build step) |
| `skills/deixion/` | The Claude Code skill shipped with the installer |
| `tools/` | Packing scripts, dev server, end-to-end scripts |

## Tests are end to end / 只做端到端验证

There are no unit or integration tests, by design. Verify a change the way a user meets it: the real executable, real input, and a result you can see.

- `tools/e2e/mcp-e2e.mjs`, `mcp-stress.mjs`, `focus-e2e.ps1`: engine, input and the “never steal the foreground” guarantee.
- `tools/e2e/e2e-setup.ps1`: install, upgrade over locked files, damaged package rollback, uninstall.
- `tools/e2e/i18n-e2e.mjs`: the UI in both languages, page by page.

## Languages / 双语界面

The UI, the tray menu and native notices exist in Chinese and English. The default follows the Windows display language; Settings can override it.

- UI strings are written in Chinese in the code and wrapped in `t('…')`; the English text lives in `ui/js/i18n/en/*.js`, keyed by the Chinese original.
- Never call `t()` at module top level (the language is not known yet). Keep constants in Chinese and translate where they are rendered.
- Native strings use `i18n::pick(lang, zh, en)` in `src/core/base/i18n.hpp`.
- `node tools/i18n-check.mjs` must pass; add `--unwrapped` to list Chinese literals that are not directly inside `t()`.

## Rules that keep the product honest / 产品底线

- Background mode never moves the user's real cursor and never takes focus.
- The release contract is fixed: `Deixion-Setup-x64.exe`, `SHA256SUMS.txt`, and the installer accepting `/S /UPDATE /RELAUNCH`. Renaming any of them breaks self-update for installed copies.
- UI look: Chinese in SimSun, Western text and punctuation in Times New Roman, formulas with KaTeX, no explanatory text on pages (tooltips name the icons), one visual style everywhere.
- Never commit secrets, signing material, personal paths or private window titles.

## Pull requests / 提交流程

1. Open an issue first for anything larger than a fix.
2. Keep one change per pull request; describe how you verified it end to end.
3. Commit messages: `type: summary` (`feat`, `fix`, `docs`, `chore`), then a short body that says why.
