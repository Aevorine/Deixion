// 总览：控制台、关键指标、实时动作流、落点雷达。
import { h, panel, seg, toggle, row, asyncBtn, chip, empty, kbd } from '../core/dom.js';
import { icon } from '../core/icons.js';
import { bus } from '../core/bus.js';
import { call } from '../core/bridge.js';
import { state, patchSettings, refreshActions, tipKey } from '../core/store.js';
import { registerPage } from '../core/registry.js';
import { vlist } from '../core/vlist.js';
import { sparkline } from '../core/chart.js';
import { actionCells } from '../core/actionrow.js';
import { codeOf, parseLL } from '../core/geo.js';
import { t } from '../core/i18n.js';
import * as fmt from '../core/fmt.js';

const HK = [['toggle', 'eye', '显示 / 隐藏窗口'], ['mode', 'eyeoff', '切换前台 / 后台'], ['pause', 'pause', '暂停 / 继续'], ['undo', 'undo', '撤销上一步'], ['shot', 'camera', '截图到剪贴板'], ['stop', 'stop', '紧急停止']];
function hkKbd(name) {
  const box = h('span');
  const paint = () => box.replaceChildren(kbd(state.hotkeys.find((k) => k.name === name)?.chord || state.settings.hotkeys?.[name] || ''));
  paint();
  bus.on('hotkeys', paint);
  bus.on('settings', paint);
  return box;
}

function controlPanel() {
  const modeSeg = seg({
    big: true,
    value: state.settings.mode,
    options: [
      { value: 'background', icon: 'eyeoff', label: t('后台'), tip: t('后台模式：全程隐藏，不抢焦点、不动光标') },
      { value: 'foreground', icon: 'eye', label: t('前台'), tip: t('前台模式：显示光标轨迹与操作过程') },
    ],
    onChange: (v) => patchSettings({ mode: v }),
  });
  const pause = toggle({ value: state.settings.paused, tip: tipKey(t('暂停接收操作'), 'pause'), onChange: (v) => patchSettings({ paused: v }) });
  const hop = toggle({ value: state.settings.allow_hop, tip: t('后台通道都失败时，允许短暂切到前台完成操作'), onChange: (v) => patchSettings({ allow_hop: v }) });
  const overlay = toggle({ value: state.settings.overlay, tip: t('前台模式下显示落点涟漪与高亮框'), onChange: (v) => patchSettings({ overlay: v }) });
  const speed = seg({
    value: state.settings.speed,
    options: [{ value: 'instant', label: t('瞬时'), tip: t('不做任何平滑，光标瞬间到位') }, { value: 'fast', label: t('快速'), tip: t('短促平滑，肉眼可跟随') }, { value: 'smooth', label: t('平滑'), tip: t('较慢的拟人路径') }],
    onChange: (v) => patchSettings({ speed: v }),
  });
  const verify = toggle({ value: state.settings.verify !== 'off', tip: t('操作后用事件确认是否生效'), onChange: (v) => patchSettings({ verify: v ? 'auto' : 'off' }) });
  const sync = () => {
    const s = state.settings;
    modeSeg.set(s.mode);
    pause.set(s.paused);
    hop.set(s.allow_hop);
    overlay.set(s.overlay);
    speed.set(s.speed);
    verify.set(s.verify !== 'off');
  };
  bus.on('settings', sync);

  const cmd = (name) => () => call('app.cmd', { name });
  const actions = h('div', { style: { display: 'grid', gridTemplateColumns: 'repeat(3, minmax(0, 1fr))', gap: 'var(--sp-2)' } },
    asyncBtn({ icon: 'camera', label: t('截图'), tip: tipKey(t('截图（含经纬网格）到剪贴板'), 'shot'), big: true }, cmd('shot')),
    asyncBtn({ icon: 'undo', label: t('撤销'), tip: tipKey(t('撤销上一步操作'), 'undo'), big: true }, cmd('undo')),
    asyncBtn({ icon: 'stop', label: t('停止'), tip: tipKey(t('紧急停止正在执行的批处理'), 'stop'), big: true, kind: 'danger' }, cmd('stop')));
  return panel({
    cls: 's5',
    col: true,
    body: [
      h('div', { style: { display: 'flex', justifyContent: 'center' } }, modeSeg),
      h('div', { class: 'rows two' }, row(t('暂停接收'), pause), row(t('短暂切前台'), hop), row(t('显示轨迹'), overlay), row(t('操作后验证'), verify)),
      h('div', { class: 'rows' }, row(t('速度'), speed)),
      h('div', { class: 'hk-legend' }, HK.map(([n, ic, tp]) => h('span', { class: 'hk', tip: t(tp) }, icon(ic, 'sm'), hkKbd(n)))),
      h('div', { style: { flex: 1 } }),
      actions,
    ],
  });
}

