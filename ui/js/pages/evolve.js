// 经验：每个应用、每类控件、每种操作下，各执行通道的战绩。引擎据此做 UCB 选择，越用越准。
import { h, panel, btn, asyncBtn, chip, empty, toast, copy, confirmBox } from '../core/dom.js';
import { icon } from '../core/icons.js';
import { bus } from '../core/bus.js';
import { call } from '../core/bridge.js';
import { registerPage } from '../core/registry.js';
import { methodInfo, strategyName } from '../core/meta.js';
import { tex } from '../core/math.js';
import { chartHost } from '../core/chart.js';
import { cssColor, alpha } from '../core/theme.js';
import * as fmt from '../core/fmt.js';
import { t } from '../core/i18n.js';

const C = 0.35;

registerPage({
  id: 'evolve', icon: 'evolve', tip: '经验', order: 50,
  render(root) {
    let arms = [];
    let summary = {};
    let app = '';

    const kpi = (label, ic) => { const n = h('div', { class: 'n' }); return { n, el: h('div', { class: 'kpi' }, h('div', { class: 't' }, icon(ic, 'sm'), label), n) }; };
    const kArms = kpi(t('通道记录'), 'layers');
    const kApps = kpi(t('已学习的应用'), 'window');
    const kObs = kpi(t('累计观测'), 'history');
    const appsList = h('div', { class: 'rows' });
    const arena = h('div', { class: 'arena' });
    const cv = h('canvas', { class: 'chart' });
    const legend = h('div', { style: { display: 'flex', gap: '6px', flexWrap: 'wrap' } });
    const PAL = ['--c-accent', '--c-ok', '--c-warn', '--c-danger', '--c-accent-2', '--c-text-2'];
    const colorOf = (name) => PAL[[...name].reduce((s, c) => s + c.charCodeAt(0), 0) % PAL.length];
    // 策略地形：横轴耗时（对数）、纵轴成功率、气泡大小是样本量；右上越靠右上角越慢越准，左上最理想。
    const terrain = chartHost(cv, (g, w, h2) => {
      const list = arms.filter((a) => !app || a.app === app);
      const padL = 44, padB = 26, padT = 10, padR = 14;
      const iw = w - padL - padR, ih = h2 - padT - padB;
      g.font = '12px "Times New Roman", serif';
      g.strokeStyle = cssColor('--c-line-soft'); g.fillStyle = cssColor('--c-text-3'); g.lineWidth = 1;
      g.textAlign = 'right'; g.textBaseline = 'middle';
      for (let i = 0; i <= 4; i++) { const y = padT + (ih * i) / 4; g.beginPath(); g.moveTo(padL, y); g.lineTo(w - padR, y); g.stroke(); g.fillText(`${100 - i * 25}%`, padL - 6, y); }
      if (!list.length) return;
      const lat = list.map((a) => Math.max(0.05, a.latency_ms));
      const lo = Math.log10(Math.min(...lat)) - 0.15, hi = Math.log10(Math.max(...lat)) + 0.15;
      g.textAlign = 'center'; g.textBaseline = 'top';
      for (let e = Math.ceil(lo); e <= Math.floor(hi); e++) { const x = padL + ((e - lo) / (hi - lo)) * iw; g.beginPath(); g.moveTo(x, padT); g.lineTo(x, padT + ih); g.stroke(); g.fillText(fmt.ms(10 ** e), x, padT + ih + 6); }
      const nmax = Math.max(...list.map((a) => a.n));
      for (const a of list) {
        const x = padL + ((Math.log10(Math.max(0.05, a.latency_ms)) - lo) / (hi - lo)) * iw;
        const y = padT + ih - a.success_rate * ih;
        const r = 5 + 11 * Math.sqrt(a.n / nmax);
        const c = cssColor(colorOf(a.strategy));
        g.beginPath(); g.arc(x, y, r, 0, Math.PI * 2); g.fillStyle = alpha(c, 0.35); g.fill(); g.strokeStyle = c; g.lineWidth = 1.6; g.stroke();
      }
    });

    function groupBy(list, keyf) {
      const m = new Map();
      for (const a of list) { const k = keyf(a); (m.get(k) || m.set(k, []).get(k)).push(a); }
      return m;
    }

    function paintApps() {
      appsList.replaceChildren();
      const byApp = groupBy(arms, (a) => a.app);
      const rows = [...byApp.entries()].map(([name, list]) => ({ name, n: list.reduce((s, a) => s + a.total, 0), sr: list.reduce((s, a) => s + a.success_rate * a.n, 0) / Math.max(1e-9, list.reduce((s, a) => s + a.n, 0)), arms: list.length }))
        .sort((a, b) => b.n - a.n);
      const all = h('button', { type: 'button', class: 'app-item' + (app === '' ? ' on' : ''), onClick: () => { app = ''; paintApps(); paintArena(); paintLegend(); terrain.redraw(); } }, h('span', { class: 'nm' }, t('全部应用')), chip(String(rows.length)));
      appsList.append(all);
      for (const r of rows) {
        appsList.append(h('button', { type: 'button', class: 'app-item' + (app === r.name ? ' on' : ''), onClick: () => { app = r.name; paintApps(); paintArena(); paintLegend(); terrain.redraw(); } },
          h('span', { class: 'nm', tip: r.name }, r.name),
          h('span', { class: 'bar ' + (r.sr >= 0.9 ? 'ok' : r.sr >= 0.6 ? 'warn' : 'danger'), style: { width: '64px', '--w': `${(r.sr * 100).toFixed(0)}%` } }, h('i')),
          h('span', { class: 'cnt' }, fmt.int(r.n))));
      }
      if (!rows.length) appsList.replaceChildren(empty(t('Claude 操作过的应用会出现在这里'), 'evolve'));
    }

    function paintArena() {
      arena.replaceChildren();
      const list = arms.filter((a) => !app || a.app === app);
      if (!list.length) { arena.append(empty(t('还没有可展示的经验'), 'evolve')); return; }
      const groups = groupBy(list, (a) => `${a.app}|${a.role}|${a.action}`);
      const cards = [...groups.entries()].map(([k, g]) => {
        const N = g.reduce((s, a) => s + a.n, 0);
        const scored = g.map((a) => ({ ...a, ucb: a.mean_reward + C * Math.sqrt(Math.log(Math.max(2, N)) / Math.max(0.5, a.n)) })).sort((x, y) => y.ucb - x.ucb);
        return { k, g: scored, N };
      }).sort((a, b) => b.N - a.N);
      for (const c of cards) {
        const [capp, role, action] = c.k.split('|');
        const mi = methodInfo(action);
        arena.append(h('section', { class: 'arena-card' },
          h('header', null, h('span', { class: 'chip accent' }, icon(mi.icon, 'sm'), mi.label), h('span', { class: 'chip' }, role || '—'), app ? null : h('span', { class: 'app' }, capp), h('span', { style: { flex: 1 } }), h('span', { class: 'sub' }, `n=${c.N.toFixed(1)}`)),
          ...c.g.map((a, i) => h('div', { class: 'arm' + (i === 0 ? ' best' : ''), tip: t('平均收益 {r} · UCB {u} · 累计 {n} 次', { r: a.mean_reward.toFixed(3), u: a.ucb.toFixed(3), n: fmt.int(a.total) }) },
            h('span', { class: 'nm' }, i === 0 ? icon('bolt', 'sm') : null, strategyName(a.strategy)),
            h('span', { class: 'bar ' + (a.success_rate >= 0.9 ? 'ok' : a.success_rate >= 0.6 ? 'warn' : 'danger'), style: { '--w': `${(a.success_rate * 100).toFixed(0)}%` } }, h('i')),
            h('span', { class: 'pc' }, fmt.pct(a.success_rate, 0)),
            h('span', { class: 'ms' }, fmt.ms(a.latency_ms))))));
      }
    }

    function paintLegend() {
      const names = [...new Set(arms.filter((a) => !app || a.app === app).map((a) => a.strategy))];
      legend.replaceChildren(...names.map((n) => h('span', { class: 'chip' }, h('i', { class: 'dot', style: { background: `var(${colorOf(n)})` } }), strategyName(n))));
    }

    async function load() {
      try {
        const r = await call('experience', { limit: 2000 });
        arms = r.arms || [];
        summary = r.summary || {};
        kArms.n.textContent = fmt.int(summary.arms ?? 0);
        kApps.n.textContent = fmt.int(summary.apps ?? 0);
        kObs.n.textContent = fmt.int(Math.round(summary.observations ?? 0));
        paintApps();
        paintArena();
        paintLegend();
        terrain.redraw();
      } catch (e) { toast(e.message, 'danger'); }
    }

    const left = h('div', { class: 's4 stack', style: { gridTemplateRows: 'auto minmax(0, 1fr) auto' } },
      h('div', { style: { display: 'grid', gridTemplateColumns: 'repeat(3, minmax(0, 1fr))', gap: 'var(--sp-3)' } }, kArms.el, kApps.el, kObs.el),
      panel({ label: t('应用'), body: appsList, flush: false }),
      panel({ label: t('选择规则'), col: true, body: [tex('\\mathrm{UCB}_i=\\bar r_i+c\\sqrt{\\dfrac{\\ln N}{n_i}},\\ c=0.35', { block: true }), tex('n\\leftarrow\\gamma n,\\ \\gamma=0.985', { block: true })] }));
    const arenaPanel = panel({
      label: t('策略擂台'),
      acts: [
        btn({ icon: 'refresh', tip: t('刷新'), kind: 'ghost', onClick: load }),
        asyncBtn({ icon: 'copy', tip: t('复制全部经验（JSON）'), kind: 'ghost' }, async () => { const r = await call('experience', { export: true, limit: 1 }); await copy(JSON.stringify(r.export, null, 2), call); }),
        asyncBtn({ icon: 'broom', tip: t('重置经验'), kind: 'ghost' }, async () => { if (await confirmBox(t('重置经验'), t('所有已学到的通道偏好都会清空，之后从头学习。确定吗？'), true, t('重置'))) { await call('experience.reset'); await load(); toast(t('已重置'), 'ok'); } }),
      ],
      body: arena,
    });
    const right = h('div', { class: 's8 stack', style: { gridTemplateRows: 'minmax(0, 1.1fr) minmax(0, 0.9fr)' } }, arenaPanel, panel({ label: t('策略地形（横轴耗时 · 纵轴成功率 · 圆越大样本越多）'), flush: true, body: cv, acts: [legend] }));
    root.append(h('div', { class: 'grid', style: { gridTemplateRows: 'minmax(0, 1fr)' } }, left, right));
    bus.on('action', () => { if (root.classList.contains('on')) load(); });
    load();
    return { show: load };
  },
});
