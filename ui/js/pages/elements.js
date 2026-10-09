// 元素：一次缓存快照读出窗口的全部控件，模糊搜索、在截图上定位、直接对控件操作。
import { h, panel, btn, asyncBtn, select, toggle, chip, empty, toast, copy } from '../core/dom.js';
import { icon } from '../core/icons.js';
import { call } from '../core/bridge.js';
import { registerPage } from '../core/registry.js';
import { vlist } from '../core/vlist.js';
import { shotView } from '../core/shotview.js';
import { debounce } from '../core/fmt.js';
import * as fmt from '../core/fmt.js';
import { t } from '../core/i18n.js';

const ROLES = ['', 'Button', 'Edit', 'CheckBox', 'RadioButton', 'ComboBox', 'ListItem', 'MenuItem', 'TabItem', 'TreeItem', 'Hyperlink', 'Text', 'Document'];

registerPage({
  id: 'elements', icon: 'elements', tip: '元素', order: 30,
  render(root) {
    let target = '';
    let nodes = [];
    let sel = null;
    let loading = false;
    let picked = false;

    const winSel = select({ options: [{ value: '', label: t('选择窗口…') }], value: '', onChange: (v) => { target = v; reload(true); } });
    winSel.style.width = '240px';
    const q = h('input', { class: 'input', placeholder: t('模糊搜索名称 / 标识'), style: { width: '100%' } });
    const search = h('div', { class: 'search', style: { width: '230px' } }, icon('search', 'sm'), q);
    const roleSel = select({ options: ROLES.map((r) => ({ value: r, label: r || t('全部类型') })), value: '', onChange: () => reload(false) });
    const inter = toggle({ value: true, tip: t('只显示可交互 / 有名称的控件'), onChange: () => reload(false) });
    const count = chip('0');
    const view = shotView();
    const overlay = view.layer;

    const list = vlist({
      rowH: 36,
      onRow: (n, el) => { el.addEventListener('click', () => select_(n)); el.classList.toggle('sel', sel?.id === n.id); },
      render: (n) => [
        h('span', { style: { width: '3.2em', color: 'var(--c-text-3)' } }, n.id),
        h('span', { class: 'chip accent', style: { minWidth: '5.6em', justifyContent: 'center' } }, n.role),
        h('span', { style: { flex: 1, minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis' } }, n.name || h('span', { style: { color: 'var(--c-text-3)' } }, n.aid || t('（无名称）'))),
        h('span', { style: { color: 'var(--c-text-3)', width: '9.5em', textAlign: 'right', fontVariantNumeric: 'tabular-nums' } }, n.at),
        ...(n.state || []).map((s) => chip(s, s === 'disabled' ? 'warn' : '')),
      ],
    });

    const detail = h('div', { class: 'rows' });
    const actRow = h('div', { style: { display: 'flex', gap: 'var(--sp-2)', flexWrap: 'wrap', alignItems: 'center' } });

    function drawBoxes() {
      overlay.replaceChildren();
      for (const n of nodes.slice(0, 400)) {
        if (!n.box) continue;
        const [a, b, c, d] = n.box;
        const bx = h('i', { class: 'eb' + (sel?.id === n.id ? ' sel' : ''), style: { left: `${a * 100}%`, top: `${b * 100}%`, width: `${Math.max(0.2, (c - a) * 100)}%`, height: `${Math.max(0.2, (d - b) * 100)}%` }, 'data-id': n.id });
        bx.addEventListener('click', (e) => { e.stopPropagation(); select_(n); });
        overlay.append(bx);
      }
    }

    function select_(n) {
      sel = n;
      drawBoxes();
      list.set(nodes);
      detail.replaceChildren();
      actRow.replaceChildren();
      if (!n) { detail.append(empty(t('选择一个控件'), 'elements')); return; }
      const line = (k, v) => h('div', { class: 'row' }, h('div', { class: 'k' }, k), h('div', { class: 'v' }, v));
      detail.append(
        line(t('编号'), n.id), line(t('类型'), chip(n.role, 'accent')), line(t('名称'), n.name || '—'), line(t('标识'), n.aid || '—'),
        line(t('中心 λ,φ'), n.at), line(t('范围'), n.box ? n.box.map((v) => v.toFixed(3)).join(' · ') : '—'),
        line(t('Meridian 码'), h('span', { style: { fontWeight: 'bold', letterSpacing: '0.08em' } }, n.code)),
        line(t('可执行'), (n.can || []).length ? h('span', { style: { display: 'inline-flex', gap: '4px', flexWrap: 'wrap', justifyContent: 'flex-end' } }, n.can.map((c) => chip(c))) : '—'),
        line(t('深度'), n.depth ?? '—'),
      );
      const canValue = (n.can || []).includes('value');
      const val = h('input', { class: 'input', placeholder: t('要写入的值'), style: { width: '170px', display: canValue ? '' : 'none' } });
      actRow.append(
        asyncBtn({ icon: 'actions', label: t('点击'), kind: 'primary', disabled: (n.state || []).includes('disabled') }, async () => {
          const r = await call('click', { window: target, element: n.id });
          toast(t('已点击') + ' · ' + r.strategy + (r.confirmed ? ' · ' + t('已确认') : ''), 'ok');
        }),
        val,
        canValue ? asyncBtn({ icon: 'type', label: t('赋值') }, async () => {
          const r = await call('set_value', { window: target, element: n.id, value: val.value });
          toast(t('已赋值') + ' · ' + r.strategy, 'ok');
        }) : null,
        btn({ icon: 'copy', tip: t('复制选择器（给 Claude 用）'), kind: 'ghost', onClick: () => copy(JSON.stringify({ window: target, element: n.id, role: n.role, name: n.name }), call) }),
      );
    }

    async function loadWindows() {
      try {
        const r = await call('windows', {});
        const cur = winSel.value;
        winSel.replaceChildren(h('option', { value: '' }, t('选择窗口…')), ...(r.windows || []).filter((w) => !w.minimized && w.client.w > 0).map((w) => h('option', { value: `hwnd:${w.hwnd}` }, `${w.exe} · ${(w.title || w.class).slice(0, 38)}`)));
        winSel.value = [...winSel.options].some((o) => o.value === cur) ? cur : '';
        if (!winSel.value && !picked) {
          picked = true;
          const w = (r.windows || []).find((x) => !x.minimized && x.client.w > 200 && x.exe.toLowerCase() !== 'deixion.exe');
          if (w) { winSel.value = `hwnd:${w.hwnd}`; target = winSel.value; reload(true); }
        }
      } catch (e) { toast(e.message, 'danger'); }
    }

    async function reload(withShot) {
      if (!target || loading) return;
      loading = true;
      refresh.classList.add('busy');
      try {
        const params = { window: target, limit: 600, interactive: inter.getAttribute('aria-checked') === 'true' };
        if (q.value.trim()) params.query = q.value.trim();
        if (roleSel.value) params.role = roleSel.value;
        const [r, s] = await Promise.all([call('elements', params), withShot || view.shot.style.display === 'none' ? call('capture', { window: target, grid: false, max_dim: 1200 }) : null]);
        nodes = r.nodes || [];
        count.textContent = `${r.count} / ${r.total}${r.truncated ? '+' : ''} · ${fmt.us(r.us)}`;
        if (s) view.set(s.image.mime, s.image.b64, s.image.w, s.image.h);
        sel = sel && nodes.find((n) => n.id === sel.id) || null;
        select_(sel);
      } catch (e) { toast(e.message, 'danger'); } finally { loading = false; refresh.classList.remove('busy'); }
    }
    const refresh = btn({ icon: 'refresh', tip: t('重新读取元素与截图'), kind: 'ghost', onClick: () => { loadWindows(); reload(true); } });
    q.addEventListener('input', debounce(() => reload(false), 220));

    const left = panel({
      cls: 's7', flush: true,
      label: [count],
      acts: [winSel, search, roleSel, h('span', { style: { display: 'inline-flex', alignItems: 'center', gap: '6px' } }, icon('list', 'sm'), inter), refresh],
      body: list.root,
    });
    const right = h('div', { class: 's5', style: { display: 'grid', gridTemplateRows: 'minmax(0, 1.25fr) minmax(0, 1fr)', gap: 'var(--sp-3)', minHeight: 0 } },
      panel({ label: t('窗口预览'), flush: true, body: view.root }),
      panel({ label: t('控件详情'), body: [detail], acts: [actRow] }));
    root.append(h('div', { class: 'grid', style: { gridTemplateRows: 'minmax(0, 1fr)' } }, left, right));
    select_(null);
    loadWindows();
    return { show: loadWindows };
  },
});
