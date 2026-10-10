// 1.0.7 加固与增强的端到端回归：真实 deixion-cli 走真实入口，不碰单元测试。
//   DX_E2E_ISOLATED=1  用 --inproc 在自己的进程里跑引擎，不碰正在运行的 Deixion 与真实设置（推荐）
// 覆盖：launch 对不存在文件快速失败、.lnk / %VAR% / 命令行开关绕过护栏、NaN 坐标、孤立代理项 JSON、
//       后台往 Chromium（Edge）网页编辑器输入（contenteditable / textarea / input）、滑块 set_value 与撤销。
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { cliPlan, isolated } from './isolated.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..').split(path.sep).join('/');
const plan = cliPlan(root);
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'dx-hard-'));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let fails = 0, seq = 0;
const check = (name, ok, detail = '') => { if (!ok) fails++; console.log(`${ok ? 'PASS' : 'FAIL'}  ${name} ${detail}`); };

function call(method, params = {}, timeout = 60000) {
  const f = path.join(work, `p${seq++}.json`);
  fs.writeFileSync(f, JSON.stringify(params));
  const t0 = Date.now();
  const r = spawnSync(plan.cmd, [...(isolated ? ['--inproc'] : []), 'call', method, `@${f}`], { encoding: 'utf8', timeout });
  let json = null;
  try { json = JSON.parse(r.stdout); } catch { /* 错误时 stdout 为空 */ }
  return { ms: Date.now() - t0, ok: r.status === 0, json, text: `${r.stdout || ''}${r.stderr || ''}`.trim() };
}
const titleOf = (hwnd) => {
  const r = call('windows', { filter: 'DXWEB' });
  const w = (r.json?.windows || []).find((x) => x.hwnd.toLowerCase() === String(hwnd).toLowerCase());
  return w ? w.title : '';
};

// —— launch 护栏与错误处理 ——
{
  const r = call('launch', { path: 'C:/dx_nonexistent_dir/nothing.exe', wait_window_ms: 0 });
  check('launch of a missing file fails fast with not_found (no modal error box)', !r.ok && /not found|not_found/i.test(r.text) && r.ms < 8000, `${r.ms} ms ${r.text.slice(0, 90)}`);
}
{
  const lnk = path.join(work, 'dx-ps.lnk');
  const ps = spawnSync('powershell', ['-NoProfile', '-Command',
    `$s=(New-Object -ComObject WScript.Shell).CreateShortcut('${lnk}'); $s.TargetPath='C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe'; $s.Save()`], { encoding: 'utf8' });
  check('test shortcut created', fs.existsSync(lnk), ps.stderr.slice(0, 80));
  const r = call('launch', { path: lnk.split(path.sep).join('/') });
  check('launch refuses a .lnk that points at PowerShell', !r.ok && /command shell/.test(r.text), r.text.slice(0, 100));
  const lnk2 = path.join(work, 'dx-cmdargs.lnk');
  spawnSync('powershell', ['-NoProfile', '-Command',
    `$s=(New-Object -ComObject WScript.Shell).CreateShortcut('${lnk2}'); $s.TargetPath='C:\\Windows\\explorer.exe'; $s.Arguments='C:\\Windows\\System32\\cmd.exe'; $s.Save()`], { encoding: 'utf8' });
  const r2 = call('launch', { path: lnk2.split(path.sep).join('/') });
  check('launch refuses a .lnk that makes explorer open cmd.exe', !r2.ok && /command shell/.test(r2.text), r2.text.slice(0, 100));
}
for (const [what, args] of [
  ['%COMSPEC%', { path: '%COMSPEC%' }],
  ['%SystemRoot%\\System32\\cmd.exe', { path: '%SystemRoot%\\System32\\cmd.exe' }],
  ['a .py script', { path: 'C:/Windows/x.py' }],
  ['WindowsTerminal.exe', { path: 'WindowsTerminal.exe' }],
  ['a Chromium --renderer-cmd-prefix flag', { path: 'msedge.exe', args: '--renderer-cmd-prefix="cmd /c calc"' }],
  ['an ubuntu.exe WSL launcher', { path: 'ubuntu.exe' }],
]) {
  const r = call('launch', args);
  check(`launch refuses ${what}`, !r.ok && /command shell/.test(r.text), r.text.slice(0, 100));
}

// —— 输入校验 ——
{
  const r = call('click', { window: 'screen', at: 'nan,nan' });
  check('non-finite coordinates are rejected', !r.ok && /at must be/.test(r.text), r.text.slice(0, 90));
  const s = call('windows', { filter: '\ud800\u0041\udc00' });
  check('lone surrogates in JSON do not break the parser', s.ok && s.json?.ok === true, s.text.slice(0, 60));
}

