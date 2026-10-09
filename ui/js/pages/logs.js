// 日志：引擎与应用的实时日志。按级别与关键字筛选，虚拟列表，几千行也流畅。
import { h, panel, btn, asyncBtn, seg, toggle, chip, empty, toast, copy } from '../core/dom.js';
import { icon } from '../core/icons.js';
import { bus } from '../core/bus.js';
import { call } from '../core/bridge.js';
import { registerPage } from '../core/registry.js';
import { vlist } from '../core/vlist.js';
import { debounce } from '../core/fmt.js';
import { sparkline } from '../core/chart.js';
import * as fmt from '../core/fmt.js';

const ORDER = { trace: 0, debug: 1, info: 2, warn: 3, error: 4 };
const KIND = { debug: '', info: 'accent', warn: 'warn', error: 'danger' };
const CAP = 6000;

registerPage({
  id: 'logs', icon: 'logs', tip: '日志', order: 60,
  render(root) {
    let all = [];
    let level = 'info';
    let query = '';
    let follow = true;
    let lastId = 0;

    const list = vlist({
      rowH: 30, stick: true,
      render: (r) => [
        h('span', { style: { color: 'var(--c-text-3)', width: '7.2em', fontVariantNumeric: 'tabular-nums' } }, fmt.timeMs(r.t)),
        h('span', { class: 'chip ' + (KIND[r.lv] || ''), style: { minWidth: '3.4em', justifyContent: 'center' } }, { debug: '调试', info: '信息', warn: '警告', error: '错误' }[r.lv] || r.lv),
        h('span', { style: { color: 'var(--c-text-3)', width: '6em', overflow: 'hidden', textOverflow: 'ellipsis' } }, r.cat),
        h('span', { style: { flex: 1, minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis' }, tip: r.msg.length > 90 ? r.msg : null }, r.msg),
      ],
      onRow: (r, el) => el.addEventListener('dblclick', () => copy(`${fmt.dateTime(r.t)} [${r.lv}] ${r.cat}: ${r.msg}`, call)),
    });
    const none = empty('没有符合条件的日志', 'logs');
    const count = chip('');
    const levelSeg = seg({ value: 'info', options: [{ value: 'debug', label: '调试' }, { value: 'info', label: '信息' }, { value: 'warn', label: '警告' }, { value: 'error', label: '错误' }], onChange: (v) => { level = v; paint(); } });
    const q = h('input', { class: 'input', placeholder: '关键字', style: { width: '100%' } });
    q.addEventListener('input', debounce(() => { query = q.value.trim().toLowerCase(); paint(); }, 160));
    const followSw = toggle({ value: true, tip: '跟随最新', onChange: (v) => { follow = v; if (v) list.scrollToEnd(); } });

    function visible() {
      const min = ORDER[level] ?? 2;
      return all.filter((r) => (ORDER[r.lv] ?? 2) >= min && (!query || r.msg.toLowerCase().includes(query) || r.cat.toLowerCase().includes(query)));
    }
    // 右侧统计：各级别条数、来源前几名、近 30 分钟每分钟日志量
    const lvBox = h('div', { class: 'rows' });
    const catBox = h('div', { class: 'rows' });
    const perMin = [];
    const cv = h('canvas', { class: 'chart', style: { height: '90px' } });
    const spark = sparkline(cv, () => perMin, { color: '--c-accent', min: 0 });
    function paintStats() {
      const cnt = { debug: 0, info: 0, warn: 0, error: 0 };
      const cats = new Map();
      const now = Date.now();
      perMin.length = 0;
      for (let i = 0; i < 30; i++) perMin.push(0);
      for (const r of all) {
        cnt[r.lv] = (cnt[r.lv] || 0) + 1;
        cats.set(r.cat, (cats.get(r.cat) || 0) + 1);
        const age = Math.floor((now - r.t) / 60000);
        if (age >= 0 && age < 30) perMin[29 - age]++;
      }
      const max = Math.max(1, ...Object.values(cnt));
      lvBox.replaceChildren(...[['error', '错误', 'danger'], ['warn', '警告', 'warn'], ['info', '信息', 'accent'], ['debug', '调试', '']].map(([k, t, kind]) => h('div', { class: 'row' }, h('div', { class: 'k' }, chip(t, kind)), h('div', { class: 'v', style: { flex: 1 } }, h('div', { class: 'bar ' + (kind === 'danger' ? 'danger' : kind === 'warn' ? 'warn' : ''), style: { flex: 1, '--w': `${(cnt[k] / max) * 100}%` } }, h('i')), h('span', { style: { minWidth: '3em', textAlign: 'right' } }, fmt.int(cnt[k]))))));
      catBox.replaceChildren(...[...cats.entries()].sort((a, b) => b[1] - a[1]).slice(0, 9).map(([c, n]) => h('div', { class: 'row' }, h('div', { class: 'k' }, c), h('div', { class: 'v' }, fmt.int(n)))));
      spark.redraw();
    }

    function paint() {
      paintStats();
      const v = visible();
      list.set(v);
      count.textContent = `${v.length} / ${all.length}`;
      none.style.display = v.length ? 'none' : '';
      list.root.style.display = v.length ? '' : 'none';
      if (follow) list.scrollToEnd();
    }
    const add = (recs) => {
      for (const r of recs) if (r.id > lastId) { all.push(r); lastId = r.id; }
      if (all.length > CAP) all = all.slice(-CAP);
    };
    const paintSoon = debounce(paint, 60);
    bus.on('ev:log', (d) => { add(d.records || []); paintSoon(); });

    async function load() {
      try { const r = await call('log', { limit: 1500, level: 'debug' }); all = []; lastId = 0; add(r.records || []); paint(); } catch (e) { toast(e.message, 'danger'); }
    }

    const p = panel({
      label: [count], flush: true, cls: 's9',
      acts: [h('div', { class: 'search', style: { width: '240px' } }, icon('search', 'sm'), q), levelSeg,
        h('span', { style: { display: 'inline-flex', alignItems: 'center', gap: '6px' } }, icon('chevD', 'sm'), followSw),
        btn({ icon: 'broom', tip: '清空当前显示', kind: 'ghost', onClick: () => { all = []; paint(); } }),
        btn({ icon: 'folder', tip: '打开日志目录', kind: 'ghost', onClick: () => call('app.open', { kind: 'logs' }).catch((e) => toast(e.message, 'warn')) }),
        asyncBtn({ icon: 'copy', tip: '复制当前显示的日志', kind: 'ghost' }, async () => { await copy(visible().map((r) => `${fmt.dateTime(r.t)} [${r.lv}] ${r.cat}: ${r.msg}`).join('\n'), call); })],
      body: [list.root, none],
    });
    const side = h('div', { class: 's3 stack', style: { gridTemplateRows: 'auto minmax(0, 1fr) auto' } }, panel({ label: '级别', body: lvBox }), panel({ label: '来源', body: catBox }), panel({ label: '每分钟条数（近 30 分钟）', flush: true, body: cv }));
    root.append(h('div', { class: 'grid', style: { gridTemplateRows: 'minmax(0, 1fr)' } }, p, side));
    load();
    return { show() { load(); } };
  },
});
