// 更新器端到端：真实 Deixion.exe（便携模式，放在临时目录，不碰你的设置与数据）经 WebView2 调试端口，
// 用界面自己的桥调 update.check / update.download / update.state / update.apply，真实联网下载并校验。
// 重点：已下载并校验通过（ready）之后再检查一次，已校验的安装包不能被丢掉，界面仍然是「安装并重启」。
// 用法：node tools/e2e/updater-e2e.mjs <构建目录> [--expect-bug]
//   构建目录里的 Deixion.exe 版本必须低于 GitHub 上最新的 Release，否则没有可下载的新版本。
//   例：cmake 加 -DCMAKE_PROJECT_Deixion_INCLUDE=<把 PROJECT_VERSION 设成 1.0.4 的 cmake 文件>，只构建 Deixion 目标。
//   --expect-bug：对照实验，期望再检查后状态丢失（用来证明这个测试抓得到旧缺陷）。
//   先退出正在运行的 Deixion（单实例互斥量是全局的）：Deixion.exe --quit
import { spawn, spawnSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const args = process.argv.slice(2);
const expectBug = args.includes('--expect-bug');
const buildDir = path.resolve(args.find((a) => !a.startsWith('--')) || path.join(root, 'build'));
const PORT = 40123;  // 避开 Windows 保留端口段（netsh int ipv4 show excludedportrange protocol=tcp）
const SUMS_URL = 'https://github.com/Aevorine/Deixion/releases/latest/download/SHA256SUMS.txt';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const ok = (c, msg, extra = '') => { console.log(`${c ? 'PASS' : 'FAIL'}  ${msg}${extra ? '  ' + extra : ''}`); if (!c) failed++; };
// 对照模式下，这几条断言反过来：旧缺陷在，条件才不成立。
const okFix = (c, msg, extra = '') => (expectBug ? ok(!c, `（对照，期望旧缺陷）${msg}`, extra) : ok(c, msg, extra));

function stage() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'dx-upd-'));
  for (const f of ['Deixion.exe', 'WebView2Loader.dll']) fs.copyFileSync(path.join(buildDir, f), path.join(dir, f));
  fs.writeFileSync(path.join(dir, 'portable.flag'), '');
  fs.mkdirSync(path.join(dir, 'data'));
  // 关掉启动 20 秒后的自动检查，让每一次检查都由脚本发起；固定中文界面，按钮文字可查
  fs.writeFileSync(path.join(dir, 'data', 'settings.json'), JSON.stringify({ check_updates: false, language: 'zh' }));
  return dir;
}

async function launch(dir) {
  const child = spawn(path.join(dir, 'Deixion.exe'), [], { cwd: dir, stdio: 'ignore', env: { ...process.env, DEIXION_DEVTOOLS: '1', WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: `--remote-debugging-port=${PORT}` } });
  for (let i = 0; i < 100; i++) {
    await sleep(200);
    try {
      const list = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
      const page = list.find((t) => t.type === 'page' && t.webSocketDebuggerUrl);
      if (page) return { child, page };
    } catch { /* 还没起来 */ }
  }
  child.kill();
  throw new Error('WebView2 调试端口一直没出现（已有 Deixion 在运行？先 Deixion.exe --quit）');
}

async function connect(page) {
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej(new Error('CDP 连接失败')); });
  let id = 0;
  const waits = new Map();
  ws.onmessage = (e) => {
    const m = JSON.parse(e.data);
    if (m.id && waits.has(m.id)) { waits.get(m.id)(m); waits.delete(m.id); }
  };
  const send = (method, params = {}) => new Promise((res) => { const i = ++id; waits.set(i, res); ws.send(JSON.stringify({ id: i, method, params })); });
  const ev = async (expr) => {
    const r = await send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true });
    if (r.result?.exceptionDetails) throw new Error(r.result.exceptionDetails.text + ' ' + (r.result.exceptionDetails.exception?.description || ''));
    return r.result?.result?.value;
  };
  await send('Runtime.enable');
  return { ws, ev };
}

