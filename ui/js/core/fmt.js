// 数字、时间、字节的统一格式化。
import { t } from './i18n.js';
export const pad2 = (n) => String(n).padStart(2, '0');

export function time(ms) {
  const d = new Date(ms);
  return `${pad2(d.getHours())}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())}`;
}
export function timeMs(ms) {
  const d = new Date(ms);
  return `${time(ms)}.${String(d.getMilliseconds()).padStart(3, '0')}`;
}
export function dateTime(ms) {
  const d = new Date(ms);
  return `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())} ${time(ms)}`;
}

/** 微秒 → 人读：<1ms 用 µs，<1s 用 ms，否则 s。 */
export function us(v) {
  if (v == null || Number.isNaN(v)) return '—';
  if (v < 1000) return `${Math.round(v)} µs`;
  if (v < 1e6) return `${(v / 1000).toFixed(v < 1e4 ? 2 : v < 1e5 ? 1 : 0)} ms`;
  return `${(v / 1e6).toFixed(2)} s`;
}
export function ms(v) {
  if (v == null || Number.isNaN(v)) return '—';
  return v < 1 ? `${(v * 1000).toFixed(0)} µs` : v < 1000 ? `${v.toFixed(v < 10 ? 2 : 1)} ms` : `${(v / 1000).toFixed(2)} s`;
}
export function bytes(n) {
  if (n == null) return '—';
  const u = ['B', 'KB', 'MB', 'GB'];
  let i = 0;
  let v = n;
  while (v >= 1024 && i < u.length - 1) { v /= 1024; i++; }
  return `${v.toFixed(v < 10 && i ? 1 : 0)} ${u[i]}`;
}
export function dur(s) {
  s = Math.max(0, Math.floor(s));
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60);
  return h ? t('{h} 时 {m} 分', { h, m }) : m ? t('{m} 分 {s} 秒', { m, s: s % 60 }) : t('{s} 秒', { s });
}
export const int = (n) => (n == null ? '—' : Number(n).toLocaleString('en-US'));
export const pct = (v, d = 1) => (v == null ? '—' : `${(v * 100).toFixed(d)}%`);
export const fixed = (v, d = 4) => (v == null ? '—' : Number(v).toFixed(d));
export const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

/** "0x1A2B" → 数值，供比较。 */
export const hwndNum = (s) => parseInt(String(s), 16) || 0;

export function debounce(fn, wait) {
  let t = 0;
  return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), wait); };
}
export function throttleFrame(fn) {
  let queued = false;
  return (...a) => {
    if (queued) return;
    queued = true;
    requestAnimationFrame(() => { queued = false; fn(...a); });
  };
}
