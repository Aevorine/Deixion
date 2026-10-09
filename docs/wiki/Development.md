# Development / 开发

See also [CONTRIBUTING.md](https://github.com/Aevorine/Deixion/blob/main/CONTRIBUTING.md).

## Build

llvm-mingw (clang, UCRT, static), CMake ≥ 3.25, Ninja, Node.js. No MSVC, no runtime library to install.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_RC_COMPILER=llvm-windres
cmake --build build
```

CI does the same on a Windows runner with a pinned, hash-checked llvm-mingw and uploads the installer as an artifact.

## Layout

| Path | What |
|---|---|
| `src/core/` | Engine: UI Automation, capture, input, Meridian, experience store, journal, IPC, MCP |
| `src/app/` | Window, tray, hotkeys, WebView2 host, updater |
| `src/setup/` | Installer / uninstaller |
| `src/cli/` | `deixion-cli` and the stdio MCP server |
| `ui/` | The ten pages (plain ES modules) and the English dictionary |
| `skills/deixion/` | The Claude Code skill shipped with the installer |
| `tools/` | Packing, dev server, end-to-end scripts, i18n checker |

## Verification is end to end

No unit tests. Run the real thing and look at the result:

- `tools/e2e/mcp-e2e.mjs`, `mcp-stress.mjs`, `focus-e2e.ps1`: engine, input, and “the test target never becomes the foreground window”. The named pipe is one per user, so by default they talk to whatever Deixion is already running (an installed older version, say). Set `DX_E2E_ISOLATED=1` (`-Isolated` for `focus-e2e.ps1`) to run the freshly built engine inside the script's own process instead (`--inproc`, portable data under `.scratch/`); `DX_E2E_CLI` points that mode at another `deixion-cli.exe`.
- `tools/e2e/console-e2e.ps1`: PowerShell in a classic console window gets `-- ee --ee aa 11 ;;` typed in the background and must receive it verbatim; a minimized console is restored / maximized / minimized / restored while a sub-millisecond poller checks that the console never becomes the foreground window. Always isolated; `-Cli` compares against another build.
- `tools/e2e/e2e-setup.ps1`: install, upgrade over locked files, damaged-package rollback, uninstall. It sets `DEIXION_NO_CLAUDE=1` so the program leaves your Claude Code registration and skill alone, and it backs up and restores the real install's uninstall entry.
- `tools/e2e/i18n-e2e.mjs`: the real app in both languages through the WebView2 debugging port. Pick a port outside `netsh int ipv4 show excludedportrange protocol=tcp`.
- `node tools/i18n-check.mjs`: every Chinese string in the code has an English entry.

## Translations

Chinese is the source language. Wrap strings in `t('中文')`; add the English to `ui/js/i18n/en/<page>.js`. Never call `t()` at module top level. Native strings use `i18n::pick(lang, zh, en)`.

## Releases

The updater's contract is fixed: assets `Deixion-Setup-x64.exe` and `SHA256SUMS.txt`, installer flags `/S /UPDATE /RELAUNCH`. Bump the version in `CMakeLists.txt`, `res/app.manifest` and `tools/devserver.mjs`; build; `node tools/release.mjs pack`; `gh release create vX.Y.Z dist/Deixion-Setup-x64.exe dist/SHA256SUMS.txt`; `node tools/release.mjs prune 2` keeps the two newest releases.
