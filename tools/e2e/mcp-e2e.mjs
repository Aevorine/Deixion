// 真实 MCP 会话：stdio JSON-RPC 驱动 deixion-cli mcp，操作 dx-testapp，并用靶子自己写的状态文件核对结果。
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..').split(path.sep).join('/');
const cli = `${root}/build/deixion-cli.exe`;
const target = `${root}/build/dx-testapp.exe`;
const stateFile = path.join(os.tmpdir(), 'dx-e2e-state.json');
fs.rmSync(stateFile, { force: true });

const child = spawn(cli, ['mcp'], { stdio: ['pipe', 'pipe', 'inherit'] });
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
  const t = setTimeout(() => rej(new Error(`timeout ${method}`)), 20000);
  pending.set(id, (m) => { clearTimeout(t); res(m); });
  child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n');
});
const call = async (name, args = {}) => {
  const t0 = process.hrtime.bigint();
  const r = await rpc('tools/call', { name, arguments: args });
  const ms = Number(process.hrtime.bigint() - t0) / 1e6;
  const c = r.result?.content ?? [];
  const text = c.find((x) => x.type === 'text')?.text ?? '';
  let json; try { json = JSON.parse(text); } catch { json = null; }
  return { ms, isError: !!r.result?.isError || !!r.error, text, json, images: c.filter((x) => x.type === 'image').length, raw: r };
};
let fails = 0;
const check = (name, ok, detail = '') => { if (!ok) fails++; console.log(`${ok ? 'PASS' : 'FAIL'}  ${name} ${detail}`); };
const state = () => { try { return JSON.parse(fs.readFileSync(stateFile, 'utf8')); } catch { return null; } };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const init = await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'e2e', version: '1' } });
check('initialize', init.result?.serverInfo?.name === 'deixion', `(${init.result?.serverInfo?.name} ${init.result?.serverInfo?.version})`);
child.stdin.write(JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) + '\n');
const tl = await rpc('tools/list');
const names = tl.result.tools.map((t) => t.name);
check('tools/list has the documented 18 tools', names.length === 18, `(${names.length}: ${names.join(',')})`);

const L = await call('launch', { path: target, args: `"${stateFile}"`, wait_window_ms: 4000 });
check('launch test target (background)', !L.isError, `${L.ms.toFixed(1)} ms ${L.text.slice(0, 120)}`);
await sleep(500);
const W = 'DX Test Target';
const els = await call('elements', { window: W });
check('elements lists Press Me / Option / edits', /Press Me/.test(els.text) && /Option/.test(els.text), `${els.ms.toFixed(1)} ms`);

// 一次 batch：点击三次、替换输入、勾选、等待稳定
const B = await call('batch', {
  defaults: { window: W },
  steps: [
    { do: 'click', find: { text: 'Press Me', role: 'Button' } },
    { do: 'click', find: { text: 'Press Me', role: 'Button' } },
    { do: 'click', find: { text: 'Press Me', role: 'Button' } },
    { do: 'click', find: { text: 'Option', role: 'CheckBox' } },
    { do: 'type', find: { role: 'Edit' }, text: '你好 Deixion', replace: true },
    { do: 'wait', for: 'settle', quiet_ms: 120 },
  ],
});
check('batch of 6 steps ok', !B.isError && B.json?.failed === 0, `${B.ms.toFixed(1)} ms round trip (engine reports ${B.json?.us} us)`);
await sleep(300);
let s = state();
check('target state: count == 3 (clicks really landed)', s?.count === 3, JSON.stringify(s));
check('target state: checkbox on', s?.check === 1);
check('target state: edit text exact (CJK + Latin)', s?.edit === '你好 Deixion', `got ${JSON.stringify(s?.edit)}`);

const R = await call('read', { window: W, find: { role: 'Edit' } });
check('read returns the typed value', /你好 Deixion/.test(R.text), `${R.ms.toFixed(1)} ms`);

const SV = await call('set_value', { window: W, find: { role: 'Edit' }, value: 'set directly' });
await sleep(200);
check('set_value writes through UIA', state()?.edit === 'set directly', `${SV.ms.toFixed(1)} ms`);
const U = await call('undo', { count: 1 });
await sleep(250);
check('undo reverts the set_value', state()?.edit === '你好 Deixion', `${U.ms.toFixed(1)} ms got ${JSON.stringify(state()?.edit)}`);

