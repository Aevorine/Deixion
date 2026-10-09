# @aevorine/deixion

A small launcher for [Deixion](https://github.com/Aevorine/Deixion), which lets any Claude Code model operate Windows apps in the background. It contains no program code of its own. 这是 Deixion 的启动器，本身不含程序。

| Command | What it does |
|---|---|
| `npx @aevorine/deixion install` | Downloads `Deixion-Setup-x64.exe` from the GitHub Release, **verifies its SHA-256 against `SHA256SUMS.txt`**, then runs it. `--silent` installs without a window, `--version 1.0.5` picks a release, `--dry-run` only downloads and verifies. |
| `npx @aevorine/deixion mcp` | Starts the stdio MCP server of the installed `deixion-cli.exe`. |
| `npx @aevorine/deixion cli <args>` | Runs `deixion-cli.exe` with the given arguments. |
| `npx @aevorine/deixion status` | Shows where Deixion is installed and its version. |

The package version is the Release it was published from (the workflow takes it from the release tag, for example `v1.0.5` gives `1.0.5`). Without `--version`, `install` fetches the latest Release, not necessarily the one matching the package.

Register it with Claude Code:

```text
claude mcp add --scope user deixion -- npx -y @aevorine/deixion mcp
```

The installer's own registration (`claude mcp add … deixion-cli.exe mcp`) is more direct and starts faster; this launcher is for setups that prefer `npx`.

## Using the GitHub Packages registry

GitHub's npm registry needs a token even for public packages. Put this in your user `.npmrc` (a classic token with `read:packages`):

```ini
@aevorine:registry=https://npm.pkg.github.com
//npm.pkg.github.com/:_authToken=YOUR_TOKEN
```

Windows and Node.js 18 or newer are required. Source and issues: <https://github.com/Aevorine/Deixion>.
