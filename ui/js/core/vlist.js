// 虚拟列表：只渲染视口内的行，几千条日志 / 动作也不卡。行高固定。
import { h } from './dom.js';

/**
 * vlist({ rowH, render(item, index) → Node[]|Node, onRow(item, el), stick })
 * stick: 滚动到底部时新数据来了自动跟随（日志用）。
 */
export function vlist({ rowH = 34, render, onRow, stick = false, cls = '' }) {
  const root = h('div', { class: 'vl ' + cls });
  const pad = h('div', { class: 'pad' });
  root.append(pad);
  let items = [];
  const live = new Map();
  let pinned = true;

  function fill() {
    const top = root.scrollTop, vh = root.clientHeight || 400;
    const a = Math.max(0, Math.floor(top / rowH) - 4);
    const b = Math.min(items.length, Math.ceil((top + vh) / rowH) + 4);
    for (const [i, el] of live) if (i < a || i >= b) { el.remove(); live.delete(i); }
    for (let i = a; i < b; i++) {
      if (live.has(i)) continue;
      const el = h('div', { class: 'it', style: { top: `${i * rowH}px`, height: `${rowH}px` } }, render(items[i], i));
      onRow?.(items[i], el, i);
      pad.append(el);
      live.set(i, el);
    }
  }
  root.addEventListener('scroll', () => { pinned = root.scrollTop + root.clientHeight >= root.scrollHeight - rowH; fill(); }, { passive: true });
  new ResizeObserver(fill).observe(root);

  return {
    root,
    set(next) {
      items = next;
      for (const el of live.values()) el.remove();
      live.clear();
      pad.style.height = `${items.length * rowH}px`;
      if (stick && pinned) root.scrollTop = root.scrollHeight;
      fill();
    },
    get length() { return items.length; },
    scrollToEnd() { root.scrollTop = root.scrollHeight; pinned = true; },
    scrollToStart() { root.scrollTop = 0; },
  };
}
