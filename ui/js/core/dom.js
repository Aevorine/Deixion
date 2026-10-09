// DOM 助手：h() 建节点、悬停提示、消息条、对话框、常用控件。所有页面只用这里的构件，保证风格统一。
import { icon } from './icons.js';

const SVG_NS = 'http://www.w3.org/2000/svg';
const tipFns = new WeakMap();

function append(el, kids) {
  for (const k of kids) {
    if (k == null || k === false || k === true) continue;
    if (Array.isArray(k)) append(el, k);
    else el.append(k instanceof Node ? k : document.createTextNode(String(k)));
  }
}

/** h('div', {class:'x', tip:'功能名', onClick}, ...子节点)。tip 可以是函数，悬停时才求值（例如带当前快捷键）。 */
export function h(tag, props, ...kids) {
  const el = tag === 'svg' || tag === 'path' || tag === 'circle' || tag === 'line' || tag === 'rect' || tag === 'g' || tag === 'text' || tag === 'polyline'
    ? document.createElementNS(SVG_NS, tag) : document.createElement(tag);
  if (props) {
    for (const [k, v] of Object.entries(props)) {
      if (v == null || v === false) continue;
      if (k === 'class') el.setAttribute('class', v);
      else if (k === 'style' && typeof v === 'object') for (const [sk, sv] of Object.entries(v)) sk.startsWith('--') ? el.style.setProperty(sk, sv) : (el.style[sk] = sv);
      else if (k === 'tip') {
        if (typeof v === 'function') { tipFns.set(el, v); el.setAttribute('aria-label', String(v()).split('\n')[0]); }
        else { el.dataset.tip = v; el.setAttribute('aria-label', String(v).split('\n')[0]); }
      } else if (k.length > 2 && k.startsWith('on') && typeof v === 'function') el.addEventListener(k[2].toLowerCase() + k.slice(3), v);
      else if (k === 'value' || k === 'checked' || k === 'disabled' || k === 'textContent' || k === 'htmlFor') el[k] = v;
      else el.setAttribute(k, v === true ? '' : v);
    }
  }
  append(el, kids);
  return el;
}

export const $ = (sel, root = document) => root.querySelector(sel);
export const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];
export function clear(el) { while (el.firstChild) el.firstChild.remove(); return el; }
export function setText(el, t) { if (el.textContent !== String(t)) el.textContent = t; }

// —— 悬停提示：整个应用只有一个提示节点，靠事件委托工作 ——
let tipEl, tipTimer, tipFor;
function tipText(el) {
  const fn = tipFns.get(el);
  return fn ? fn() : el.dataset.tip;
}
function showTip(el) {
  const text = tipText(el);
  if (!text) return;
  tipFor = el;
  clear(tipEl);
  const [first, ...rest] = String(text).split('\n');
  tipEl.append(first);
  for (const r of rest) tipEl.append(h('div', null, r));
  tipEl.classList.add('on');
  const r = el.getBoundingClientRect();
  const tw = tipEl.offsetWidth, th = tipEl.offsetHeight;
  let x = r.left + r.width / 2 - tw / 2;
  let y = r.bottom + 8;
  const nav = el.closest('.rail');
  if (nav) { x = r.right + 10; y = r.top + r.height / 2 - th / 2; }
  else if (y + th > innerHeight - 6) y = r.top - th - 8;
  tipEl.style.left = `${Math.max(6, Math.min(innerWidth - tw - 6, x))}px`;
  tipEl.style.top = `${Math.max(6, Math.min(innerHeight - th - 6, y))}px`;
}
function hideTip() {
  clearTimeout(tipTimer);
  tipFor = null;
  tipEl?.classList.remove('on');
}
export function initTips() {
  tipEl = h('div', { class: 'tip', role: 'tooltip' });
  document.body.append(tipEl);
  const find = (t) => t.closest?.('[data-tip]') || null;
  document.addEventListener('pointerover', (e) => {
    const el = find(e.target);
    if (!el || el === tipFor) return;
    clearTimeout(tipTimer);
    tipTimer = setTimeout(() => showTip(el), tipEl.classList.contains('on') ? 40 : 320);
  });
  document.addEventListener('pointerout', (e) => { if (!e.relatedTarget || !find(e.relatedTarget)) hideTip(); });
  document.addEventListener('pointerdown', hideTip, true);
  document.addEventListener('keydown', hideTip, true);
  document.addEventListener('scroll', hideTip, true);
  addEventListener('blur', hideTip);
  document.addEventListener('focusin', (e) => { const el = find(e.target); if (el && e.target.matches(':focus-visible')) showTip(el); });
  document.addEventListener('focusout', hideTip);
}

