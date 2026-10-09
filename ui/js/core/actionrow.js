// 动作行：总览的实时动作流与操作页的历史共用同一种显示。
import { h, asyncBtn, toast } from './dom.js';
import { icon } from './icons.js';
import { call } from './bridge.js';
import { refreshActions } from './store.js';
import { methodInfo, strategyName } from './meta.js';
import * as fmt from './fmt.js';

export function actionCells(a) {
  const m = methodInfo(a.method);
  const stateIcon = a.ok ? (a.confirmed ? 'ok' : 'check') : 'fail';
  const stateTip = a.ok ? (a.confirmed ? '已确认生效' : '已执行（未能确认效果）') : `失败：${a.err || ''}`;
  const undo = a.undoable
    ? asyncBtn({ icon: 'undo', tip: '撤销这一步', kind: 'ghost' }, async () => {
      await call('rollback', { id: a.id });
      toast('已撤销', 'ok', 1500);
      await refreshActions();
    })
    : null;
  return [
    h('span', { style: { color: 'var(--c-text-3)', width: '4.8em' } }, fmt.time(a.ts)),
    h('span', { class: 'chip accent', style: { minWidth: '5em' }, tip: m.label }, icon(m.icon, 'sm'), m.label),
    h('span', { style: { flex: 1, minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis' }, tip: a.title || a.app }, a.app || '—', a.title ? h('span', { style: { color: 'var(--c-text-3)' } }, ` · ${a.title}`) : null),
    h('span', { class: 'chip' }, strategyName(a.strategy)),
    h('span', { style: { width: '5.6em', textAlign: 'right', fontVariantNumeric: 'tabular-nums' } }, fmt.us(a.us)),
    h('span', { style: { color: a.ok ? 'var(--c-ok)' : 'var(--c-danger)', display: 'inline-flex' }, tip: stateTip }, icon(stateIcon, 'sm')),
    undo || h('span', { style: { width: 'var(--control-h)' } }),
  ];
}
