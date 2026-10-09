// 界面开发服务器：用普通浏览器打开 ui/，引擎方法经命名管道转给正在运行的 Deixion.exe（没运行会先启动托盘实例）。
// app.* / update.* / claude.* 是原生窗口才有的能力，这里给出最小的占位应答，只为让界面在浏览器里能完整渲染。
// 用法：node tools/devserver.mjs [端口，默认 5173]
import http from 'node:http';
import net from 'node:net';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const uiDir = path.join(root, 'ui');
const port = Number(process.argv[2]) || 5173;
const pipeName = `\\\\.\\pipe\\Deixion-v1-${os.userInfo().username}`;
const MIME = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.json': 'application/json', '.png': 'image/png', '.svg': 'image/svg+xml', '.woff2': 'font/woff2', '.ico': 'image/x-icon' };

function pipeCall(method, params) {
  return new Promise((resolve, reject) => {
    const s = net.connect(pipeName);
    const chunks = [];
    let need = -1;
    let got = 0;
    s.on('connect', () => {
      const body = Buffer.from(JSON.stringify({ id: 1, method, params }), 'utf8');
      const head = Buffer.alloc(4);
      head.writeUInt32LE(body.length);
      s.write(Buffer.concat([head, body]));
    });
    s.on('data', (d) => {
      chunks.push(d);
      got += d.length;
      const buf = Buffer.concat(chunks);
      if (need < 0 && buf.length >= 4) need = buf.readUInt32LE(0);
      if (need >= 0 && buf.length >= 4 + need) {
        s.end();
        try { resolve(JSON.parse(buf.subarray(4, 4 + need).toString('utf8'))); } catch (e) { reject(e); }
      }
    });
    s.on('error', reject);
    setTimeout(() => { s.destroy(); reject(new Error('引擎管道超时')); }, 60000);
  });
}

let started = false;
let aliveAt = 0;
async function ensureApp() {
  if (Date.now() - aliveAt < 10000) return;
  try { await pipeCall('ping', {}); aliveAt = Date.now(); return; } catch { /* 没在运行 */ }
  if (started) return;
  started = true;
  const exe = path.join(root, 'build', 'Deixion.exe');
  if (fs.existsSync(exe)) spawn(exe, ['--tray'], { detached: true, stdio: 'ignore', env: { ...process.env, DEIXION_UI_DIR: uiDir } }).unref();
  for (let i = 0; i < 40; i++) {
    await new Promise((r) => setTimeout(r, 150));
    try { await pipeCall('ping', {}); return; } catch { /* 继续等 */ }
  }
}

const mockUpdate = { state: 'current', current: '1.0.3', latest: '', notes: '', page: '', error: '', got: 0, total: 0, portable: false, repo: 'Aevorine/Deixion' };
async function rpc(m, p) {
  if (m === 'app.info') {
    const st = (await pipeCall('status', {})).result || {};
    const sg = (await pipeCall('settings.get', {})).result || {};
    return { ok: true, r: { version: st.version, webview: 'dev', portable: st.portable, exe: '', data_dir: st.data_dir, hotkeys: Object.entries(sg.settings?.hotkeys || {}).map(([name, chord]) => ({ name, chord, ok: true })), ui_from_disk: true, clients: 0, autostart_registered: false, system_dark: true, update: mockUpdate, maximized: false } };
  }
  if (m.startsWith('app.')) return { ok: true, r: {} };
  if (m === 'update.state' || m === 'update.check') return { ok: true, r: mockUpdate };
  if (m.startsWith('update.')) return { ok: true, r: mockUpdate };
  if (m === 'claude.status') return { ok: true, r: { cli_found: true, cli_path: path.join(os.homedir(), 'AppData', 'Roaming', 'npm', 'claude.cmd'), bridge_exe: path.join(root, 'build', 'deixion-cli.exe'), bridge_exists: true, mcp_registered: true, mcp_path_ok: true, mcp_command: path.join(root, 'build', 'deixion-cli.exe'), skill_installed: true, skill_current: true, skill_available: true, skill_dir: path.join(os.homedir(), '.claude', 'skills', 'deixion'), clients: 1 } };
  if (m === 'claude.config') return { ok: true, r: { json: JSON.stringify({ mcpServers: { deixion: { command: path.join(root, 'build', 'deixion-cli.exe'), args: ['mcp'] } } }), command_line: `claude mcp add --scope user deixion -- "${path.join(root, 'build', 'deixion-cli.exe')}" mcp` } };
  if (m.startsWith('claude.')) return { ok: true, r: { mcp: 'registered', skill: 'installed' } };
  const resp = await pipeCall(m, p);
  return resp.ok ? { ok: true, r: resp.result } : { ok: false, e: { code: resp.error?.code, msg: resp.error?.message } };
}

http.createServer(async (req, res) => {
  try {
    if (req.method === 'POST' && req.url === '/rpc') {
      const chunks = [];
      for await (const c of req) chunks.push(c);
      const { m, p } = JSON.parse(Buffer.concat(chunks).toString('utf8'));
      await ensureApp();
      const out = await rpc(m, p || {}).catch((e) => ({ ok: false, e: { code: 'internal', msg: e.message } }));
      res.writeHead(200, { 'content-type': 'application/json' });
      res.end(JSON.stringify(out));
      return;
    }
    let rel = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    if (rel === '/') rel = '/index.html';
    const file = path.join(uiDir, rel);
    if (!file.startsWith(uiDir) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) { res.writeHead(404); res.end('not found'); return; }
    res.writeHead(200, { 'content-type': MIME[path.extname(file)] || 'application/octet-stream', 'cache-control': 'no-store' });
    fs.createReadStream(file).pipe(res);
  } catch (e) {
    res.writeHead(500); res.end(String(e.message));
  }
}).listen(port, '127.0.0.1', () => console.log(`Deixion UI dev server: http://127.0.0.1:${port}/`));
