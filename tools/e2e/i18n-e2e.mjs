// 语言端到端：真实 Deixion.exe（便携模式，放在临时目录，不碰你的设置）+ 真实打包进去的界面，
// 经 WebView2 调试端口逐页读 DOM。英文界面不许出现汉字；中文界面保持原样；设置页点语言能切换并落盘。
// 调试端口需要程序里的 DevTools 开关（DEIXION_DEVTOOLS），脚本自己设。
// 用法：node tools/e2e/i18n-e2e.mjs [build 目录]    先结束正在运行的 Deixion（管道与单实例互斥量是全局的）。
import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const buildDir = path.resolve(process.argv[2] || path.join(root, 'build'));
const PORT = 40123;  // 避开 Windows 保留端口段（netsh int ipv4 show excludedportrange protocol=tcp）
const CJK_RE = /[㐀-鿿　-〿＀-￯]/;
const CJK = { test: (s) => CJK_RE.test(String(s).replaceAll('中文', '')) };  // 语言选项永远用各自的文字写，「中文」是唯一允许的汉字
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const ok = (c, msg, extra = '') => { console.log(`${c ? 'PASS' : 'FAIL'}  ${msg}${extra ? '  ' + extra : ''}`); if (!c) failed++; };

function stage(lang) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'dx-i18n-'));
  for (const f of ['Deixion.exe', 'WebView2Loader.dll']) fs.copyFileSync(path.join(buildDir, f), path.join(dir, f));
  fs.writeFileSync(path.join(dir, 'portable.flag'), '');
  fs.mkdirSync(path.join(dir, 'data'));
  if (lang) fs.writeFileSync(path.join(dir, 'data', 'settings.json'), JSON.stringify({ language: lang }));
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
  throw new Error('WebView2 调试端口一直没出现');
}

async function connect(page) {
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej(new Error('CDP 连接失败')); });
  let id = 0;
  const waits = new Map();
  const logs = [];
  ws.onmessage = (e) => {
    const m = JSON.parse(e.data);
    if (m.id && waits.has(m.id)) { waits.get(m.id)(m); waits.delete(m.id); return; }
    if (m.method === 'Runtime.exceptionThrown') logs.push('异常 ' + (m.params.exceptionDetails.exception?.description || m.params.exceptionDetails.text));
    else if (m.method === 'Runtime.consoleAPICalled' && (m.params.type === 'error' || (m.params.type === 'warning' && /i18n/.test(JSON.stringify(m.params.args))))) logs.push(m.params.type + ' ' + m.params.args.map((x) => x.value ?? x.description).join(' '));
  };
  const send = (method, params = {}) => new Promise((res) => { const i = ++id; waits.set(i, res); ws.send(JSON.stringify({ id: i, method, params })); });
  const ev = async (expr) => {
    const r = await send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true });
    if (r.result?.exceptionDetails) throw new Error(r.result.exceptionDetails.text + ' ' + (r.result.exceptionDetails.exception?.description || ''));
    return r.result?.result?.value;
  };
  await send('Runtime.enable');
  return { ws, send, ev, logs };
}

/** 当前页面里所有"人看得见"的文字：正文、悬停提示、aria-label、占位符、title。窗口选择器里的真实窗口标题是用户数据，不算。 */
const COLLECT = `(() => {
  const out = new Set();
  const add = (s) => { s = String(s || '').trim(); if (s) out.add(s); };
  const scope = document.querySelector('.page.on');
  for (const root of [scope, document.querySelector('.rail'), document.querySelector('.statusbar'), document.querySelector('.toasts'), document.querySelector('.scrim')]) {
    if (!root) continue;
    const c = root.cloneNode(true);
    c.querySelectorAll('select, option, [data-user], canvas, .kx, .katex, code, pre').forEach((n) => n.remove());
    c.querySelectorAll('*').forEach((n) => { for (const a of ['data-tip', 'aria-label', 'placeholder', 'title', 'alt']) add(n.getAttribute(a)); });
    add(c.getAttribute?.('aria-label'));
    for (const line of c.innerText.split(/\\n+/)) add(line);
  }
  add(document.title);
  return [...out];
})()`;

