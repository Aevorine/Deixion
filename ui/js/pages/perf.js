// 性能：进程资源趋势、各方法的耗时分位数、一键自测。
import { h, panel, btn, asyncBtn, chip, empty, toast } from '../core/dom.js';
import { icon } from '../core/icons.js';
import { bus } from '../core/bus.js';
import { call } from '../core/bridge.js';
import { state } from '../core/store.js';
import { registerPage } from '../core/registry.js';
import { lineChart } from '../core/chart.js';
import { methodInfo } from '../core/meta.js';
import * as fmt from '../core/fmt.js';

registerPage({
  id: 'perf', icon: 'perf', tip: '性能', order: 70,
  render(root) {
    const kpi = (label, ic) => { const n = h('div', { class: 'n' }); return { n, el: h('div', { class: 'kpi' }, h('div', { class: 't' }, icon(ic, 'sm'), label), n) }; };
    const kCpu = kpi('CPU 占用', 'cpu');
    const kMem = kpi('内存', 'memory');
    const kRate = kpi('外部调用', 'bolt');
    const kUp = kpi('已运行', 'clock');

    const cv1 = h('canvas', { class: 'chart' });
    const cv2 = h('canvas', { class: 'chart' });
    const c1 = lineChart(cv1, () => [{ values: state.series.cpu, color: '--c-accent', fill: true, len: 150 }, { values: state.series.ext, color: '--c-warn', len: 150 }], { yFmt: (v) => v.toFixed(v < 10 ? 1 : 0) });
    const c2 = lineChart(cv2, () => [{ values: state.series.mem, color: '--c-ok', fill: true, len: 150 }], { yFmt: (v) => `${v.toFixed(0)}M` });

    const tbody = h('tbody');
    const table = h('table', { class: 'tbl' }, h('thead', null, h('tr', null, ['方法', '次数', '中位 P50', 'P90', 'P99', '最小', '最大', '平均', '分布'].map((t, i) => h('th', { class: i && i < 8 ? 'num' : '' }, t)))), tbody);
    const none = empty('调用后这里会出现各方法的耗时', 'perf');
    const cpuChips = h('div', { style: { display: 'flex', gap: '6px', flexWrap: 'wrap' } });

    function paint() {
      const st = state.status, pf = state.perf;
      if (!st || !pf) return;
      const cpu = state.series.cpu.at(-1) ?? 0, ext = state.series.ext.at(-1) ?? 0;
      kCpu.n.replaceChildren(`${cpu.toFixed(cpu < 10 ? 2 : 1)}%`, h('small', null, `${st.cpu?.threads ?? '?'} 线程`));
      kMem.n.replaceChildren(fmt.bytes(pf.process.working_set), h('small', null, `私有 ${fmt.bytes(pf.process.private)}`));
      kRate.n.replaceChildren(ext.toFixed(ext < 10 ? 1 : 0), h('small', null, '次/秒'));
      kUp.n.replaceChildren(fmt.dur(pf.uptime_s));
      cpuChips.replaceChildren(chip(st.cpu?.name || 'CPU'), chip('BMI2', st.cpu?.bmi2 ? 'ok' : ''), chip('AVX2', st.cpu?.avx2 ? 'ok' : ''), chip('SSE4.2', st.cpu?.sse42 ? 'ok' : ''), chip(st.activity_hook ? '活动钩子 开' : '活动钩子 休眠', st.activity_hook ? 'accent' : ''));
      const ms = [...(pf.methods || [])].sort((a, b) => b.count - a.count);
      none.style.display = ms.length ? 'none' : '';
      table.style.display = ms.length ? '' : 'none';
      const maxP99 = Math.max(1, ...ms.map((m) => m.p99_us));
      tbody.replaceChildren(...ms.map((m) => {
        const mi = methodInfo(m.name);
        return h('tr', null,
          h('td', { style: { maxWidth: 'none', width: '14em' } }, h('span', { class: 'chip accent' }, icon(mi.icon, 'sm'), mi.label), h('span', { style: { color: 'var(--c-text-3)', marginLeft: '8px' } }, m.name)),
          ...[fmt.int(m.count), fmt.us(m.p50_us), fmt.us(m.p90_us), fmt.us(m.p99_us), fmt.us(m.min_us), fmt.us(m.max_us), fmt.us(m.mean_us)].map((v) => h('td', { class: 'num' }, v)),
          h('td', { style: { width: '22%', maxWidth: 'none' } }, h('div', { class: 'dist', tip: `P50 ${fmt.us(m.p50_us)} · P99 ${fmt.us(m.p99_us)}` }, h('i', { class: 'p99', style: { width: `${(m.p99_us / maxP99) * 100}%` } }), h('i', { class: 'p50', style: { width: `${(m.p50_us / maxP99) * 100}%` } }))));
      }));
      c1.redraw();
      c2.redraw();
    }
    bus.on('status', paint);

    const benchOut = h('div', { class: 'rows' }, empty('点“一键自测”测量端到端耗时', 'flask'));
    const SUITE = [
      ['窗口列表', 'windows', {}, 30],
      ['整屏截图', 'capture', { window: 'screen', grid: true }, 12],
      ['引擎状态', 'status', {}, 60],
      ['坐标换算', 'geo', { window: 'screen', at: '0.5,0.5' }, 60],
    ];
    const bench = asyncBtn({ icon: 'flask', label: '一键自测', kind: 'primary' }, async () => {
      benchOut.replaceChildren();
      for (const [name, m, p, n] of SUITE) {
        const t = [];
        for (let i = 0; i < n; i++) { const t0 = performance.now(); await call(m, p); t.push(performance.now() - t0); }
        t.sort((a, b) => a - b);
        const q = (f) => t[Math.min(t.length - 1, Math.floor(f * t.length))];
        benchOut.append(h('div', { class: 'row' }, h('div', { class: 'k' }, `${name} × ${n}`), h('div', { class: 'v' }, chip(`P50 ${q(0.5).toFixed(2)} ms`, 'accent'), chip(`P99 ${q(0.99).toFixed(2)} ms`), chip(`最小 ${t[0].toFixed(2)} ms`))));
      }
      toast('自测完成（含网页与原生之间的往返）', 'ok');
    });

    const top = h('div', { class: 's12', style: { display: 'grid', gridTemplateColumns: 'repeat(4, minmax(0, 1fr))', gap: 'var(--sp-3)' } }, kCpu.el, kMem.el, kRate.el, kUp.el);
    root.append(h('div', { class: 'grid', style: { gridTemplateRows: 'auto minmax(0, 0.9fr) minmax(0, 1.1fr)' } },
      top,
      panel({ cls: 's6', label: 'CPU（蓝）与外部调用速率（黄）', flush: true, body: cv1 }),
      panel({ cls: 's6', label: '内存（MB）', flush: true, body: cv2 }),
      panel({ cls: 's8', label: '各方法耗时', flush: true, body: [h('div', { style: { overflow: 'auto', height: '100%' } }, table), none], acts: [cpuChips] }),
      panel({ cls: 's4', label: '自测', body: benchOut, acts: [bench] })));
    paint();
    return { show: paint };
  },
});
