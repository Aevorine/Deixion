// 后台模式在经典 Win32 之外的界面框架上是否抢前台、动作是否真的生效：WPF、UWP、Electron 各一个真实目标。
//   WPF      tools/e2e/wpf-target.ps1（PowerShell 托管的 WPF 窗口，自己把状态写进文件）
//   UWP      Windows 计算器（7 + 8 =，读结果显示）
//   Electron Visual Studio Code，用临时的 --user-data-dir / --extensions-dir 起一个独立实例，不碰你的 VS Code 配置
// 做法：记下你当前所在的前台窗口，拉起目标后把前台还给它，再用隔离的 MCP 会话（--inproc、便携数据）在后台操作目标；
// 同时 fg-watch.ps1 的高优先级线程不停读真实前台窗口。报告每个框架上：动作是否生效（看目标自己的状态）、目标成为前台的次数与时长。
// 用法：node tools/e2e/frameworks-e2e.mjs [wpf] [uwp] [electron] [--shots=目录]      不带参数 = 全部；--shots 把每个目标操作后的截图存下来。先完整构建。
// 会在桌面上打开并关闭这几个窗口；只结束脚本自己拉起的进程。跑的时候别去点目标窗口，否则那次前台会算在目标头上。
import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

process.env.DX_E2E_ISOLATED = '1';
const { cliPlan } = await import('./isolated.mjs');
const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../..').split(path.sep).join('/');
const PS = path.join(process.env.SystemRoot || 'C:/Windows', 'System32/WindowsPowerShell/v1.0/powershell.exe');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'dx-fw-'));
const want = process.argv.slice(2).filter((a) => !a.startsWith('-'));
const shots = process.argv.slice(2).find((a) => a.startsWith('--shots='))?.slice(8) || '';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const readJson = (f) => { try { return JSON.parse(fs.readFileSync(f, 'utf8').replace(/^\uFEFF/, '')); } catch { return null; } };
async function waitFile(f, ms) { const end = Date.now() + ms; while (Date.now() < end) { if (fs.existsSync(f)) return true; await sleep(80); } return false; }

// —— 隔离的 MCP 会话 ——
const plan = cliPlan(root);
const child = spawn(plan.cmd, plan.mcpArgs, { stdio: ['pipe', 'pipe', 'inherit'] });
let buf = '', nextId = 1;
const pending = new Map();
child.stdout.on('data', (d) => {
  buf += d;
  let i;
  while ((i = buf.indexOf('\n')) >= 0) {
    const line = buf.slice(0, i).trim(); buf = buf.slice(i + 1);
    if (!line) continue;
    const m = JSON.parse(line);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  }
});
const rpc = (method, params = {}) => new Promise((res, rej) => {
  const id = nextId++;
  const t = setTimeout(() => rej(new Error(`timeout ${method}`)), 30000);
  pending.set(id, (m) => { clearTimeout(t); res(m); });
  child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n');
});
const call = async (name, args = {}) => {
  const r = await rpc('tools/call', { name, arguments: args });
  const text = (r.result?.content ?? []).find((x) => x.type === 'text')?.text ?? '';
  let json; try { json = JSON.parse(text); } catch { json = null; }
  return { isError: !!r.result?.isError || !!r.error, text, json, image: (r.result?.content ?? []).find((x) => x.type === 'image')?.data };
};
const exists = async (W, q) => ((await call('find', { window: W, limit: 1, ...q })).json?.matches ?? []).length > 0;
await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'frameworks-e2e', version: '1' } });
child.stdin.write(JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) + '\n');

const windows = async () => (await call('windows')).text.split('\n').map((l) => /^(0x[0-9A-Fa-f]+)\s+(\S+)\s+"(.*)"\s+\d+x\d+/.exec(l.trim())).filter(Boolean).map((m) => ({ hwnd: m[1], exe: m[2], title: m[3] }));
async function newWindow(before, pred, ms) {
  const known = new Set(before.map((w) => w.hwnd));
  const end = Date.now() + ms;
  while (Date.now() < end) {
    const hit = (await windows()).find((w) => !known.has(w.hwnd) && pred(w));
    if (hit) return hit;
    await sleep(300);
  }
  return null;
}

