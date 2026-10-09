// 操作：手动发一条操作或一批操作，看结果；下方是完整历史，每一步都能撤销。
import { h, panel, btn, asyncBtn, select, seg, chip, empty, toast, copy, confirmBox } from '../core/dom.js';
import { icon } from '../core/icons.js';
import { bus } from '../core/bus.js';
import { call } from '../core/bridge.js';
import { state, refreshActions } from '../core/store.js';
import { registerPage } from '../core/registry.js';
import { vlist } from '../core/vlist.js';
import { actionCells } from '../core/actionrow.js';
import { methodInfo } from '../core/meta.js';
import * as fmt from '../core/fmt.js';

const FORMS = {
  click: ['point', 'button', 'count'],
  type: ['point', 'text'],
  set_value: ['point', 'value'],
  key: ['keys'],
  scroll: ['point', 'dy'],
  drag: ['from', 'to'],
  window: ['op'],
  launch: ['path', 'args'],
  wait: ['for', 'timeout_ms'],
};
const LABELS = { point: '落点', button: '按键', count: '次数', text: '文字', value: '值', keys: '按键组合', dy: '滚动量', from: '起点', to: '终点', op: '窗口操作', path: '程序路径', args: '参数', for: '等待', timeout_ms: '超时(毫秒)' };
const PLACE = { point: '0.42,0.31 · e12 · 确定 · code:K7Q', from: '0.2,0.5', to: '0.8,0.5', keys: 'ctrl+a', text: '要输入的内容', value: '要写入的值', dy: '-3（向下）', path: 'notepad.exe', args: '', timeout_ms: '5000' };

/** 落点写法 → 引擎参数：0.4,0.3 经纬 · e12 控件编号 · code:K7Q 码 · px:120,80 像素 · 其它文字按名称查找。 */
function pointParams(s, key = '') {
  s = s.trim();
  if (!s) return {};
  const put = (o) => (key ? { [key]: o } : o);
  if (/^e\d+$/.test(s)) return key ? { [key]: { element: s } } : { element: s };
  if (/^code:/i.test(s)) return key ? { [key]: { code: s.slice(5) } } : { code: s.slice(5) };
  if (/^px:/i.test(s)) { const [x, y] = s.slice(3).split(',').map(Number); return put({ px: { x, y } }); }
  if (/^-?\d*\.?\d+\s*,\s*-?\d*\.?\d+$/.test(s)) return key ? { [key]: { at: s.replace(/\s/g, '') } } : { at: s.replace(/\s/g, '') };
  return key ? { [key]: { find: { text: s } } } : { find: { text: s } };
}

