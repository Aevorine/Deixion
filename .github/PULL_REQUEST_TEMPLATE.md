## What and why

<!-- One or two sentences. Link the issue: Fixes #123 -->

## How it was checked

Deixion is verified end to end only (real executable, real input, user-visible result). Tick what applies and say how:

- [ ] Built with `cmake --build build` and ran the affected path in the real app
- [ ] `node tools/i18n-check.mjs` passes (every user-visible Chinese string has an English entry)
- [ ] `node tools/e2e/i18n-e2e.mjs` passes if the UI or settings changed
- [ ] `tools/e2e/mcp-e2e.mjs` / `focus-e2e.ps1` / `e2e-setup.ps1` pass if the engine, input or installer changed

## Checklist

- [ ] Background mode still does not move the cursor or take focus
- [ ] The release contract is untouched (`Deixion-Setup-x64.exe`, `SHA256SUMS.txt`, `/S /UPDATE /RELAUNCH`)
- [ ] No secrets, tokens, personal paths or private window titles in code, logs or screenshots
- [ ] README (both languages) updated if behaviour or settings changed