async function walk(c, lang, label) {
  const names = await c.ev(`[...document.querySelectorAll('.rail .nav')].map((b) => b.getAttribute('aria-label'))`);
  ok(names.length === 12, `${label}：外壳导航按钮 ${names.length} 个`, names.join(' | '));
  const seenPages = new Set();
  // 按钮顺序：各页面…、模式、暂停、操作指南与设置（页脚两个页面）。模式与暂停不是页面，不点。
  const pageIdx = names.map((_, i) => i).filter((i) => i !== names.length - 4 && i !== names.length - 3);
  const bad = [];
  const scan = (where, texts) => { if (lang === 'en') for (const s of texts) if (CJK.test(s)) bad.push(`[${where}] ${s.slice(0, 90)}`); };
  let clicked = 0;
  for (const i of pageIdx) {
    await c.ev(`document.querySelectorAll('.rail .nav')[${i}].click()`);
    await sleep(600);
    const pageId = await c.ev(`document.querySelector('.page.on')?.dataset.page`);
    seenPages.add(pageId);
    scan(names[i], await c.ev(COLLECT));
    if (pageId === 'settings') continue;
    // 每个分段开关（标签页、筛选）都点一遍，动态生成的文字也要翻译
    const n = await c.ev(`document.querySelectorAll('.page.on .seg button').length`);
    for (let k = 0; k < n; k++) {
      await c.ev(`document.querySelectorAll('.page.on .seg button')[${k}]?.click()`);
      await sleep(250);
      scan(`${names[i]} 分段${k}`, await c.ev(COLLECT));
      clicked++;
    }
    if (pageId === 'actions') {
      // 方法下拉里的每一项都选一遍：表单标签、占位符各不相同
      const opts = await c.ev(`(() => { const s = [...document.querySelectorAll('.page.on select')].find((x) => [...x.options].some((o) => o.value === 'launch')); return s ? [...s.options].map((o) => o.textContent) : []; })()`);
      scan('操作页方法下拉', opts);
      for (let k = 0; k < opts.length; k++) {
        await c.ev(`(() => { const s = [...document.querySelectorAll('.page.on select')].find((x) => [...x.options].some((o) => o.value === 'launch')); s.selectedIndex = ${k}; s.dispatchEvent(new Event('change', { bubbles: true })); })()`);
        await sleep(200);
        scan(`操作页方法 ${opts[k]}`, await c.ev(COLLECT));
        clicked++;
      }
    }
  }
  ok(seenPages.size === 10, `${label}：10 个页面都走到了`, [...seenPages].join(' '));
  console.log(`      （${label}：交互 ${clicked} 次）`);
  if (lang === 'en') ok(bad.length === 0, `${label}：英文界面 ${pageIdx.length} 个页面没有汉字`, '\n      ' + bad.slice(0, 15).join('\n      '));
  return pageIdx;
}

/** 点设置页里某一行右边的按钮，收集弹窗文字，再 Esc 关掉。 */
async function dialogText(c, rowKeyRe) {
  await c.ev(`(() => { const r = [...document.querySelectorAll('.page.on .row')].find((x) => ${rowKeyRe}.test(x.querySelector('.k')?.textContent || '')); r?.querySelector('button')?.click(); })()`);
  await sleep(400);
  const texts = await c.ev(COLLECT);
  await c.ev(`document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }))`);
  await sleep(300);
  return texts;
}

