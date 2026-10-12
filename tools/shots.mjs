// README 里的界面截图：真实 Deixion.exe（便携副本，不碰你的设置与已装的那份）+ 测试靶子 dx-testapp，
// 经 WebView2 调试端口逐页截图，中文、英文各一套。
// 入镜的窗口与路径都来自这个副本和测试靶子：副本放在公用目录（路径里没有用户名）；定位页默认目标是整个屏幕、
// 元素页会自动挑最上面的窗口，所以这两页都先切到测试靶子、确认画面换过来了再截；「接入」「日志」两页会显示本机路径与管道名，不截。
// 脚本只管流程，每张图入库前仍要自己看一遍。
// 用法：node tools/shots.mjs [build 目录] [--out=docs/images] [--stage=目录]
//       先完整构建（含 dx-testapp），并退出正在运行的 Deixion（单实例互斥量与命名管道是全局的）。
import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..');
const argv = process.argv.slice(2);
const opt = (k, d) => { const a = argv.find((x) => x.startsWith(`--${k}=`)); return a ? a.slice(k.length + 3) : d; };
const buildDir = path.resolve(argv.find((a) => !a.startsWith('--')) || path.join(root, 'build'));
const outDir = path.resolve(opt('out', path.join(root, 'docs', 'images')));
const stageDir = path.resolve(opt('stage', path.join(process.env.PUBLIC || os.tmpdir(), 'Deixion-demo')));
const PORT = 40124;  // 避开 Windows 保留端口段（netsh int ipv4 show excludedportrange protocol=tcp）
const PS = 'powershell.exe';
const TARGET = 'exe:dx-testapp.exe';
const PAGES = ['overview', 'locate', 'elements', 'actions', 'evolve', 'perf', 'settings'];
const VIEW = { width: 1280, height: 780, deviceScaleFactor: 1.5, mobile: false };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const say = (msg) => console.log(msg);

if (/Deixion\.exe/i.test(spawnSync('tasklist', ['/FI', 'IMAGENAME eq Deixion.exe', '/FO', 'CSV', '/NH'], { encoding: 'utf8' }).stdout)) {
  console.error('Deixion 正在运行：先让它退出（托盘菜单，或 Deixion.exe --quit），截完再启动。');
  process.exit(2);
}
for (const f of ['Deixion.exe', 'deixion-cli.exe', 'WebView2Loader.dll', 'dx-testapp.exe']) {
  if (!fs.existsSync(path.join(buildDir, f))) { console.error(`缺少 ${f}：先完整构建（cmake --build build --target all dx-testapp）。`); process.exit(2); }
}

// 你此刻所在的窗口：副本和靶子拉起后把前台还给它，截图走调试端口，不需要它们在最前面。
const foreground = () => Number(spawnSync(PS, ['-NoProfile', '-Command', "(Add-Type -PassThru -Namespace Dx -Name Fg -MemberDefinition '[DllImport(\"user32.dll\")] public static extern System.IntPtr GetForegroundWindow();')::GetForegroundWindow().ToInt64()"], { encoding: 'utf8' }).stdout.trim()) || 0;
const giveBack = (hwnd) => { if (hwnd) spawnSync(PS, ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', path.join(here, 'e2e', 'fg-watch.ps1'), '-Raise', String(hwnd), '-Seconds', '1', '-Out', path.join(stageDir, 'fg.json'), '-Stop', path.join(stageDir, 'fg.stop')], { stdio: 'ignore' }); };

function stage() {
  fs.rmSync(stageDir, { recursive: true, force: true, maxRetries: 10, retryDelay: 300 });
  fs.mkdirSync(path.join(stageDir, 'data'), { recursive: true });
  for (const f of ['Deixion.exe', 'deixion-cli.exe', 'WebView2Loader.dll', 'dx-testapp.exe']) fs.copyFileSync(path.join(buildDir, f), path.join(stageDir, f));
  fs.writeFileSync(path.join(stageDir, 'portable.flag'), '');
}
function setLanguage(lang) {
  const f = path.join(stageDir, 'data', 'settings.json');
  let s = {};
  try { s = JSON.parse(fs.readFileSync(f, 'utf8')); } catch { /* 第一次还没有 */ }
  fs.writeFileSync(f, JSON.stringify({ ...s, language: lang, check_updates: false }));
}

async function launch() {
  // 窗口被别的窗口盖住时 Chromium 会停止出帧，截图会一直等：关掉遮挡检测与后台节流。
  const flags = `--remote-debugging-port=${PORT} --disable-features=CalculateNativeWinOcclusion --disable-backgrounding-occluded-windows --disable-renderer-backgrounding`;
  const child = spawn(path.join(stageDir, 'Deixion.exe'), [], { cwd: stageDir, stdio: 'ignore', env: { ...process.env, DEIXION_DEVTOOLS: '1', WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: flags } });
  for (let i = 0; i < 100; i++) {
    await sleep(200);
    try {
      const list = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
      const page = list.find((t) => t.type === 'page' && t.webSocketDebuggerUrl);
      if (page) return { child, page };
    } catch { /* 还没起来 */ }
  }
  child.kill();
  throw new Error('WebView2 调试端口一直没出现');
}

async function connect(page) {
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej(new Error('CDP 连接失败')); });
  let id = 0;
  const waits = new Map();
  ws.onmessage = (e) => { const m = JSON.parse(e.data); if (m.id && waits.has(m.id)) { waits.get(m.id)(m); waits.delete(m.id); } };
  const send = (method, params = {}) => new Promise((res) => { const i = ++id; waits.set(i, res); ws.send(JSON.stringify({ id: i, method, params })); });
  const ev = async (expr) => {
    const r = await send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true });
    if (r.result?.exceptionDetails) throw new Error(r.result.exceptionDetails.text + ' ' + (r.result.exceptionDetails.exception?.description || ''));
    return r.result?.result?.value;
  };
  return { ws, send, ev };
}

