// 定位：把任意窗口或屏幕映射成 [0,1]² 的经纬平面。悬停读坐标，点击钉点，逐级放大，直接在该点操作。
import { h, panel, btn, asyncBtn, select, seg, toggle, chip, empty, toast, copy } from '../core/dom.js';
import { icon } from '../core/icons.js';
import { call } from '../core/bridge.js';
import { state } from '../core/store.js';
import { registerPage } from '../core/registry.js';
import { codeOf, decode, MAX_LEVEL, cellSize } from '../core/geo.js';
import { tex } from '../core/math.js';
import { throttleFrame } from '../core/fmt.js';

registerPage({
  id: 'locate', icon: 'locate', tip: '定位', order: 20,
  render(root) {
    let target = 'screen';
    let frame = null; // 当前目标在屏幕上的矩形
    let region = { a: { lam: 0, phi: 0 }, b: { lam: 1, phi: 1 } };
    let trail = []; // 放大轨迹（Meridian 码）
    let pin = null;
    let hover = null;
    let imgRatio = 16 / 9;
    let live = false;
    let liveTimer = 0;
    let wins = [];

    const winSel = select({ options: [{ value: 'screen', label: '整个屏幕' }], value: 'screen', onChange: (v) => { target = v; trail = []; region = { a: { lam: 0, phi: 0 }, b: { lam: 1, phi: 1 } }; pin = null; shoot(); } });
    winSel.style.width = '260px';
    const gridSeg = seg({ value: '10', options: ['5', '10', '20'].map((v) => ({ value: v, label: v })), onChange: () => paintGrid() });
    gridSeg.setAttribute('data-tip', '网格密度');
    const img = h('img', { class: 'shot-img', alt: '', draggable: 'false' });
    const gridEl = h('div', { class: 'shot-grid' });
    const cross = h('div', { class: 'shot-cross' }, h('i', { class: 'h' }), h('i', { class: 'v' }));
    const pinEl = h('div', { class: 'shot-pin' }, icon('pin'));
    const tag = h('div', { class: 'shot-tag' });
    const shot = h('div', { class: 'shot' }, img, gridEl, cross, pinEl, tag);
    const stage = h('div', { class: 'shot-stage' }, shot);
    const emptyEl = h('div', { class: 'shot-empty' }, empty('选择窗口后点击“截图”', 'camera'));
    stage.append(emptyEl);

    const fit = () => {
      const r = stage.getBoundingClientRect();
      const w = Math.min(r.width, r.height * imgRatio);
      shot.style.width = `${Math.max(0, w)}px`;
      shot.style.height = `${Math.max(0, w / imgRatio)}px`;
    };
    new ResizeObserver(fit).observe(stage);

    const toGlobal = (u, v) => ({ lam: region.a.lam + u * (region.b.lam - region.a.lam), phi: region.a.phi + v * (region.b.phi - region.a.phi) });
    const toLocal = (g) => ({ u: (g.lam - region.a.lam) / (region.b.lam - region.a.lam), v: (g.phi - region.a.phi) / (region.b.phi - region.a.phi) });

    function paintGrid() {
      const n = +gridSeg.querySelector('.on')?.dataset.v || 10;
      gridEl.style.setProperty('--gx', `${100 / n}%`);
      gridEl.style.setProperty('--gy', `${100 / n}%`);
      gridEl.replaceChildren();
      for (let i = 0; i <= n; i += n > 10 ? 4 : 2) {
        const g = toGlobal(i / n, i / n);
        gridEl.append(h('span', { class: 'gx', style: { left: `${(i / n) * 100}%` } }, g.lam.toFixed(2)), h('span', { class: 'gy', style: { top: `${(i / n) * 100}%` } }, g.phi.toFixed(2)));
      }
    }

    const readout = h('div', { class: 'locate-read' });
    const infoBody = h('div', { class: 'rows' });
    const elemBody = h('div', { class: 'rows' });
    const trailEl = h('div', { class: 'tabs' });

    function paintTrail() {
      trailEl.replaceChildren(h('button', { class: trail.length ? '' : 'on', onClick: () => zoomTo([]) }, '全部'), ...trail.map((c, i) => h('button', { class: i === trail.length - 1 ? 'on' : '', onClick: () => zoomTo(trail.slice(0, i + 1)) }, c)));
    }

    function paintPin() {
      if (!pin) { pinEl.style.display = 'none'; return; }
      const l = toLocal(pin);
      pinEl.style.display = l.u < 0 || l.u > 1 || l.v < 0 || l.v > 1 ? 'none' : '';
      pinEl.style.left = `${l.u * 100}%`;
      pinEl.style.top = `${l.v * 100}%`;
    }

    function paintInfo() {
      infoBody.replaceChildren();
      if (!pin || !frame) { infoBody.append(empty('点击图像钉一个点', 'pin')); return; }
      const px = { x: Math.min(frame.w - 1, Math.floor(pin.lam * frame.w)), y: Math.min(frame.h - 1, Math.floor(pin.phi * frame.h)) };
      const copyBtn = (t) => btn({ icon: 'copy', tip: '复制', kind: 'ghost', onClick: () => copy(t, call) });
      const line = (k, v, c) => h('div', { class: 'row' }, h('div', { class: 'k' }, k), h('div', { class: 'v' }, h('span', null, v), c ? copyBtn(c) : null));
      infoBody.append(
        line('λ 横向', pin.lam.toFixed(5), pin.lam.toFixed(5)),
        line('φ 纵向', pin.phi.toFixed(5), pin.phi.toFixed(5)),
        line('窗口内像素', `${px.x}, ${px.y}`, `${px.x},${px.y}`),
        line('屏幕像素', `${frame.x + px.x}, ${frame.y + px.y}`, `${frame.x + px.x},${frame.y + px.y}`),
      );
      for (let lv = 1; lv <= MAX_LEVEL; lv++) {
        const code = codeOf(pin.lam, pin.phi, lv);
        const cs = cellSize(lv);
        infoBody.append(h('div', { class: 'row' },
          h('div', { class: 'k' }, h('span', { class: 'chip accent' }, `${lv} 级`), ' ', h('span', { style: { fontWeight: 'bold', letterSpacing: '0.08em' } }, code)),
          h('div', { class: 'v' }, h('span', { style: { color: 'var(--c-text-3)' } }, `${(cs.w * frame.w).toFixed(0)}×${(cs.h * frame.h).toFixed(0)} px`),
            btn({ icon: 'zoomIn', tip: `放大到 ${code} 格`, kind: 'ghost', onClick: () => zoomTo([code]) }), copyBtn(code))));
      }
    }

    async function probe() {
      elemBody.replaceChildren();
      if (!pin || target === 'screen') { elemBody.append(empty(target === 'screen' ? '选择具体窗口后显示该点的控件' : '—', 'elements')); return; }
      try {
        const r = await call('locate', { window: target, at: `${pin.lam},${pin.phi}` });
        const e = r.interactive || r.element;
        if (!e) { elemBody.append(empty('该点没有可识别的控件', 'elements')); return; }
        const line = (k, v) => h('div', { class: 'row' }, h('div', { class: 'k' }, k), h('div', { class: 'v' }, v));
        elemBody.append(line('类型', chip(e.role, 'accent')), line('名称', e.name || '—'), line('标识', e.aid || '—'), line('编号', e.id), line('可执行', (e.can || []).join(' · ') || '—'), line('状态', (e.state || []).join(' · ') || '正常'));
      } catch (err) { elemBody.append(empty(err.message, 'alert')); }
    }

    async function loadWindows() {
      try {
        const r = await call('windows', {});
        wins = r.windows || [];
        const cur = winSel.value;
        winSel.replaceChildren(h('option', { value: 'screen' }, '整个屏幕'), ...wins.filter((w) => !w.minimized && w.client.w > 0).map((w) => h('option', { value: `hwnd:${w.hwnd}` }, `${w.exe} · ${w.title.slice(0, 40) || w.class}`)));
        winSel.value = [...winSel.options].some((o) => o.value === cur) ? cur : 'screen';
      } catch (e) { toast(e.message, 'danger'); }
    }

    async function shoot() {
      const params = { window: target === 'screen' ? 'screen' : target, grid: false, max_dim: 1800 };
      if (trail.length) params.code = trail.at(-1);
      const r = await call('capture', params);
      img.src = `data:${r.image.mime};base64,${r.image.b64}`;
      imgRatio = r.image.w / r.image.h;
      frame = r.frame;
      region = { a: { lam: +r.region.a.split(',')[0], phi: +r.region.a.split(',')[1] }, b: { lam: +r.region.b.split(',')[0], phi: +r.region.b.split(',')[1] } };
      emptyEl.style.display = 'none';
      shot.style.display = '';
      fit();
      paintGrid();
      paintPin();
      paintTrail();
      paintInfo();
    }
    shot.style.display = 'none';

    function zoomTo(codes) {
      trail = codes;
      shoot().catch((e) => toast(e.message, 'danger'));
    }

    const hoverMove = throttleFrame((e) => {
      const r = shot.getBoundingClientRect();
      const u = (e.clientX - r.left) / r.width, v = (e.clientY - r.top) / r.height;
      if (u < 0 || u > 1 || v < 0 || v > 1) return;
      hover = toGlobal(u, v);
      cross.style.display = 'block';
      cross.style.setProperty('--x', `${u * 100}%`);
      cross.style.setProperty('--y', `${v * 100}%`);
      tag.style.display = 'block';
      tag.style.left = `${Math.min(u * 100, 74)}%`;
      tag.style.top = `${Math.min(v * 100 + 2, 92)}%`;
      tag.textContent = `λ ${hover.lam.toFixed(4)}  φ ${hover.phi.toFixed(4)}`;
    });
    shot.addEventListener('pointermove', hoverMove);
    shot.addEventListener('pointerleave', () => { cross.style.display = 'none'; tag.style.display = 'none'; });
    shot.addEventListener('click', (e) => {
      const r = shot.getBoundingClientRect();
      pin = toGlobal((e.clientX - r.left) / r.width, (e.clientY - r.top) / r.height);
      paintPin();
      paintInfo();
      probe();
    });

    const doClick = (opts, tip) => asyncBtn({ icon: opts.icon, label: opts.label, tip, kind: opts.kind }, async () => {
      if (!pin) throw new Error('先在图像上点一个点');
      if (target === 'screen' && state.settings.mode !== 'foreground') toast('屏幕目标会落到该点下方的真实窗口', 'info', 2000);
      const r = await call('click', { window: target, at: `${pin.lam.toFixed(5)},${pin.phi.toFixed(5)}`, ...opts.params });
      toast(`已${opts.label} · ${r.strategy || ''}${r.confirmed ? ' · 已确认' : ''}`, 'ok');
    });

    const livePulse = () => { clearTimeout(liveTimer); if (live && !document.hidden) shoot().catch(() => {}).finally(() => { liveTimer = setTimeout(livePulse, 700); }); else if (live) liveTimer = setTimeout(livePulse, 700); };
    const liveSw = toggle({ value: false, tip: '实时刷新（约 1 帧/秒）', onChange: (v) => { live = v; if (v) livePulse(); else clearTimeout(liveTimer); } });

    const left = panel({
      cls: 's8', flush: true,
      label: '截图',
      acts: [winSel, btn({ icon: 'refresh', tip: '刷新窗口列表', kind: 'ghost', onClick: loadWindows }), gridSeg, h('span', { style: { display: 'inline-flex', alignItems: 'center', gap: '6px' } }, icon('clock', 'sm'), liveSw), asyncBtn({ icon: 'camera', label: '截图', kind: 'primary' }, () => shoot())],
      body: [stage, h('div', { class: 'shot-foot' }, trailEl)],
    });
    left.body.classList.add('shot-body');

    const maths = panel({ label: '映射', body: [tex('\\lambda=\\dfrac{x-x_0}{w},\\quad \\varphi=\\dfrac{y-y_0}{h}', { block: true }), tex('M(\\lambda,\\varphi)=\\mathrm{spread}(\\lfloor 2^{16}\\lambda\\rfloor)\\ll 1\\ \\lor\\ \\mathrm{spread}(\\lfloor 2^{16}\\varphi\\rfloor)', { block: true })] });
    const right = h('div', { class: 's4', style: { display: 'grid', gridTemplateRows: 'minmax(0, 1.4fr) minmax(0, 0.8fr) auto', gap: 'var(--sp-3)', minHeight: 0 } },
      panel({ label: '钉点坐标', body: infoBody, acts: [doClick({ icon: 'actions', label: '点击', params: {} }, '在钉点点击'), doClick({ icon: 'actions', label: '双击', params: { count: 2 } }, '在钉点双击'), doClick({ icon: 'actions', label: '右键', params: { button: 'right' } }, '在钉点右键')] }),
      panel({ label: '钉点处的控件', body: elemBody }),
      maths);

    root.append(h('div', { class: 'grid', style: { gridTemplateRows: 'minmax(0, 1fr)' } }, left, right));
    paintTrail();
    paintInfo();
    probe();
    loadWindows();
    shoot().catch(() => {});
    return {
      show() { loadWindows(); if (live) livePulse(); },
      hide() { clearTimeout(liveTimer); },
    };
  },
});