async function run(lang) {
  const dir = stage(lang);
  const { child, page } = await launch(dir);
  const c = await connect(page);
  try {
    await sleep(1200);
    const htmlLang = await c.ev('document.documentElement.lang');
    ok(htmlLang === (lang === 'en' ? 'en' : 'zh-CN'), `${lang}：<html lang> = ${htmlLang}`);
    await walk(c, lang, lang);
    const names = await c.ev(`[...document.querySelectorAll('.rail .nav')].map((b) => b.getAttribute('aria-label')).join('|')`);
    if (lang === 'zh') ok(/总览/.test(names) && /设置/.test(names), '中文界面导航仍是中文', names);
    else ok(/Overview/.test(names) && /Settings/.test(names), '英文界面导航是英文', names);
    // 设置页（上面 walk 已停在最后一个页面 = 设置）：弹窗里的文字也要翻译
    ok(await c.ev(`!!document.querySelector('.seg [data-v="en"]')`), `${lang}：设置页有语言选项`);
    const dlg1 = await dialogText(c, /允许启动的程序|Allowed programs/);
    const dlg2 = await dialogText(c, /^(配置|Configuration)$/);
    const all = [...dlg1, ...dlg2];
    ok(all.length > 4, `${lang}：设置页两个弹窗都能打开`, `${all.length} 条文字`);
    if (lang === 'en') ok(!all.some((s) => CJK.test(s)), '英文界面设置弹窗没有汉字', all.filter((s) => CJK.test(s)).join(' | '));
    ok(c.logs.length === 0, `${lang}：控制台没有异常、没有缺词条警告`, c.logs.slice(0, 8).join(' | '));
  } catch (e) {
    ok(false, `${lang}：运行异常`, e.message);
  }
  return { dir, child, c };
}

function stopAll(child) {
  try { child.kill(); } catch { /* 已退出 */ }
  spawnSync('taskkill', ['/F', '/IM', 'msedgewebview2.exe', '/FI', `COMMANDLINE eq *remote-debugging-port=${PORT}*`], { stdio: 'ignore' });
}

const results = [];
for (const lang of ['en', 'zh']) {
  const r = await run(lang);
  results.push(r);
  stopAll(r.child);
  await sleep(1500);
}

// 在界面里切换语言：中文 → English → 中文，检查重载与落盘
{
  const dir = stage('zh');
  const { child, page } = await launch(dir);
  let c = await connect(page);
  try {
    await sleep(1200);
    await c.ev(`[...document.querySelectorAll('.rail .nav')].find((b) => /设置/.test(b.getAttribute('aria-label'))).click()`);
    await sleep(400);
    await c.ev(`document.querySelector('.seg [data-v="en"]').click()`);
    await sleep(2500);
    const list = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
    c = await connect(list.find((t) => t.type === 'page'));
    ok((await c.ev('document.documentElement.lang')) === 'en', '点 English 后整页重载成英文');
    const saved = JSON.parse(fs.readFileSync(path.join(dir, 'data', 'settings.json'), 'utf8'));
    ok(saved.language === 'en', 'settings.json 里 language = en', JSON.stringify(saved.language));
    await sleep(300);
    await c.ev(`document.querySelector('.seg [data-v="zh"]').click()`);
    await sleep(2500);
    const list2 = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
    c = await connect(list2.find((t) => t.type === 'page'));
    ok((await c.ev('document.documentElement.lang')) === 'zh-CN', '点 中文 后整页重载成中文');
    const saved2 = JSON.parse(fs.readFileSync(path.join(dir, 'data', 'settings.json'), 'utf8'));
    ok(saved2.language === 'zh', 'settings.json 里 language = zh', JSON.stringify(saved2.language));
  } catch (e) {
    ok(false, '切换语言流程异常', e.message);
  }
  stopAll(child);
  await sleep(800);
  try { fs.rmSync(dir, { recursive: true, force: true }); } catch { /* 句柄还没释放 */ }
}

for (const r of results) { try { fs.rmSync(r.dir, { recursive: true, force: true }); } catch { /* 句柄还没释放 */ } }
console.log(failed ? `\n${failed} 项失败` : '\n全部通过');
process.exit(failed ? 1 : 0);
