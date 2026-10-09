# Troubleshooting / 常见问题

**The window is blank or says it needs WebView2.**
The Microsoft WebView2 runtime is missing. Install it from <https://go.microsoft.com/fwlink/p/?LinkId=2124703>. Until then the tray menu, hotkeys, MCP and the CLI keep working.

**Windows SmartScreen warns about the installer.**
The installer is not code-signed yet (tracked in [#5](https://github.com/Aevorine/Deixion/issues/5)). Compare the file with `SHA256SUMS.txt` on the Releases page.

**Claude Code does not see Deixion.**
Restart Claude Code after installing or updating. On the **Claude Code** page check that the MCP server path points to the current install (“Path outdated” means it does not) and click *Connect*. If the `claude` command is not found, the page shows the JSON and command line to register by hand.

**An action failed in background mode.**
Some programs ignore window messages or expose no UI Automation patterns. The failure message says why. You can target another element, or enable `allow_hop` so Deixion may hop to the foreground briefly as a last resort. Behaviour on WPF, Electron, UWP and games is still being verified ([#4](https://github.com/Aevorine/Deixion/issues/4)).

**`launch` was refused.**
By design: `launch` will not start command shells, script hosts or Deixion's own programs unless you enable `allow_shell_launch`, and with `launch_strict` it only starts programs on your list (Settings → Allowed programs). This is a guard rail, not a sandbox.

**A hotkey does nothing.**
Another program owns the chord. Settings shows “In use by another program”; record a different chord.

**The interface is in the wrong language.**
Settings → Appearance and system → Language. `auto` follows the Windows display language.

**Where are the logs and my data?**
Settings → Data folder → *Open*: `%LOCALAPPDATA%\Deixion` (`data` next to the exe for a portable copy). Typed text is never logged. Uninstalling keeps this folder unless you pass `/PURGE`.

**How do I uninstall?**
Apps → Deixion, or run `uninstall.exe` in the install folder. It also removes the MCP registration and the skill. Silent: `Deixion-Setup-x64.exe /S /UNINSTALL [/PURGE]`.

**Something else.**
Open an [issue](https://github.com/Aevorine/Deixion/issues/new/choose) with the version, the Windows version, the exact tool call and the relevant log lines. Report vulnerabilities privately (see the [security policy](https://github.com/Aevorine/Deixion/security/policy)).