const stopAll = (child) => {
  try { child.kill(); } catch { /* 已退出 */ }
  spawnSync('taskkill', ['/F', '/IM', 'msedgewebview2.exe', '/FI', `COMMANDLINE eq *remote-debugging-port=${PORT}*`], { stdio: 'ignore' });
};

const dir = stage();
const { child, page } = await launch(dir);
const c = await connect(page);
const rpc = (m, p = {}) => c.ev(`import('./js/core/bridge.js').then((b) => b.call(${JSON.stringify(m)}, ${JSON.stringify(p)})).then((r) => ({ ok: true, r }), (e) => ({ ok: false, msg: String(e.message), code: e.code }))`);
const state = async () => (await rpc('update.state')).r;
async function until(pred, ms, step = 400) {
  const t0 = Date.now();
  for (;;) {
    const s = await state();
    if (pred(s)) return s;
    if (Date.now() - t0 > ms) return s;
    await sleep(step);
  }
}
const installer = path.join(dir, 'data', 'update', 'Deixion-Setup-x64.exe');

try {
  await sleep(1200);
  const s0 = await state();
  ok(s0.portable === true && s0.state === 'idle', `起点：便携模式、未检查，当前版本 ${s0.current}`, JSON.stringify({ state: s0.state, portable: s0.portable }));

  await rpc('update.check');
  const s1 = await until((s) => !['idle', 'checking'].includes(s.state), 30000);
  ok(s1.state === 'available', `检查：发现新版本 ${s1.latest}（当前 ${s1.current}）`, s1.state === 'available' ? '' : `state=${s1.state} error=${s1.error}（构建版本得低于最新 Release，且要能联网）`);
  if (s1.state !== 'available') throw new Error('没有可下载的新版本，后面的步骤无法进行');

  await rpc('update.download');
  const s2 = await until((s) => ['ready', 'error'].includes(s.state), 180000, 700);
  ok(s2.state === 'ready', '下载：状态进入 ready（原生代码已按发布页 SHA256SUMS 校验）', s2.state === 'ready' ? '' : `state=${s2.state} error=${s2.error}`);
  if (s2.state !== 'ready') throw new Error('下载没有完成');
  const got = crypto.createHash('sha256').update(fs.readFileSync(installer)).digest('hex');
  const sums = await (await fetch(SUMS_URL)).text();
  ok(sums.toLowerCase().includes(got), '下载下来的安装包哈希 = 发布页 SHA256SUMS', got.slice(0, 16) + '…');

  // 便携版不原地安装：apply 在状态为 ready 时撞上的是便携守卫，不是「没有已校验的更新」
  const a0 = await rpc('update.apply');
  ok(!a0.ok && /portable/.test(a0.msg), '再检查之前：apply 被便携守卫拒绝（说明状态是 ready）', a0.msg);

  await rpc('update.check');
  const s3 = await until((s) => s.state !== 'checking', 30000);
  okFix(s3.state === 'ready', '已校验之后再检查一次：状态仍是 ready', `state=${s3.state}`);
  okFix(fs.existsSync(installer), '再检查之后：已校验的安装包仍在磁盘上');
  const a1 = await rpc('update.apply');
  okFix(!a1.ok && /portable/.test(a1.msg), '再检查之后：apply 仍撞上便携守卫，而不是「没有已校验的更新」', a1.msg);

  // 界面：设置页的更新区域是否给出「安装并重启」
  await c.ev(`[...document.querySelectorAll('.rail .nav')].find((b) => /设置/.test(b.getAttribute('aria-label'))).click()`);
  await sleep(700);
  const hasInstall = await c.ev(`[...document.querySelectorAll('.page.on button')].some((b) => /安装并重启/.test(b.textContent))`);
  okFix(hasInstall, '设置页更新区域有「安装并重启」按钮', hasInstall ? '' : '按钮不在');
} catch (e) {
  ok(false, '运行异常', e.message);
}
stopAll(child);
await sleep(1000);
try { fs.rmSync(dir, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 }); } catch { /* 句柄还没释放 */ }
console.log(failed ? `\n${failed} 项失败` : (expectBug ? '\n对照实验：旧缺陷被测试抓到' : '\n全部通过'));
process.exit(failed ? 1 : 0);