function stopApp(child) {
  spawnSync(path.join(stageDir, 'Deixion.exe'), ['--quit'], { cwd: stageDir, stdio: 'ignore', timeout: 8000 });
  try { child.kill(); } catch { /* 已退出 */ }
  spawnSync('taskkill', ['/F', '/IM', 'msedgewebview2.exe', '/FI', `COMMANDLINE eq *remote-debugging-port=${PORT}*`], { stdio: 'ignore' });
}

// —— 让各页面有内容：对测试靶子做几步真实操作 ——
let callN = 0;
function act(method, params = {}) {
  const f = path.join(stageDir, `call-${++callN}.json`);
  fs.writeFileSync(f, JSON.stringify({ window: TARGET, ...params }));
  const r = spawnSync(path.join(stageDir, 'deixion-cli.exe'), ['call', method, `@${f}`], { cwd: stageDir, encoding: 'utf8' });
  fs.rmSync(f, { force: true });
  if (r.status !== 0) say(`  ! ${method}: ${(r.stderr || r.stdout || '').replace(/\s+/g, ' ').slice(0, 160)}`);
  try { return JSON.parse(r.stdout); } catch { return null; }
}
function populate() {
  act('elements');
  for (let i = 0; i < 3; i++) act('click', { find: { text: 'Press Me', role: 'Button' } });
  act('click', { find: { text: 'Option', role: 'CheckBox' } });
  act('type', { find: { role: 'Edit' }, text: 'Hello, Deixion', replace: true });
  act('set_value', { find: { role: 'Edit' }, value: 'Meridian k3m' });
  act('read', { find: { role: 'Edit' } });
  act('click', { at: '0.50,0.50', hover: true });
  spawnSync(path.join(stageDir, 'deixion-cli.exe'), ['bench', TARGET, '60'], { cwd: stageDir, stdio: 'ignore' });
  const m = act('find', { text: 'Press Me', role: 'Button', limit: 1 })?.matches?.[0]?.at;
  const [lam, phi] = String(m ?? '0.5,0.5').split(',').map(Number);
  return Number.isFinite(lam) && Number.isFinite(phi) ? { lam, phi } : { lam: 0.5, phi: 0.5 };
}

