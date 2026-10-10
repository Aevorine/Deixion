#!/usr/bin/env node
// Deixion 启动器：不含程序本体。install 从 GitHub Release 下载安装包，按 SHA256SUMS.txt 校验后才运行（与应用内更新器同一套信任规则）；
// mcp / cli 把参数转给已安装的 deixion-cli.exe。没有第三方依赖。
'use strict';
const { spawn, spawnSync } = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const https = require('node:https');
const os = require('node:os');
const path = require('node:path');

const REPO = 'Aevorine/Deixion';
const ASSET = 'Deixion-Setup-x64.exe';
const SUMS = 'SHA256SUMS.txt';
const HOSTS = [/^github\.com$/, /(^|\.)githubusercontent\.com$/];

const out = (s) => process.stdout.write(s + '\n');
const die = (s, code = 1) => { process.stderr.write(s + '\n'); process.exit(code); };

/** 已安装的目录：先看卸载注册表项，再看默认位置。 */
function installDir() {
  const cands = [];
  // 用系统目录里的 reg.exe 的绝对路径：不走 PATH 查找，当前目录或 PATH 里被放了同名程序也不会被执行。
  const reg = path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'reg.exe');
  const r = spawnSync(reg, ['query', 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Deixion', '/v', 'InstallLocation'], { encoding: 'utf8' });
  const m = /InstallLocation\s+REG_SZ\s+(.+)/.exec(r.stdout || '');
  if (m) cands.push(m[1].trim());
  if (process.env.LOCALAPPDATA) cands.push(path.join(process.env.LOCALAPPDATA, 'Programs', 'Deixion'));
  return cands.find((d) => fs.existsSync(path.join(d, 'deixion-cli.exe'))) || null;
}

// 下载上限：安装包 256 MiB、校验和文件 1 MiB。超过就中止，不会被一个无限长的响应撑爆内存。
function get(url, maxBytes, hops = 0) {
  return new Promise((resolve, reject) => {
    const u = new URL(url);
    if (u.protocol !== 'https:' || !HOSTS.some((re) => re.test(u.hostname))) return reject(new Error('refusing a non-GitHub address: ' + u.origin));
    const req = https.get(u, { headers: { 'user-agent': 'deixion-launcher' } }, (res) => {
      if ([301, 302, 303, 307, 308].includes(res.statusCode) && res.headers.location && hops < 5) { res.resume(); return resolve(get(new URL(res.headers.location, u).href, maxBytes, hops + 1)); }
      if (res.statusCode !== 200) { res.resume(); return reject(new Error(`HTTP ${res.statusCode} for ${u.href}`)); }
      const declared = Number(res.headers['content-length']);
      if (declared > maxBytes) { res.resume(); return reject(new Error(`refusing a ${declared}-byte download (limit ${maxBytes})`)); }
      const chunks = [];
      let size = 0;
      res.on('data', (c) => {
        size += c.length;
        if (size > maxBytes) { req.destroy(); return reject(new Error(`download exceeded ${maxBytes} bytes, aborted`)); }
        chunks.push(c);
      });
      res.on('end', () => resolve(Buffer.concat(chunks)));
      res.on('error', reject);
    });
    req.on('error', reject);
  });
}

async function install(args) {
  const silent = args.includes('--silent');
  const dry = args.includes('--dry-run');
  const vi = args.indexOf('--version');
  const tag = vi >= 0 && args[vi + 1] ? 'v' + args[vi + 1].replace(/^v/, '') : null;
  const base = tag ? `https://github.com/${REPO}/releases/download/${tag}` : `https://github.com/${REPO}/releases/latest/download`;
  out(`Downloading ${ASSET} (${tag || 'latest'}) …`);
  const [exe, sums] = await Promise.all([get(`${base}/${ASSET}`, 256 << 20), get(`${base}/${SUMS}`, 1 << 20)]);
  const want = new RegExp(`^([0-9a-f]{64})\\s+\\*?${ASSET.replace('.', '\\.')}\\s*$`, 'im').exec(sums.toString('utf8'));
  if (!want) die(`${SUMS} has no entry for ${ASSET}`);
  const got = crypto.createHash('sha256').update(exe).digest('hex');
  if (got !== want[1].toLowerCase()) die(`SHA-256 mismatch: expected ${want[1]}, got ${got}. Nothing was run.`);
  out(`SHA-256 verified: ${got}`);
  if (dry) return out('Dry run: the installer was not started.');
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'deixion-install-'));
  const file = path.join(dir, ASSET);
  fs.writeFileSync(file, exe);
  out(silent ? 'Installing silently …' : 'Starting the installer …');
  const p = spawn(file, silent ? ['/S'] : [], { stdio: 'inherit' });
  p.on('exit', (code) => { try { fs.rmSync(dir, { recursive: true, force: true }); } catch { /* 安装程序可能还占着文件 */ } process.exit(code ?? 1); });
}

function forward(args) {
  const dir = installDir();
  if (!dir) die('Deixion is not installed. Run:  npx @aevorine/deixion install', 3);
  const p = spawn(path.join(dir, 'deixion-cli.exe'), args, { stdio: 'inherit' });
  p.on('exit', (code) => process.exit(code ?? 1));
  p.on('error', (e) => die('cannot start deixion-cli.exe: ' + e.message));
}

function help() {
  out(`deixion — launcher for ${REPO}

  install [--silent] [--version X.Y.Z] [--dry-run]   download the release, verify SHA-256, run the installer
  mcp                                                start the MCP server (stdio) for Claude Code
  cli <args…>                                        run deixion-cli.exe with the given arguments
  status                                             show where Deixion is installed
  help

Register with Claude Code:
  claude mcp add --scope user deixion -- npx -y @aevorine/deixion mcp`);
}

(async () => {
  const [cmd = 'help', ...args] = process.argv.slice(2);
  if (process.platform !== 'win32' && cmd !== 'help') die('Deixion runs on Windows only.');
  try {
    if (cmd === 'install') await install(args);
    else if (cmd === 'mcp') forward(['mcp']);
    else if (cmd === 'cli') forward(args);
    else if (cmd === 'status') {
      const dir = installDir();
      if (!dir) return out('Deixion is not installed.');
      out(`Installed at ${dir}`);
      forward(['version']);
    } else help();
  } catch (e) { die(e.message); }
})();
