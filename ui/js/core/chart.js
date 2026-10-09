// 轻量画布图表：折线、迷你趋势。只在数据变化时重画，随容器尺寸与主题自动重绘。
import { cssColor, alpha } from './theme.js';
import { bus } from './bus.js';

const hosts = new Set();
bus.on('theme', () => hosts.forEach((h) => h.redraw()));

/** 绑定画布与绘制函数；返回 { redraw }。 */
export function chartHost(canvas, draw) {
  let raf = 0;
  const host = {
    redraw() {
      cancelAnimationFrame(raf);
      raf = requestAnimationFrame(() => {
        const r = canvas.getBoundingClientRect();
        if (r.width < 2 || r.height < 2) return;
        const dpr = devicePixelRatio || 1;
        const w = Math.round(r.width * dpr), h = Math.round(r.height * dpr);
        if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
        const g = canvas.getContext('2d');
        g.setTransform(dpr, 0, 0, dpr, 0, 0);
        g.clearRect(0, 0, r.width, r.height);
        draw(g, r.width, r.height);
      });
    },
  };
  new ResizeObserver(() => host.redraw()).observe(canvas);
  hosts.add(host);
  return host;
}

/** 迷你趋势：values 为数组。 */
export function sparkline(canvas, getValues, { color = '--c-accent', min, max } = {}) {
  return chartHost(canvas, (g, w, h) => {
    const v = getValues();
    if (!v || v.length < 2) return;
    const lo = min ?? Math.min(...v), hi = max ?? Math.max(...v);
    const span = hi - lo || 1;
    const x = (i) => (i / (v.length - 1)) * w;
    const y = (val) => h - 3 - ((val - lo) / span) * (h - 6);
    const c = cssColor(color);
    g.beginPath();
    v.forEach((val, i) => (i ? g.lineTo(x(i), y(val)) : g.moveTo(x(i), y(val))));
    g.strokeStyle = c; g.lineWidth = 1.6; g.lineJoin = 'round'; g.stroke();
    g.lineTo(w, h); g.lineTo(0, h); g.closePath();
    const grad = g.createLinearGradient(0, 0, 0, h);
    grad.addColorStop(0, alpha(c, 0.28)); grad.addColorStop(1, alpha(c, 0));
    g.fillStyle = grad; g.fill();
  });
}

/** 多序列折线：series=[{values,color}]，yFmt 格式化纵轴刻度。 */
export function lineChart(canvas, getSeries, { min = 0, max, yFmt = (v) => v, rows = 4 } = {}) {
  return chartHost(canvas, (g, w, h) => {
    const series = getSeries();
    const padL = 46, padB = 6, padT = 8, padR = 6;
    const iw = w - padL - padR, ih = h - padT - padB;
    const all = series.flatMap((s) => s.values);
    const hi = max ?? Math.max(1e-9, ...all) * 1.15;
    const lo = min;
    g.font = `${getComputedStyle(canvas).fontSize || '12px'} "Times New Roman", serif`;
    g.textBaseline = 'middle'; g.textAlign = 'right';
    g.lineWidth = 1;
    for (let i = 0; i <= rows; i++) {
      const yy = padT + (ih * i) / rows;
      g.strokeStyle = cssColor('--c-line-soft');
      g.beginPath(); g.moveTo(padL, yy); g.lineTo(w - padR, yy); g.stroke();
      g.fillStyle = cssColor('--c-text-3');
      g.fillText(String(yFmt(hi - ((hi - lo) * i) / rows)), padL - 6, yy);
    }
    for (const s of series) {
      const v = s.values;
      if (v.length < 2) continue;
      const c = cssColor(s.color);
      g.beginPath();
      v.forEach((val, i) => {
        const xx = padL + (i / (s.len ? s.len - 1 : v.length - 1)) * iw + (s.len ? iw - (v.length / s.len) * iw : 0);
        const yy = padT + ih - ((val - lo) / (hi - lo)) * ih;
        i ? g.lineTo(xx, yy) : g.moveTo(xx, yy);
      });
      g.strokeStyle = c; g.lineWidth = 1.8; g.lineJoin = 'round'; g.stroke();
      if (s.fill) {
        const last = padL + iw;
        g.lineTo(last, padT + ih); g.lineTo(padL + (s.len ? iw - (v.length / s.len) * iw : 0), padT + ih); g.closePath();
        const grad = g.createLinearGradient(0, padT, 0, padT + ih);
        grad.addColorStop(0, alpha(c, 0.22)); grad.addColorStop(1, alpha(c, 0));
        g.fillStyle = grad; g.fill();
      }
    }
  });
}