// —— 前台监视 ——
const foreground = () => Number(spawnSync(PS, ['-NoProfile', '-Command', "(Add-Type -PassThru -Namespace Dx -Name Fg -MemberDefinition '[DllImport(\"user32.dll\")] public static extern System.IntPtr GetForegroundWindow();')::GetForegroundWindow().ToInt64()"], { encoding: 'utf8' }).stdout.trim()) || 0;
let watchN = 0;
async function watch(raiseHwnd) {
  const out = path.join(tmp, `fg-${++watchN}.json`);
  spawn(PS, ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', path.join(here, 'fg-watch.ps1'), '-Raise', String(raiseHwnd), '-Out', out, '-Stop', `${out}.stop`], { stdio: 'ignore' });
  if (!(await waitFile(`${out}.ready`, 20000))) throw new Error('the foreground watcher did not start');
  return async () => { fs.writeFileSync(`${out}.stop`, ''); await waitFile(out, 10000); return readJson(out); };
}
function episodes(log, isTarget) {
  const ev = log?.events ?? [];
  let count = 0, total = 0, longest = 0;
  ev.forEach((e, i) => {
    if (!isTarget(e)) return;
    const d = (ev[i + 1]?.t ?? log.end_ms) - e.t;
    count++; total += d; longest = Math.max(longest, d);
  });
  return { count, total, longest, changes: ev.length };
}

// —— 目标 ——
const wpfState = path.join(tmp, 'wpf-state.json');
const codeExe = (() => {
  const w = spawnSync('where', ['code'], { encoding: 'utf8' }).stdout.split(/\r?\n/).find((l) => /code\.cmd$/i.test(l.trim()));
  const exe = w ? path.resolve(path.dirname(w.trim()), '..', 'Code.exe') : '';
  return exe && fs.existsSync(exe) ? exe : '';
})();
const TARGETS = {
  wpf: {
    label: 'WPF (PowerShell-hosted WPF window)',
    launch: () => spawn(PS, ['-NoProfile', '-STA', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden', '-File', path.join(here, 'wpf-target.ps1'), '-State', wpfState], { stdio: 'ignore' }),
    match: (w) => w.title === 'dx-wpf-target',
    steps: (W) => [
      ['click the button', 'click', { window: W, find: { aid: 'btnCount' } }],
      ['click the button again', 'click', { window: W, find: { aid: 'btnCount' } }],
      ['type into the text box', 'type', { window: W, find: { aid: 'edit' }, text: 'Deixion 123', replace: true }],
      ['tick the check box', 'click', { window: W, find: { aid: 'chk' } }],
      ['set the slider to 70', 'set_value', { window: W, find: { aid: 'slider' }, value: '70' }],
    ],
    verify: async () => {
      await sleep(400);
      const s = readJson(wpfState) || {};
      return [['button clicks counted by the app == 2', s.count === 2, `count ${s.count}`], ['text box holds the typed text', s.edit === 'Deixion 123', JSON.stringify(s.edit)], ['check box is on', s.chk === true, `chk ${s.chk}`], ['slider is at 70', s.slider === 70, `slider ${s.slider}`]];
    },
  },
  uwp: {
    label: 'UWP (Windows Calculator)',
    launch: () => spawn('calc.exe', [], { stdio: 'ignore', detached: true }),
    match: (w) => /计算器|Calculator/.test(w.title),
    steps: (W) => [
      ['press 7', 'click', { window: W, find: { aid: 'num7Button' } }],
      ['press +', 'click', { window: W, find: { aid: 'plusButton' } }],
      ['press 8', 'click', { window: W, find: { aid: 'num8Button' } }],
      ['press =', 'click', { window: W, find: { aid: 'equalButton' } }],
    ],
    verify: async (W) => {
      await sleep(400);
      const r = await call('find', { window: W, aid: 'CalculatorResults', limit: 1 });
      const name = r.json?.matches?.[0]?.name ?? '';
      return [['the display shows 15', /(^|\D)15(\D|$)/.test(name), JSON.stringify(name)]];
    },
  },
  electron: {
    label: 'Electron (Visual Studio Code, throwaway profile)',
    skip: codeExe ? '' : 'Visual Studio Code is not installed',
    launch: () => {
      const ud = path.join(tmp, 'vsc-data'), ext = path.join(tmp, 'vsc-ext');
      fs.mkdirSync(path.join(ud, 'User'), { recursive: true });
      // 全新配置的 VS Code 会在窗口第一次拿到焦点时弹出“Welcome to VS Code / Sign in”模态框，盖住整个窗口并吃掉随后的点击与输入
      // （看每步截图发现的：搜索框一直是空的，其实字符进了那个框）。它和被测的引擎无关，用启动参数加设置两处一起关掉。
      fs.writeFileSync(path.join(ud, 'User', 'settings.json'), JSON.stringify({ 'workbench.startupEditor': 'none', 'workbench.welcomePage.experimentalOnboarding': false, 'telemetry.telemetryLevel': 'off', 'update.mode': 'none', 'extensions.autoCheckUpdates': false, 'security.workspace.trust.enabled': false, 'workbench.enableExperiments': false }));
      return spawn(codeExe, ['--user-data-dir', ud, '--extensions-dir', ext, '--new-window', '--disable-updates', '--skip-welcome', '--skip-release-notes', '--force-renderer-accessibility'], { stdio: 'ignore', detached: true });
    },
    match: (w) => /^Code(\s-\s.*)?\.exe$/i.test(w.exe) || /Visual Studio Code/.test(w.title),
    settle: 6000,
    steps: (W) => [
      ['open the Search view from the activity bar', 'click', { window: W, find: { text: 'Search (Ctrl+Shift+F)' } },
        async () => { await sleep(700); return [await exists(W, { text: 'Search: Type Search Term', role: 'Edit' }), 'the search box is there']; }],
      ['type into the search box', 'type', { window: W, find: { text: 'Search: Type Search Term', role: 'Edit' }, text: 'deixion-e2e', replace: true },
        async () => { await sleep(500); const v = (await call('read', { window: W, find: { text: 'Search: Type Search Term', role: 'Edit' } })).json?.element?.value ?? ''; return [v === 'deixion-e2e', `the search box holds ${JSON.stringify(v)}`]; }],
      // 实测：后台输入之后约 1 秒内发出的消息点击会被 VS Code 吞掉，而引擎仍报成功；等界面安静下来再点就正常（技能文档要求的用法）。
      ['wait for the UI to settle', 'wait', { window: W, for: 'settle', quiet_ms: 600, timeout_ms: 6000 }],
      ['open the Explorer view from the activity bar', 'click', { window: W, find: { text: 'Explorer (Ctrl+Shift+E)' } },
        async () => { await sleep(700); return [await exists(W, { text: 'Open Folder', role: 'Button' }), 'the Explorer view shows its Open Folder button']; }],
    ],
    verify: async () => [],
  },
};

// —— 运行 ——
let fails = 0, steals = 0;
const line = (tag, msg, detail = '') => console.log(`${tag}  ${msg}${detail ? '  ' + detail : ''}`);

for (const key of Object.keys(TARGETS)) {
  if (want.length && !want.includes(key)) continue;
  const t = TARGETS[key];
  console.log(`\n== ${t.label}`);
  if (t.skip) { line('SKIP', t.skip); continue; }
  const before = await windows();
  const mine = foreground();  // 你此刻所在的窗口：目标拉起后把前台还给它
  const proc = t.launch();
  const win = await newWindow(before, t.match, 30000);
  if (!win) { line('FAIL', 'the target window did not appear'); fails++; try { proc.kill(); } catch { /* 已退出 */ } continue; }
  const W = `hwnd:${win.hwnd}`;
  await sleep(t.settle ?? 1500);
  await call('elements', { window: W, refresh: true, limit: 5 });  // 让目标先把无障碍树建出来
  await sleep(800);
  try {
    const stop = await watch(mine);
    const done = [];
    for (const [name, tool, args, check] of t.steps(W)) {
      const r = await call(tool, args);
      const eff = !r.isError && check ? await check() : null;  // 这一步自己的效果：引擎报成功不算数，看目标
      done.push({ name, ok: !r.isError, strategy: r.json?.strategy ?? '', confirmed: r.json?.confirmed, focus: [r.json?.focus, r.json?.web_focus && `web focus: ${r.json.web_focus}`].filter(Boolean).join('; '), err: r.isError ? r.text.replace(/\s+/g, ' ').slice(0, 140) : '', eff });
      // 每一步之后的画面：某一步“报成功却没生效”时，直接看图就知道输入去了哪儿，不用反复重跑。
      if (shots && tool !== 'wait') { fs.mkdirSync(shots, { recursive: true }); const sh = await call('screenshot', { window: W, grid: false, max_dim: 1400 }); if (sh.image) fs.writeFileSync(path.join(shots, `${key}-${done.length}-${tool}.jpg`), Buffer.from(sh.image, 'base64')); }
      await sleep(350);
    }
    const checks = await t.verify(W);
    const log = await stop();
    for (const d of done) {
      if (!d.ok) fails++;
      line(d.ok ? 'PASS' : 'FAIL', `action: ${d.name}`, d.ok ? `via ${d.strategy}${d.confirmed === false ? ' (engine saw no reaction)' : ''}${d.focus ? ` | engine note: ${d.focus}` : ''}` : d.err);
      if (d.eff) { if (!d.eff[0]) fails++; line(d.eff[0] ? 'PASS' : 'FAIL', `  effect: ${d.eff[1]}`, d.eff[0] ? '' : `the engine reported success via ${d.strategy}, the target did not change`); }
    }
    if (shots) { fs.mkdirSync(shots, { recursive: true }); const sh = await call('screenshot', { window: W, grid: false, max_dim: 1400 }); if (sh.image) fs.writeFileSync(path.join(shots, `${key}.jpg`), Buffer.from(sh.image, 'base64')); }
    for (const [name, ok, detail] of checks) { if (!ok) fails++; line(ok ? 'PASS' : 'FAIL', `effect: ${name}`, detail); }
    const pids = new Set(key === 'uwp' ? [] : [proc.pid]);
    const hw = parseInt(win.hwnd, 16);
    const fg = episodes(log, (e) => e.hwnd === hw || pids.has(e.pid) || (key === 'uwp' && e.proc === 'CalculatorApp'));
    if (!log?.anchor_ok) line('WARN', 'the window you were in could not be brought back to the foreground before the run; the foreground figures below are not meaningful');
    if (fg.count) steals++;
    line(fg.count ? 'NOTE' : 'PASS', fg.count ? `foreground: the target became the foreground window ${fg.count} time(s), ${fg.total.toFixed(0)} ms in total, longest ${fg.longest.toFixed(0)} ms` : `foreground: the target never became the foreground window`, `(${done.length} actions, ${fg.changes} foreground change(s) recorded)`);
    if (fg.count) for (const e of log.events) line('     ', `${e.t.toFixed(1)} ms  <${e.proc}> ${e.title}`);
  } catch (e) {
    line('FAIL', 'run', e.message); fails++;
  }
  await call('window_op', { window: W, op: 'close' });
  await sleep(600);
  if (key === 'wpf') try { proc.kill(); } catch { /* 已退出 */ }
}

child.stdin.end();
await sleep(500);
try { child.kill(); } catch { /* 已退出 */ }
plan.cleanup();
try { fs.rmSync(tmp, { recursive: true, force: true, maxRetries: 10, retryDelay: 300 }); } catch { /* VS Code 还没放开句柄 */ }
console.log(`\nFRAMEWORKS RESULT: ${fails} failed, ${steals} framework(s) where the target took the foreground`);
process.exit(fails ? 1 : 0);  // 前台次数是测量结果，不算失败；退出码只反映动作或效果有没有出错
