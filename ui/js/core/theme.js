// 主题：令牌里的颜色是 light-dark()，这里负责 data-theme / data-density、取解析后的颜色、并把标题栏颜色同步给原生窗口。
import { bus } from './bus.js';
import { call } from './bridge.js';

let probe;
const cache = new Map();

/** 解析某个颜色令牌当前的实际颜色（rgb 字符串），图表与画布用。 */
export function cssColor(name) {
  if (cache.has(name)) return cache.get(name);
  if (!probe) { probe = document.createElement('i'); probe.style.cssText = 'position:absolute;visibility:hidden;pointer-events:none'; document.documentElement.append(probe); }
  probe.style.color = `var(${name})`;
  const v = getComputedStyle(probe).color;
  cache.set(name, v);
  return v;
}
export const alpha = (rgb, a) => rgb.replace(/^rgb\(/, 'rgba(').replace(/\)$/, `, ${a})`);
const hex = (rgb) => '#' + (rgb.match(/\d+/g) || [0, 0, 0]).slice(0, 3).map((n) => (+n).toString(16).padStart(2, '0')).join('');
const dm = matchMedia('(prefers-color-scheme: dark)');
export const isDark = () => (document.documentElement.dataset.theme ? document.documentElement.dataset.theme === 'dark' : dm.matches);

let last = { theme: 'auto', density: 'standard' };
export function applyTheme(s = last) {
  last = { theme: s.theme || 'auto', density: s.density || 'standard' };
  const root = document.documentElement;
  if (last.theme === 'auto') root.removeAttribute('data-theme'); else root.dataset.theme = last.theme;
  root.dataset.density = last.density;
  cache.clear();
  bus.emit('theme');
  call('app.theme', { dark: isDark(), bg: hex(cssColor('--c-bg')), fg: hex(cssColor('--c-text')) }).catch(() => {});
}
dm.addEventListener('change', () => { if (last.theme === 'auto') applyTheme(); });
bus.on('ev:system_theme', () => { if (last.theme === 'auto') applyTheme(); });
