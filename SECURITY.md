# Security policy / 安全策略

## Supported versions / 支持的版本

Only the latest release receives fixes. The two newest releases are kept on the Releases page; an older release that is found to be unsafe is marked as superseded and replaced by a patch release instead of being rewritten.

只修复最新版本。Releases 页保留最近两版；发现旧版本有安全问题时，发补丁版并在旧版说明里标注“已被取代”，不改写已发布的版本。

## Reporting a vulnerability / 报告漏洞

Please report privately: **Security → Report a vulnerability** on this repository
(<https://github.com/Aevorine/Deixion/security/advisories/new>). Do not open a public issue for a vulnerability.

请私下报告：本仓库 **Security → Report a vulnerability**。不要用公开 issue 报告漏洞。

Helpful details: the Deixion version, the Windows version, the exact tool call or steps, and what an attacker gains. Reports are handled on a best-effort basis; there is no guaranteed response time.

有用的信息：Deixion 版本、Windows 版本、具体的工具调用或步骤，以及攻击者能得到什么。按尽力而为处理，不承诺响应时限。

## What is in scope / 范围

- Ways for an MCP client to do more than the documented tools allow (for example reaching `settings.set` through `batch`, operating Deixion's own windows, or starting a shell past the `launch` guard).
- The named pipe: a caller that is not the current Windows account being served.
- The installer and updater: running a package that does not match `SHA256SUMS.txt`, unsafe extraction paths, or a rollback that leaves a half-installed state.
- Anything that writes typed text, secrets or other private data to logs or the repository.

## Known limits that are not vulnerabilities / 已知限制

These are documented in the README under “Known limits”:

- The `launch` guard and the `batch` allow-list are guard rails, not a sandbox. Another program of the same Windows user can talk to the pipe or edit `settings.json`, and a program the model may start can start a shell itself.
- The installer is not code-signed, so SmartScreen may warn on first run.

## Handling of secrets / 密钥处理

Passwords, keys and signing material never go into the repository or logs. Release assets are checked against `SHA256SUMS.txt` by the updater before they run.
