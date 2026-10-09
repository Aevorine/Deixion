// 外壳：左侧图标栏、页面容器、底部状态栏。页面都在 pages/ 里登记，这里不认识具体页面。
import { h, initTips } from './core/dom.js';
import { icon } from './core/icons.js';
import { bus } from './core/bus.js';
import { init, state, patchSettings, tipKey } from './core/store.js';
import { t, setLang, getLang, resolveLang } from './core/i18n.js';
import { localizeMeta } from './core/meta.js';
import { getPages } from './core/registry.js';
import * as fmt from './core/fmt.js';
import './pages/index.js';

setLang(resolveLang('auto'));  // 引擎连上之前先按浏览器语言，连上后按设置
initTips();
const app = document.getElementById('app');
const stage = h('div', { class: 'stage' });
const rail = h('nav', { class: 'rail' });
const status = h('div', { class: 'statusbar' });
app.append(rail, h('div', { class: 'main' }, stage, status));

const views = new Map();
const navBtns = new Map();
let current = null;

const remember = (id) => { try { localStorage.setItem('dx.page', id); } catch { /* 隐私窗口里可能不可用 */ } };
const recall = () => { try { return localStorage.getItem('dx.page'); } catch { return null; } };

function go(id) {
  const def = getPages().find((p) => p.id === id);
  if (!def || current === id) return;
  if (current) {
    const old = views.get(current);
    old.root.classList.remove('on');
    old.ctl?.hide?.();
    navBtns.get(current)?.classList.remove('on');
  }
  let v = views.get(id);
  if (!v) {
    const root = h('div', { class: 'page', 'data-page': id });
    stage.append(root);
    v = { root, ctl: null };
    views.set(id, v);
    try { v.ctl = def.render(root) || {}; } catch (e) { console.error(e); root.append(h('div', { class: 'empty' }, t('页面加载失败：{e}', { e: e.message }))); }
  }
  v.root.classList.add('on');
  v.ctl?.show?.();
  navBtns.get(id)?.classList.add('on');
  current = id;
  remember(id);
}
bus.on('navigate', go);
bus.on('ev:navigate', (d) => go(d.page));

function buildRail() {
  rail.setAttribute('aria-label', t('功能导航'));
  rail.append(h('div', { class: 'logo', tip: 'Deixion' }, h('img', { src: 'assets/appicon-64.png', alt: 'Deixion' })));
  const pages = getPages();
  const mk = (p, n) => {
    const b = h('button', { type: 'button', class: 'nav', tip: () => (n ? `${t(p.tip)}\nCtrl + ${n % 10}` : t(p.tip)), onClick: () => go(p.id) }, icon(p.icon));
    navBtns.set(p.id, b);
    return b;
  };
  pages.filter((p) => p.area === 'main').forEach((p, i) => rail.append(mk(p, i + 1)));
  rail.append(h('div', { class: 'grow' }));
  const modeBtn = h('button', { type: 'button', class: 'nav', tip: tipKey(t('切换前台 / 后台模式'), 'mode'), onClick: () => patchSettings({ mode: state.settings.mode === 'foreground' ? 'background' : 'foreground' }) });
  const pauseBtn = h('button', { type: 'button', class: 'nav', tip: tipKey(t('暂停 / 继续接收操作'), 'pause'), onClick: () => patchSettings({ paused: !state.settings.paused }) });
  const paintQuick = () => {
    const s = state.settings;
    if (!s) return;
    modeBtn.replaceChildren(icon(s.mode === 'foreground' ? 'eye' : 'eyeoff'));
    pauseBtn.replaceChildren(icon(s.paused ? 'play' : 'pause'));
    pauseBtn.classList.toggle('on', !!s.paused);
  };
  paintQuick();
  bus.on('settings', paintQuick);
  rail.append(modeBtn, pauseBtn);
  pages.filter((p) => p.area === 'foot').forEach((p) => rail.append(mk(p, 0)));
}