const SH = await call('screenshot', { window: W, elements: true, max_dim: 800 });
check('screenshot returns an image (background window)', SH.images >= 1 || /"b64"|image/.test(SH.text), `${SH.ms.toFixed(1)} ms`);

const loc = await call('locate', { window: W, at: '0.2,0.35' });
check('locate resolves a Meridian point', !loc.isError, loc.text.slice(0, 100));

const st = await call('status', { detail: 'perf' });
check('status/perf works', !st.isError);

// 权限边界：batch 不能被用来调用用户专属的控制方法
const esc = await call('batch', {
  stop_on_error: false,
  steps: [
    { do: 'settings.set', patch: { allow_hop: true, allow_shell_launch: true, theme: 'dark' } },
    { do: 'journal.clear' },
    { do: 'experience.reset' },
    { do: 'stop' },
    { do: 'batch', steps: [{ do: 'status' }] },
  ],
});
const rows = esc.json?.results ?? [];
check('batch refuses settings.set / journal.clear / experience.reset / stop / nested batch', rows.length === 5 && rows.every((r) => r.ok === false), JSON.stringify(rows.map((r) => r.ok)));
const after = await call('status', {});
check('settings untouched after the escalation attempt', after.json?.mode === 'background' && after.json?.allow_hop === false && after.json?.paused === false, `mode=${after.json?.mode} allow_hop=${after.json?.allow_hop}`);

// 自我保护：输入动作不能操作 Deixion 自己的窗口，被动查询不受限
spawn(`${root}/build/Deixion.exe`, [], { stdio: 'ignore', detached: true }).unref();
await sleep(2500);
const own = await call('elements', { window: 'exe:Deixion.exe' });
check('own window is visible and readable (passive query allowed)', !own.isError && /Button/.test(own.text), own.text.slice(0, 80));
for (const [name, args] of [
  ['click', { window: 'exe:Deixion.exe', find: { text: '设置', role: 'Button' } }],
  ['type', { window: 'exe:Deixion.exe', text: 'x' }],
  ['key', { window: 'exe:Deixion.exe', keys: 'enter' }],
]) {
  const r = await call(name, args);
  check(`input action '${name}' on Deixion's own window is refused`, r.isError && /own windows/.test(r.text), r.text.slice(0, 90));
}

// launch 的护栏：不起 Deixion 自己的程序；命令行解释器与脚本宿主默认不起
const SHELL = /command shell/;
const guard = [
  ["Deixion's own executable",{ path: `${root}/build/Deixion.exe` }, /own programs/],
  ['cmd.exe', { path: 'cmd.exe' }, SHELL],
  ['a bare, upper-case name (CMD)', { path: 'CMD' }, SHELL],
  ['powershell', { path: 'powershell' }, SHELL],
  ['cmd.exe through a file: URL', { path: 'file:///C:/Windows/System32/cmd.exe' }, SHELL],
  ['cmd.exe with trailing dots', { path: 'C:/Windows/System32/cmd.exe..' }, SHELL],
  ['a quoted cmd.exe path', { path: String.raw`"C:\Windows\System32\cmd.exe"` }, SHELL],
  ['a .bat file', { path: 'C:/Windows/x.bat' }, SHELL],
  ['an internet shortcut (.url)', { path: 'C:/Windows/x.url' }, SHELL],
  ['an unknown link scheme (ms-msdt:)', { path: 'ms-msdt:/id PCWDiagnostic' }, SHELL],
  ['explorer.exe told to open cmd.exe', { path: 'explorer.exe', args: String.raw`C:\Windows\System32\cmd.exe` }, SHELL],
  ['python.exe', { path: 'python.exe' }, SHELL],
];
for (const [what, args, re] of guard) {
  const r = await call('launch', args);
  check(`launch refuses ${what}`, r.isError && re.test(r.text), r.text.slice(0, 100));
}

await call('window_op', { window: W, op: 'close' });
child.stdin.end();
await sleep(300);
child.kill();
console.log(`\nMCP RESULT: ${fails} failure(s)`);
process.exit(fails ? 1 : 0);