function kpis() {
  const mk = (ic, label, cls = '') => {
    const n = h('div', { class: 'n' });
    const cv = h('canvas');
    const el = h('div', { class: 'kpi ' + cls }, h('div', { class: 't' }, icon(ic, 'sm'), label), n, cv);
    return { el, n, cv };
  };
  const calls = mk('bolt', t('调用次数'));
  const succ = mk('ok', t('成功率'));
  const lat = mk('clock', t('点击耗时中位数'));
  const exp = mk('evolve', t('自学习经验'));
  sparkline(calls.cv, () => state.series.ext, { color: '--c-accent' });
  const cpuSpark = sparkline(succ.cv, () => state.series.cpu, { color: '--c-ok', min: 0 });
  const memSpark = sparkline(lat.cv, () => state.series.mem, { color: '--c-accent-2' });
  const paint = () => {
    const st = state.status, pf = state.perf;
    if (!st) return;
    calls.n.replaceChildren(fmt.int(st.calls), h('small', null, t('错误 {n}', { n: fmt.int(st.errors) })));
    const rate = st.calls ? (st.calls - st.errors) / st.calls : 1;
    succ.n.replaceChildren(fmt.pct(rate, 1), h('small', null, `CPU ${(state.series.cpu.at(-1) ?? 0).toFixed(1)}%`));
    succ.el.className = 'kpi ' + (rate >= 0.95 ? 'ok' : rate >= 0.8 ? 'warn' : 'danger');
    const click = pf?.methods?.find((m) => m.name === 'click');
    lat.n.replaceChildren(click ? fmt.us(click.p50_us) : '—', h('small', null, click ? `P99 ${fmt.us(click.p99_us)}` : t('尚无点击')));
    const e = st.experience || {};
    exp.n.replaceChildren(fmt.int(e.arms ?? 0), h('small', null, t('{apps} 个应用 · {obs} 次观测', { apps: fmt.int(e.apps ?? 0), obs: fmt.int(e.observations ?? 0) })));
    cpuSpark.redraw();
    memSpark.redraw();
  };
  bus.on('status', paint);
  paint();
  return h('div', { class: 's7', style: { display: 'grid', gridTemplateColumns: 'repeat(2, minmax(0, 1fr))', gridTemplateRows: 'repeat(2, minmax(0, 1fr))', gap: 'var(--sp-3)' } }, calls.el, succ.el, lat.el, exp.el);
}

function feedPanel() {
  const list = vlist({ rowH: 38, render: actionCells });
  const none = empty(t('Claude 发出的操作会实时出现在这里'), 'actions');
  const p = panel({
    cls: 's7', label: t('实时动作流'), flush: true, body: [list.root, none],
    acts: [chip('', ''), asyncBtn({ icon: 'refresh', tip: t('刷新'), kind: 'ghost' }, () => refreshActions())],
  });
  const count = p.head.querySelector('.chip');
  const paint = () => {
    list.set(state.actions);
    count.textContent = t('{n} 条', { n: state.actions.length });
    none.style.display = state.actions.length ? 'none' : '';
    list.root.style.display = state.actions.length ? '' : 'none';
  };
  bus.on('action', paint);
  bus.on('actions', paint);
  paint();
  return p;
}

function radarPanel() {
  const field = h('div', { class: 'radar' });
  const readout = h('div', { class: 'radar-read' });
  const axisX = h('div', { class: 'radar-x' }, [0, 0.2, 0.4, 0.6, 0.8, 1].map((v) => h('span', { style: { left: `${v * 100}%` } }, v.toFixed(1))));
  const axisY = h('div', { class: 'radar-y' }, [0.2, 0.4, 0.6, 0.8, 1].map((v) => h('span', { style: { top: `${v * 100}%` } }, v.toFixed(1))));
  const wrap = h('div', { class: 'radar-wrap' }, axisX, axisY, field);
  const p = panel({ cls: 's5', label: t('落点雷达（λ 横向 · φ 纵向）'), col: true, body: [wrap, readout] });
  let seen = 0;
  const paint = (fresh) => {
    const pts = state.actions.filter((a) => a.at).slice(0, 40);
    field.replaceChildren();
    if (!pts.length) { field.append(h('div', { class: 'radar-empty' }, empty(t('带坐标的点击会在这里留下落点'), 'locate'))); readout.replaceChildren(); return; }
    pts.forEach((a, i) => {
      const ll = parseLL(a.at);
      if (!ll) return;
      const d = h('i', { class: 'pt ' + (a.ok ? 'ok' : 'bad') + (i === 0 ? ' last' : ''), style: { left: `${ll.lam * 100}%`, top: `${ll.phi * 100}%`, opacity: String(Math.max(0.18, 1 - i / 36)) } });
      if (i === 0 && fresh) d.classList.add('new');
      field.append(d);
    });
    const a = pts[0];
    const ll = parseLL(a.at);
    readout.replaceChildren(
      h('span', { class: 'chip accent', tip: t('横向经度 λ') }, `λ ${ll.lam.toFixed(4)}`),
      h('span', { class: 'chip accent', tip: t('纵向纬度 φ') }, `φ ${ll.phi.toFixed(4)}`),
      h('span', { class: 'chip', tip: t('3 级 Meridian 码') }, codeOf(ll.lam, ll.phi, 3)),
      h('span', { style: { flex: 1 } }),
      h('span', { style: { color: 'var(--c-text-3)' } }, `${a.app || ''} · ${fmt.time(a.ts)}`),
    );
  };
  bus.on('action', () => paint(true));
  bus.on('actions', () => paint(false));
  paint(false);
  return p;
}

registerPage({
  id: 'overview', icon: 'overview', tip: '总览', order: 10,
  render(root) {
    const grid = h('div', { class: 'grid', style: { gridTemplateRows: 'minmax(min-content, 42%) minmax(0, 1fr)' } }, controlPanel(), kpis(), feedPanel(), radarPanel());
    root.append(grid);
  },
});