function buildStatus() {
  const mk = (ic, tip) => { const t = h('span'); const w = h('span', { class: 'item', tip }, icon(ic, 'sm'), t); return { w, t }; };
  const engine = mk('bolt', t('引擎状态'));
  const mode = mk('eyeoff', t('当前模式'));
  const rate = mk('actions', t('外部调用速率'));
  const lat = mk('clock', t('点击操作的中位耗时'));
  const mem = mk('memory', t('进程内存'));
  const cli = mk('claude', t('已连接的客户端数'));
  const ver = mk('info', t('版本'));
  const upd = h('button', { type: 'button', class: 'chip warn', style: { display: 'none', cursor: 'pointer', border: 0 }, tip: t('有新版本，点击查看'), onClick: () => bus.emit('navigate', 'settings') });
  status.append(engine.w, mode.w, rate.w, lat.w, mem.w, cli.w, h('span', { class: 'sp' }), upd, ver.w);
  const paint = () => {
    const st = state.status, s = state.settings;
    if (!st || !s) return;
    engine.t.textContent = s.paused ? t('已暂停') : t('运行中');
    engine.w.firstChild.style.color = s.paused ? 'var(--c-warn)' : 'var(--c-ok)';
    mode.t.textContent = s.mode === 'foreground' ? t('前台模式') : t('后台模式');
    mode.w.replaceChildren(icon(s.mode === 'foreground' ? 'eye' : 'eyeoff', 'sm'), mode.t);
    const ext = state.series.ext.at(-1) ?? 0;
    rate.t.textContent = t('{n} 次/秒', { n: ext.toFixed(ext < 10 ? 1 : 0) });
    const click = state.perf?.methods?.find((m) => m.name === 'click');
    lat.t.textContent = click ? fmt.us(click.p50_us) : '—';
    mem.t.textContent = fmt.bytes(state.perf?.process?.working_set);
    cli.t.textContent = t('{n} 个客户端', { n: state.info?.clients ?? 0 });
    ver.t.textContent = `v${st.version}`;
    const u = state.update;
    const has = u && (u.state === 'available' || u.state === 'ready');
    upd.style.display = has ? '' : 'none';
    if (has) upd.textContent = t('新版本 {v}', { v: u.latest });
  };
  bus.on('status', paint);
  bus.on('settings', paint);
  bus.on('update', paint);
  paint();
}

addEventListener('keydown', (e) => {
  if (e.key === 'F5' || ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'r')) {
    if (state.info?.ui_from_disk) { e.preventDefault(); location.reload(); }
    return;
  }
  if (!(e.ctrlKey || e.metaKey) || e.altKey || e.shiftKey) return;
  const n = e.key === '0' ? 10 : +e.key;
  if (n >= 1 && n <= 10) {
    const p = getPages().filter((x) => x.area === 'main')[n - 1];
    if (p) { e.preventDefault(); go(p.id); }
  }
});

(async () => {
  try {
    await init();
  } catch (e) {
    app.replaceChildren(h('div', { class: 'empty', style: { height: '100vh' } }, icon('alert', 'lg'), h('div', null, t('无法连接引擎：{e}', { e: e.message })), h('button', { class: 'btn primary', onClick: () => location.reload() }, t('重试'))));
    return;
  }
  setLang(resolveLang(state.settings.language, state.info?.system_lang));
  localizeMeta();
  buildRail();
  buildStatus();
  // 换语言 = 整页重载：页面文字在渲染时写定，重载最干净；原生那边（托盘菜单、提示）已经即时生效
  bus.on('ev:settings', (s) => { if (resolveLang(s.language, state.info?.system_lang) !== getLang()) location.reload(); });
  const want = new URLSearchParams(location.search).get('page') || recall();
  go(getPages().some((p) => p.id === want) ? want : getPages()[0].id);
})();