// —— 通用控件 ——
/** 带图标和可选文字的按钮；只有图标时必须给 tip。 */
export function btn({ icon: ic, label, tip, kind = '', onClick, disabled, big, cls = '' } = {}) {
  const b = h('button', { type: 'button', class: ['btn', kind, big ? 'big' : '', ic && !label ? 'icon' : '', cls].filter(Boolean).join(' '), tip, disabled, onClick: (e) => onClick && onClick(e, b) });
  if (ic) b.append(icon(ic));
  if (label) b.append(h('span', null, label));
  return b;
}

/** 异步按钮：点击期间转圈并禁用，结束后恢复；出错弹消息条。 */
export function asyncBtn(opts, fn) {
  const b = btn({ ...opts, onClick: async () => {
    if (b.classList.contains('busy')) return;
    b.classList.add('busy');
    b.disabled = true;
    try { await fn(b); } catch (e) { toast(e.message || String(e), 'danger'); } finally { b.classList.remove('busy'); b.disabled = false; }
  } });
  return b;
}

export function seg({ options, value, onChange, big }) {
  const root = h('div', { class: 'seg' + (big ? ' big' : ''), role: 'tablist' });
  const set = (v) => { for (const b of root.children) b.classList.toggle('on', b.dataset.v === String(v)); };
  for (const o of options) {
    root.append(h('button', { type: 'button', 'data-v': o.value, tip: o.tip, class: o.value === value ? 'on' : '', onClick: () => { set(o.value); onChange && onChange(o.value); } }, o.icon ? icon(o.icon) : null, o.label ? h('span', null, o.label) : null));
  }
  root.set = set;
  return root;
}

export function toggle({ value, onChange, tip, disabled }) {
  const s = h('button', { type: 'button', class: 'switch', role: 'switch', 'aria-checked': String(!!value), tip, disabled, onClick: () => { const v = s.getAttribute('aria-checked') !== 'true'; s.set(v); onChange && onChange(v); } });
  s.set = (v) => s.setAttribute('aria-checked', String(!!v));
  return s;
}

export function select({ options, value, onChange, cls = '' }) {
  const s = h('select', { class: 'select ' + cls });
  for (const o of options) s.append(h('option', { value: o.value, selected: o.value === value }, o.label));
  s.value = value;
  s.addEventListener('change', () => onChange && onChange(s.value));
  return s;
}

export function slider({ min, max, step = 1, value, onChange, fmt = (v) => v }) {
  const out = h('span', { style: { minWidth: '3.2em', textAlign: 'right' } }, fmt(value));
  const r = h('input', { type: 'range', min, max, step, value });
  const paint = () => r.style.setProperty('--fill', `${((r.value - min) / (max - min)) * 100}%`);
  paint();
  r.addEventListener('input', () => { paint(); out.textContent = fmt(+r.value); });
  r.addEventListener('change', () => onChange && onChange(+r.value));
  const root = h('div', { style: { display: 'flex', alignItems: 'center', gap: '10px', width: '220px' } }, r, out);
  root.set = (v) => { r.value = v; paint(); out.textContent = fmt(v); };
  return root;
}