// —— 页面准备 ——
const PICK = `(() => {
  const s = document.querySelector('.page.on select');
  const o = s && [...s.options].find((x) => /^dx-testapp/i.test(x.textContent));
  if (!o) return 'none';
  s.value = o.value;
  s.dispatchEvent(new Event('change', { bubbles: true }));
  return o.value;
})()`;
async function pickTarget(c) {
  for (let i = 0; i < 30; i++) {
    const v = await c.ev(PICK);
    if (v !== 'none') return v;
    await sleep(200);
  }
  return '';
}
async function prepare(c, id, pin) {
  if (id === 'locate') {
    if (!(await pickTarget(c))) return '窗口列表里没有测试靶子';
    // 页面一打开就对「整个屏幕」截了一次，那次结果可能晚到、盖住靶子的图：等它落定后再按一次「截图」。
    await sleep(1500);
    await c.ev(`document.querySelector('.page.on .btn.primary')?.click()`);
    await sleep(1200);
    const ok = await c.ev(`(() => { const el = document.querySelector('.page.on .shot'); const r = el?.getBoundingClientRect(); if (!r || !r.width) return false; el.dispatchEvent(new MouseEvent('click', { bubbles: true, clientX: r.left + r.width * ${pin.lam}, clientY: r.top + r.height * ${pin.phi} })); return true; })()`);
    if (!ok) return '定位页没有出图';
    await sleep(900);
    return '';
  }
  if (id === 'elements') {
    // 元素页会自己先挑最上面的窗口并开始读它；那次读取没结束时切换目标会被忽略，所以反复切，直到列表里出现靶子的控件。
    for (let i = 0; i < 20; i++) {
      if (!(await pickTarget(c))) return '窗口列表里没有测试靶子';
      await sleep(700);
      if (await c.ev(`[...document.querySelectorAll('.page.on [data-user]')].some((x) => x.textContent.trim() === 'Press Me')`)) break;
      if (i === 19) return '元素页一直没换到测试靶子';
    }
    await c.ev(`[...document.querySelectorAll('.page.on [data-user]')].find((x) => x.textContent.trim() === 'Press Me')?.click()`);
    await sleep(700);
    return '';
  }
  if (id === 'perf') {
    // 性能页会显示这台机器的 CPU 型号：不入镜。芯片每秒重画，所以用样式藏掉那一格，而不是删一次节点。
    await c.ev(`(() => { const b = [...document.querySelectorAll('.page.on .chip')].find((x) => x.textContent.trim() === 'BMI2'); if (!b) return; b.parentElement.classList.add('dx-shots-nocpu'); const st = document.createElement('style'); st.textContent = '.dx-shots-nocpu > :first-child { display: none !important; }'; document.head.append(st); })()`);
  }
  await sleep(900);
  return '';
}

async function shootAll(lang, mine) {
  const { child, page } = await launch();
  const c = await connect(page);
  const dir = path.join(outDir, lang);
  fs.mkdirSync(dir, { recursive: true });
  let made = 0;
  try {
    await sleep(1200);
    giveBack(mine);
    const pin = populate();  // 性能直方图只在这次运行的内存里，所以每次启动都重新攒一遍
    await c.send('Emulation.setDeviceMetricsOverride', VIEW);
    const n = await c.ev(`document.querySelectorAll('.rail .nav').length`);
    for (let i = 0; i < n; i++) {
      if (i === n - 4 || i === n - 3) continue;  // 模式、暂停不是页面
      await c.ev(`document.querySelectorAll('.rail .nav')[${i}].click()`);
      await sleep(500);
      const id = await c.ev(`document.querySelector('.page.on')?.dataset.page`);
      if (!PAGES.includes(id)) continue;
      const err = await prepare(c, id, pin);
      if (err) { say(`SKIP  ${lang}/${id}: ${err}`); continue; }
      await c.ev(`document.activeElement?.blur()`);
      const r = await c.send('Page.captureScreenshot', { format: 'png' });
      if (!r.result?.data) { say(`FAIL  ${lang}/${id}: 没有拿到图`); continue; }
      fs.writeFileSync(path.join(dir, `${id}.png`), Buffer.from(r.result.data, 'base64'));
      say(`OK    ${lang}/${id}.png`);
      made++;
    }
  } finally {
    c.ws.close();
    stopApp(child);
  }
  return made;
}

// —— 运行 ——
const mine = foreground();
stage();
const target = spawn(path.join(stageDir, 'dx-testapp.exe'), [], { cwd: stageDir, stdio: 'ignore' });
let total = 0;
try {
  for (const lang of ['zh', 'en']) {
    setLanguage(lang);
    total += await shootAll(lang, mine);
    await sleep(1500);
  }
} finally {
  try { target.kill(); } catch { /* 已退出 */ }
  await sleep(500);
  try { fs.rmSync(stageDir, { recursive: true, force: true, maxRetries: 10, retryDelay: 300 }); } catch { /* 句柄还没释放 */ }
}
say(`\n${total} 张图写入 ${path.relative(root, outDir)}`);
process.exit(total === PAGES.length * 2 ? 0 : 1);