// —— 后台往 Chromium 网页编辑器输入 ——
const edge = ['C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', 'C:/Program Files/Microsoft/Edge/Application/msedge.exe'].find((p) => fs.existsSync(p));
if (!edge) console.log('SKIP  Chromium typing (msedge.exe not found)');
else {
  const page = path.join(work, 'edit-test.html').split(path.sep).join('/');
  fs.writeFileSync(page, `<!doctype html><html><head><meta charset="utf-8"><title>DXWEB:init</title></head><body>
<div id="ce" contenteditable="true" role="textbox" aria-label="CE editor" style="border:2px solid #36c;min-height:60px;padding:8px;margin:12px"></div>
<textarea id="ta" aria-label="TA area" style="width:90%;height:70px;margin:12px"></textarea><br>
<input id="in" aria-label="IN line" style="width:90%;margin:12px">
<script>
const f=()=>{document.title='DXWEB ce='+document.getElementById('ce').innerText.replace(/\\n/g,'|')+' ta='+document.getElementById('ta').value.replace(/\\n/g,'|')+' in='+document.getElementById('in').value+' f='+(document.activeElement&&document.activeElement.id||'-')};
for(const id of ['ce','ta','in']) document.getElementById(id).addEventListener('input',f);
document.addEventListener('focusin',f);
</script></body></html>`);
  // InPrivate + 关同步：全新配置文件里 Edge 会在几秒后弹出“同步你的浏览数据”模态框并抢走键盘焦点，网页收不到键盘消息，这是浏览器自己的行为，不是引擎丢字。
  const L = call('launch', { path: edge, args: `--user-data-dir="${work.split(path.sep).join('/')}/edgeprof" --inprivate --disable-sync --disable-extensions --no-first-run --no-default-browser-check --app=file:///${page} --window-size=900,600`, wait_window_ms: 8000 });
  const hwnd = L.json?.window?.hwnd;
  check('launch the Chromium test page in the background', L.ok && !!hwnd, L.text.slice(0, 100));
  if (hwnd) {
    await sleep(800);
    const W = `hwnd:${hwnd}`;
    const t = (text, find, extra = {}) => call('type', { window: W, find, text, ...extra });
    // 网页把编辑结果异步写进标题：轮询到出现为止（最多 4 秒），不用固定等待。
    const until = async (re) => {
      const t0 = Date.now();
      let s = '';
      while (Date.now() - t0 < 4000) { s = titleOf(hwnd); if (re.test(s)) break; await sleep(80); }
      return s;
    };
    let r = t('abc', { aid: 'ce' });
    let s = await until(/ce=abc /);
    check('contenteditable: typed text reaches the page', /ce=abc /.test(s), `${r.json?.strategy ?? r.text.slice(0, 110)} -> ${s}`);
    r = t('def', { aid: 'ce' });
    s = await until(/ce=abcdef /);
    check('contenteditable: a second type appends', /ce=abcdef /.test(s), `${r.json?.strategy ?? r.text.slice(0, 110)} -> ${s}`);
    r = t('XY', { aid: 'ce' }, { replace: true });
    s = await until(/ce=XY /);
    check('contenteditable: replace overwrites the content', /ce=XY /.test(s), `${r.json?.strategy ?? r.text.slice(0, 110)} -> ${s}`);
    r = t('hi\nthere', { aid: 'ta' });
    s = await until(/ta=hi\|there /);
    check('textarea: multi-line text with a newline', /ta=hi\|there /.test(s), `${r.json?.strategy ?? r.text.slice(0, 110)} -> ${s}`);
    r = t('你好🙂', { aid: 'in' });
    s = await until(/in=你好🙂 /);
    check('input: CJK and an astral-plane emoji arrive intact', /in=你好🙂 /.test(s), `${r.json?.strategy ?? r.text.slice(0, 110)} -> ${s}`);
    r = call('key', { window: W, keys: 'ctrl+a' });
    await sleep(100);
    r = call('key', { window: W, keys: 'backspace' });
    s = await until(/in= /);
    check('key: ctrl+a then backspace clears the focused input', /in= /.test(s), `${r.ok} ${s}`);
    call('window', { window: W, op: 'close' });
  }
}

// —— 滑块：set_value 与撤销（靶子进程自己写状态文件） ——
{
  const state = path.join(work, 'state.json');
  const target = `${root}/build/dx-testapp.exe`;
  const L = call('launch', { path: target, args: `"${state}"`, wait_window_ms: 4000 });
  check('launch the test target', L.ok, L.text.slice(0, 80));
  await sleep(600);
  const rd = () => { try { return JSON.parse(fs.readFileSync(state, 'utf8')); } catch { return null; } };
  const W = 'DX Test Target';
  check('slider starts at 25', rd()?.slider === 25, JSON.stringify(rd()));
  const sv = call('set_value', { window: W, find: { role: 'Slider' }, value: 80 });
  await sleep(300);
  check('set_value moves the slider through UIA RangeValue', rd()?.slider === 80, `${sv.text.slice(0, 80)} state=${rd()?.slider}`);
  const un = call('rollback', { count: 1 });
  await sleep(300);
  check('undo restores the slider (it used to be irreversible)', rd()?.slider === 25, `${un.text.slice(0, 80)} state=${rd()?.slider}`);
  call('window', { window: W, op: 'close' });
}

fs.rmSync(work, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 });
plan.cleanup();
console.log(`\nHARDENING RESULT: ${fails} failure(s)`);
process.exit(fails ? 1 : 0);