registerPage({
  id: 'actions', icon: 'actions', tip: '操作', order: 40,
  render(root) {
    let tab = 'single';
    let selected = null;
    let filter = 'all';

    const method = select({ options: Object.keys(FORMS).map((m) => ({ value: m, label: methodInfo(m).label + ' · ' + m })), value: 'click', onChange: () => paintForm() });
    method.style.width = '100%';
    const win = select({ options: [{ value: '', label: '（不指定，用屏幕坐标）' }], value: '' });
    win.style.width = '100%';
    const fields = h('div', { class: 'form' });
    const req = h('pre', { class: 'code selectable' });
    const res = h('pre', { class: 'code selectable' });
    const resMeta = h('span', { class: 'chip' }, '尚未执行');
    const batch = h('textarea', { class: 'input code-in', rows: 10, spellcheck: 'false', value: JSON.stringify({ stop_on_error: true, defaults: { window: 'title:记事本' }, steps: [{ do: 'click', find: { text: '文件' } }, { do: 'wait', for: 'settle' }] }, null, 2) });
    const vals = {};

    const input = (name) => {
      if (name === 'button') return (vals[name] = select({ options: [{ value: 'left', label: '左键' }, { value: 'right', label: '右键' }, { value: 'middle', label: '中键' }], value: 'left', onChange: preview }));
      if (name === 'count') return (vals[name] = select({ options: [{ value: '1', label: '单击' }, { value: '2', label: '双击' }, { value: '3', label: '三击' }], value: '1', onChange: preview }));
      if (name === 'op') return (vals[name] = select({ options: ['focus', 'minimize', 'maximize', 'restore', 'close', 'topmost'].map((v) => ({ value: v, label: v })), value: 'focus', onChange: preview }));
      if (name === 'for') return (vals[name] = select({ options: [{ value: 'settle', label: '界面安静' }, { value: 'window', label: '窗口出现' }, { value: 'element', label: '控件出现' }, { value: 'gone', label: '控件消失' }], value: 'settle', onChange: preview }));
      const i = h('input', { class: 'input', placeholder: PLACE[name] || '', style: { width: '100%' } });
      i.addEventListener('input', preview);
      return (vals[name] = i);
    };

    function build() {
      const m = method.value;
      const p = {};
      if (win.value) p.window = win.value;
      for (const f of FORMS[m]) {
        const v = vals[f]?.value ?? '';
        if (v === '') continue;
        if (f === 'point') Object.assign(p, pointParams(v));
        else if (f === 'from' || f === 'to') Object.assign(p, pointParams(v, f));
        else if (f === 'count' || f === 'dy' || f === 'timeout_ms') p[f] = +v;
        else if (f === 'args') p.args = v.split(/\s+/);
        else if (f === 'button' && v === 'left') continue;
        else p[f] = v;
      }
      return { m, p };
    }
    function preview() {
      const { m, p } = build();
      req.textContent = `${m} ${JSON.stringify(p, null, 2)}`;
    }
    function paintForm() {
      fields.replaceChildren();
      for (const k of Object.keys(vals)) delete vals[k];
      for (const f of FORMS[method.value]) fields.append(h('div', { class: 'row' }, h('div', { class: 'k' }, LABELS[f]), h('div', { class: 'v', style: { flex: 1 } }, input(f))));
      preview();
    }
    win.addEventListener('change', preview);

    async function loadWindows() {
      try {
        const r = await call('windows', {});
        const cur = win.value;
        win.replaceChildren(h('option', { value: '' }, '（不指定，用屏幕坐标）'), h('option', { value: 'active' }, '前台窗口'), ...(r.windows || []).filter((w) => !w.minimized && w.client.w > 0).map((w) => h('option', { value: `hwnd:${w.hwnd}` }, `${w.exe} · ${(w.title || w.class).slice(0, 36)}`)));
        win.value = [...win.options].some((o) => o.value === cur) ? cur : '';
        preview();
      } catch { /* 窗口列表失败不影响手动输入 */ }
    }

    const show = (r, ms, ok) => {
      res.textContent = JSON.stringify(r, null, 2);
      resMeta.className = 'chip ' + (ok ? 'ok' : 'danger');
      resMeta.textContent = `${ok ? '成功' : '失败'} · ${ms.toFixed(1)} ms`;
    };
    const run = asyncBtn({ icon: 'play', label: '执行', kind: 'primary' }, async () => {
      const t0 = performance.now();
      try {
        let r;
        if (tab === 'single') { const { m, p } = build(); r = await call(m, p); } else r = await call('batch', JSON.parse(batch.value));
        show(r, performance.now() - t0, r.ok !== false);
        await refreshActions();
      } catch (e) { show({ error: e.message, code: e.code }, performance.now() - t0, false); }
    });

    const tabs = seg({ value: 'single', options: [{ value: 'single', label: '单步' }, { value: 'batch', label: '批量' }], onChange: (v) => { tab = v; single.style.display = v === 'single' ? '' : 'none'; multi.style.display = v === 'batch' ? '' : 'none'; } });
    const single = h('div', { class: 'col-gap' }, h('div', { class: 'row' }, h('div', { class: 'k' }, '方法'), h('div', { class: 'v', style: { flex: 1 } }, method)), h('div', { class: 'row' }, h('div', { class: 'k' }, '目标窗口'), h('div', { class: 'v', style: { flex: 1 } }, win)), fields, h('div', { class: 'cap' }, '请求'), req);
    const multi = h('div', { class: 'col-gap', style: { display: 'none' } }, batch);
    const composer = panel({ cls: 'compose', label: '发送', acts: [tabs, btn({ icon: 'refresh', tip: '刷新窗口列表', kind: 'ghost', onClick: loadWindows })], body: [single, multi], col: true });
    const result = panel({ label: '结果', acts: [resMeta, btn({ icon: 'copy', tip: '复制结果', kind: 'ghost', onClick: () => copy(res.textContent, call) })], body: res });

    const list = vlist({ rowH: 38, render: actionCells, onRow: (a, el) => { el.addEventListener('click', () => { selected = a; detail.textContent = JSON.stringify(a, null, 2); [...el.parentNode.children].forEach((c) => c.classList.toggle('sel', c === el)); }); } });
    const detail = h('pre', { class: 'code selectable' }, '选择一条历史查看细节');
    const none = empty('还没有操作记录', 'history');
    const total = chip('');
    const filterSeg = seg({ value: 'all', options: [{ value: 'all', label: '全部' }, { value: 'undo', label: '可撤销' }, { value: 'fail', label: '失败' }], onChange: (v) => { filter = v; paint(); } });
    const paint = () => {
      const items = state.actions.filter((a) => (filter === 'undo' ? a.undoable : filter === 'fail' ? !a.ok : true));
      list.set(items);
      total.textContent = `${items.length} / ${state.actions.length}`;
      none.style.display = items.length ? 'none' : '';
      list.root.style.display = items.length ? '' : 'none';
    };
    bus.on('action', paint);
    bus.on('actions', paint);
    const history = panel({
      cls: 'hist', flush: true, label: [total],
      acts: [filterSeg,
        asyncBtn({ icon: 'undo', label: '撤销上一步', kind: '' }, async () => { const r = await call('rollback', { count: 1 }); toast(`已撤销 ${r.undone ?? 1} 步`, 'ok'); await refreshActions(); }),
        asyncBtn({ icon: 'trash', tip: '清空历史（撤销信息也会清除）', kind: 'ghost' }, async () => { if (await confirmBox('清空历史', '清空后无法再撤销这些操作，确定吗？', true, '清空')) { await call('journal.clear'); await refreshActions(); toast('已清空', 'ok'); } })],
      body: [list.root, none],
    });
    const detailPanel = panel({ label: '细节', body: detail, acts: [btn({ icon: 'copy', tip: '复制细节', kind: 'ghost', onClick: () => copy(detail.textContent, call) })] });

    root.append(h('div', { class: 'grid', style: { gridTemplateRows: 'minmax(0, 1fr)' } },
      h('div', { class: 's5 stack', style: { gridTemplateRows: 'minmax(0, 1.5fr) minmax(0, 1fr)' } }, composer, result),
      h('div', { class: 's7 stack', style: { gridTemplateRows: 'minmax(0, 1.5fr) minmax(0, 1fr)' } }, history, detailPanel)));
    paintForm();
    paint();
    loadWindows();
    return { show() { loadWindows(); refreshActions(); } };
  },
});