/** 左标签右控件的一行。sub 是标签下方的小字（尽量别用，说明放 tip）。 */
export function row(label, control, { tip } = {}) {
  return h('div', { class: 'row', tip }, h('div', { class: 'k' }, label), h('div', { class: 'v' }, control));
}

export function panel({ label, acts, cls = '', body, flush, col } = {}) {
  const head = label != null || acts ? h('div', { class: 'head' }, h('div', { class: 'label' }, label ?? ''), acts ? h('div', { class: 'acts' }, acts) : null) : null;
  const b = h('div', { class: 'body' + (flush ? ' flush' : '') + (col ? ' col' : '') }, body);
  const p = h('section', { class: 'panel ' + cls }, head, b);
  p.body = b;
  p.head = head;
  return p;
}

export function kbd(chord) {
  return h('span', null, ...String(chord || '').split('+').filter(Boolean).flatMap((k, i) => [i ? ' ' : null, h('span', { class: 'kbd' }, prettyKey(k))]));
}
const KEY_NAMES = { ctrl: 'Ctrl', alt: 'Alt', shift: 'Shift', win: 'Win', enter: 'Enter', esc: 'Esc', space: 'Space', tab: 'Tab', back: 'Backspace', del: 'Del' };
export function prettyKey(k) {
  const l = k.toLowerCase();
  return KEY_NAMES[l] || (k.length === 1 ? k.toUpperCase() : k[0].toUpperCase() + k.slice(1));
}

export function chip(text, kind = '') { return h('span', { class: 'chip ' + kind }, text); }
export function dot(kind = '') { return h('span', { class: 'dot ' + kind }); }

export function empty(text, ic = 'info') { return h('div', { class: 'empty' }, icon(ic, 'lg'), h('div', null, text)); }

// —— 消息条 ——
let toastBox;
export function toast(text, kind = 'info', ms = 3600) {
  if (!toastBox) { toastBox = h('div', { class: 'toasts' }); document.body.append(toastBox); }
  const t = h('div', { class: 'toast ' + (kind === 'info' ? '' : kind) }, icon(kind === 'danger' ? 'fail' : kind === 'warn' ? 'alert' : kind === 'ok' ? 'ok' : 'info', 'sm'), h('span', null, text));
  toastBox.append(t);
  while (toastBox.children.length > 4) toastBox.firstChild.remove();
  setTimeout(() => { t.style.transition = 'opacity .2s'; t.style.opacity = '0'; setTimeout(() => t.remove(), 220); }, ms);
}

// —— 对话框（原生对话框在宿主里被禁用）——
export function dialog({ title, body, actions }) {
  return new Promise((resolve) => {
    const close = (v) => { scrim.remove(); document.removeEventListener('keydown', onKey, true); resolve(v); };
    const onKey = (e) => { if (e.key === 'Escape') { e.stopPropagation(); close(null); } };
    const bar = h('div', { class: 'ft' }, actions.map((a) => btn({ label: a.label, kind: a.kind, onClick: () => close(a.value) })));
    const scrim = h('div', { class: 'scrim', onClick: (e) => { if (e.target === scrim) close(null); } }, h('div', { class: 'dlg', role: 'dialog' }, title ? h('div', { class: 'hd' }, title) : null, h('div', { class: 'bd' }, body), bar));
    document.body.append(scrim);
    document.addEventListener('keydown', onKey, true);
    scrim.querySelector('.btn.primary, .btn')?.focus();
  });
}
export const confirmBox = (title, body, danger = false, ok = '确定') => dialog({ title, body, actions: [{ label: '取消', value: false }, { label: ok, kind: danger ? 'danger' : 'primary', value: true }] }).then((v) => !!v);

export async function copy(text, call) {
  try { await call('app.copy', { text }); toast('已复制', 'ok', 1400); } catch { try { await navigator.clipboard.writeText(text); toast('已复制', 'ok', 1400); } catch { toast('复制失败', 'warn'); } }
}
