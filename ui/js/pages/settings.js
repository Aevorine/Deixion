// 设置：运行方式、截图与日志、外观、系统、快捷键、更新。改动即时保存，原生一侧立刻生效。
import { h, panel, btn, asyncBtn, seg, toggle, select, slider, row, chip, kbd, toast, confirmBox, dialog, copy } from '../core/dom.js';
import { icon } from '../core/icons.js';
import { bus } from '../core/bus.js';
import { call } from '../core/bridge.js';
import { state, patchSettings } from '../core/store.js';
import { registerPage } from '../core/registry.js';
import * as fmt from '../core/fmt.js';

const HOTKEYS = [
  ['toggle', '显示 / 隐藏窗口', 'eye'],
  ['mode', '切换前台 / 后台', 'eyeoff'],
  ['pause', '暂停 / 继续接收操作', 'pause'],
  ['undo', '撤销上一步', 'undo'],
  ['shot', '截图到剪贴板', 'camera'],
  ['stop', '紧急停止', 'stop'],
];
const CODE_NAMES = { Space: 'space', Enter: 'enter', Tab: 'tab', Escape: 'esc', Backspace: 'backspace', Delete: 'delete', Insert: 'insert', Home: 'home', End: 'end', PageUp: 'pageup', PageDown: 'pagedown', ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right', Backquote: 'backtick', Minus: 'minus', Equal: 'plus', Comma: 'comma', Period: 'period', Slash: 'slash', Backslash: 'backslash', Semicolon: 'semicolon', Quote: 'quote', BracketLeft: 'lbracket', BracketRight: 'rbracket' };

/** 把按键事件转成引擎认识的组合键写法；只有修饰键、或没有修饰键时返回 null。 */
function chordFromEvent(e) {
  let key = null;
  if (/^Key[A-Z]$/.test(e.code)) key = e.code.slice(3).toLowerCase();
  else if (/^Digit\d$/.test(e.code)) key = e.code.slice(5);
  else if (/^F\d{1,2}$/.test(e.code)) key = e.code.toLowerCase();
  else if (CODE_NAMES[e.code]) key = CODE_NAMES[e.code];
  if (!key) return null;
  const mods = [e.ctrlKey && 'ctrl', e.altKey && 'alt', e.shiftKey && 'shift', e.metaKey && 'win'].filter(Boolean);
  return mods.length ? [...mods, key].join('+') : null;
}

registerPage({
  id: 'settings', icon: 'settings', tip: '设置', order: 910, area: 'foot',
  render(root) {
    const s = () => state.settings;
    const ctl = {};
    const reg = (name, c) => { ctl[name] = c; return c; };

    const run = panel({
      label: '运行', body: h('div', { class: 'rows' },
        row('模式', reg('mode', seg({ value: s().mode, options: [{ value: 'background', icon: 'eyeoff', label: '后台', tip: '全程隐藏，不抢焦点、不动光标' }, { value: 'foreground', icon: 'eye', label: '前台', tip: '显示光标轨迹与操作过程' }], onChange: (v) => patchSettings({ mode: v }) }))),
        row('暂停接收操作', reg('paused', toggle({ value: s().paused, onChange: (v) => patchSettings({ paused: v }) }))),
        row('允许短暂切前台', reg('allow_hop', toggle({ value: s().allow_hop, tip: '后台通道都失败时，才允许短暂切到前台完成操作', onChange: (v) => patchSettings({ allow_hop: v }) }))),
        row('允许启动命令行与脚本', reg('allow_shell_launch', toggle({ value: s().allow_shell_launch, tip: '默认禁止模型用“启动程序”打开 cmd、PowerShell、脚本宿主等；只有你能在这里打开', onChange: (v) => patchSettings({ allow_shell_launch: v }) }))),
        row('只允许启动清单内的程序', reg('launch_strict', toggle({ value: s().launch_strict, tip: '开启后“启动程序”只能打开右边清单里的程序，其余一律拒绝', onChange: (v) => patchSettings({ launch_strict: v }) }))),
        row('允许启动的程序', btn({ icon: 'list', label: '编辑', onClick: editAllow })),
        row('前台显示轨迹', reg('overlay', toggle({ value: s().overlay, onChange: (v) => patchSettings({ overlay: v }) }))),
        row('速度', reg('speed', seg({ value: s().speed, options: [{ value: 'instant', label: '瞬时' }, { value: 'fast', label: '快速' }, { value: 'smooth', label: '平滑' }], onChange: (v) => patchSettings({ speed: v }) }))),
        row('操作后验证', reg('verify', toggle({ value: s().verify !== 'off', tip: '用窗口事件确认操作是否生效', onChange: (v) => patchSettings({ verify: v ? 'auto' : 'off' }) })))),
    });

    const shot = panel({
      label: '截图与日志', body: h('div', { class: 'rows' },
        row('JPEG 质量', reg('jpeg_quality', slider({ min: 30, max: 100, value: s().jpeg_quality, onChange: (v) => patchSettings({ jpeg_quality: v }), fmt: (v) => `${v}` }))),
        row('截图最长边（像素）', reg('max_image_dim', slider({ min: 400, max: 4096, step: 16, value: s().max_image_dim, onChange: (v) => patchSettings({ max_image_dim: v }), fmt: (v) => `${v}` }))),
        row('截图默认带网格', reg('grid_default', toggle({ value: s().grid_default, onChange: (v) => patchSettings({ grid_default: v }) }))),
        row('日志级别', reg('log_level', select({ value: s().log_level, options: [{ value: 'debug', label: '调试' }, { value: 'info', label: '信息' }, { value: 'warn', label: '警告' }, { value: 'error', label: '错误' }], onChange: (v) => patchSettings({ log_level: v }) })))),
    });

    const look = panel({
      label: '外观与系统', body: h('div', { class: 'rows' },
        row('主题', reg('theme', seg({ value: s().theme, options: [{ value: 'auto', icon: 'contrast', label: '跟随系统' }, { value: 'light', icon: 'sun', label: '浅色' }, { value: 'dark', icon: 'moon', label: '深色' }], onChange: (v) => patchSettings({ theme: v }) }))),
        row('密度', reg('density', seg({ value: s().density, options: [{ value: 'compact', label: '紧凑' }, { value: 'standard', label: '标准' }, { value: 'relaxed', label: '宽松' }], onChange: (v) => patchSettings({ density: v }) }))),
        row('开机自启（托盘）', reg('autostart', toggle({ value: s().autostart, onChange: (v) => patchSettings({ autostart: v }) }))),
        row('关闭窗口时留在托盘', reg('close_to_tray', toggle({ value: s().close_to_tray, tip: '关闭后 Claude Code 仍可调用；从托盘菜单退出才会完全停止', onChange: (v) => patchSettings({ close_to_tray: v }) }))),
        row('自动检查更新', reg('check_updates', toggle({ value: s().check_updates, onChange: (v) => patchSettings({ check_updates: v }) }))),
        row('数据目录', btn({ icon: 'folder', label: '打开', onClick: () => call('app.open', { kind: 'data' }).catch((e) => toast(e.message, 'warn')) })),
        row('配置', h('span', { style: { display: 'inline-flex', gap: 'var(--sp-2)' } },
          asyncBtn({ icon: 'copy', label: '导出' }, () => copy(JSON.stringify(s(), null, 2), call)),
          btn({ icon: 'upload', label: '导入', onClick: importSettings })))),
    });

    async function editAllow() {
      const ta = h('textarea', { class: 'input code-in', rows: 12, spellcheck: 'false', placeholder: 'notepad.exe' });
      ta.value = (s().launch_allow || []).join('\n');
      const ok = await dialog({ title: '允许启动的程序', body: ta, actions: [{ label: '取消', value: false }, { label: '保存', kind: 'primary', value: true }] });
      if (!ok) return;
      try { await patchSettings({ launch_allow: ta.value.split(/\r?\n/).map((x) => x.trim()).filter(Boolean) }); toast('已保存', 'ok'); } catch (e) { toast(`保存失败：${e.message}`, 'danger'); }
    }

    async function importSettings() {
      const ta = h('textarea', { class: 'input code-in', rows: 12, spellcheck: 'false', placeholder: '粘贴导出的配置 JSON' });
      const ok = await dialog({ title: '导入配置', body: ta, actions: [{ label: '取消', value: false }, { label: '导入', kind: 'primary', value: true }] });
      if (!ok) return;
      try { await patchSettings(JSON.parse(ta.value)); toast('配置已导入', 'ok'); } catch (e) { toast(`导入失败：${e.message}`, 'danger'); }
    }

    // —— 快捷键 ——
    const hkRows = h('div', { class: 'rows' });
    let recording = null;
    function paintHotkeys() {
      hkRows.replaceChildren();
      for (const [name, label, ic] of HOTKEYS) {
        const chord = s().hotkeys?.[name] || '';
        const st = state.hotkeys.find((k) => k.name === name);
        const dflt = state.defaults?.hotkeys?.[name] || '';
        const rec = h('button', { type: 'button', class: 'btn' + (recording === name ? ' on' : ''), tip: '点击后按下新的组合键（需包含 Ctrl / Alt / Shift / Win 之一）', onClick: () => { recording = recording === name ? null : name; paintHotkeys(); } }, icon('keyboard', 'sm'), recording === name ? '请按下组合键…（Esc 取消）' : '录制');
        hkRows.append(h('div', { class: 'row' },
          h('div', { class: 'k', style: { display: 'flex', alignItems: 'center', gap: '10px' } }, icon(ic, 'sm'), label),
          h('div', { class: 'v' },
            st && !st.ok && st.chord ? chip('被其它程序占用', 'danger') : st && st.ok ? chip('已生效', 'ok') : null,
            kbd(chord), rec,
            btn({ icon: 'undo', tip: `恢复默认（${dflt}）`, kind: 'ghost', disabled: chord === dflt, onClick: () => patchSettings({ hotkeys: { ...s().hotkeys, [name]: dflt } }) }))));
      }
    }
    const onKey = (e) => {
      if (!recording) return;
      e.preventDefault();
      e.stopPropagation();
      if (e.key === 'Escape') { recording = null; paintHotkeys(); return; }
      const c = chordFromEvent(e);
      if (!c) return;
      const name = recording;
      recording = null;
      patchSettings({ hotkeys: { ...s().hotkeys, [name]: c } }).then(paintHotkeys);
      paintHotkeys();
    };
    addEventListener('keydown', onKey, true);
    bus.on('hotkeys', paintHotkeys);
    bus.on('settings', paintHotkeys);
    const keys = panel({ label: '全局快捷键', body: hkRows });

    // —— 更新与关于 ——
    const upBody = h('div', { class: 'rows' });
    const upActs = h('div', { style: { display: 'flex', gap: 'var(--sp-2)' } });
    function paintUpdate() {
      const u = state.update || { state: 'idle', current: state.status?.version };
      upBody.replaceChildren();
      const lab = { idle: '未检查', checking: '检查中…', current: '已是最新', available: `发现新版本 ${u.latest}`, downloading: '下载中…', ready: '已下载，可安装', error: '出错' }[u.state] || u.state;
      const kind = { current: 'ok', available: 'warn', ready: 'warn', error: 'danger', downloading: 'accent', checking: 'accent' }[u.state] || '';
      upBody.append(row('当前版本', h('span', null, `v${u.current || state.status?.version || ''}`, ' ', u.portable ? chip('便携版') : null)), row('状态', chip(lab, kind)));
      if (u.state === 'downloading' && u.total) upBody.append(row('进度', h('div', { class: 'bar', style: { width: '200px', '--w': `${((u.got / u.total) * 100).toFixed(0)}%` } }, h('i'))));
      if (u.error) upBody.append(row('错误', h('span', { style: { color: 'var(--c-danger)' } }, u.error)));
      const i = state.info || {};
      upBody.append(row('WebView2', h('span', null, i.webview || '—')), row('程序位置', h('span', { class: 'ell', tip: i.exe || '' }, i.exe || '—')), row('数据目录', h('span', { class: 'ell', tip: state.status?.data_dir || '' }, state.status?.data_dir || '—')));
      upBody.append(row('仓库', h('a', { href: '#', onClick: (e) => { e.preventDefault(); call('app.open', { kind: 'url', target: `https://github.com/${u.repo || 'Aevorine/Deixion'}` }); } }, u.repo || 'Aevorine/Deixion')));
      upActs.replaceChildren(...[
        asyncBtn({ icon: 'refresh', label: '检查更新' }, async () => { await call('update.check'); }),
        u.state === 'available' && !u.portable ? asyncBtn({ icon: 'download', label: '下载', kind: 'primary' }, async () => { await call('update.download'); }) : null,
        u.state === 'ready' ? asyncBtn({ icon: 'power', label: '安装并重启', kind: 'primary' }, async () => { if (await confirmBox('安装更新', 'Deixion 会退出并在安装完成后自动重启。', false, '安装')) await call('update.apply'); }) : null,
        (u.state === 'available' && u.portable) || u.state === 'ready' ? btn({ icon: 'external', label: '发布页', onClick: () => call('app.open', { kind: 'url', target: u.page || `https://github.com/${u.repo}/releases` }) }) : null].filter(Boolean));
    }
    bus.on('update', paintUpdate);
    const about = panel({ label: '更新与关于', body: upBody, acts: [upActs] });

    const syncAll = () => {
      const x = s();
      ctl.mode.set(x.mode); ctl.paused.set(x.paused); ctl.allow_hop.set(x.allow_hop); ctl.allow_shell_launch.set(x.allow_shell_launch); ctl.launch_strict.set(x.launch_strict); ctl.overlay.set(x.overlay); ctl.speed.set(x.speed);
      ctl.verify.set(x.verify !== 'off'); ctl.jpeg_quality.set(x.jpeg_quality); ctl.max_image_dim.set(x.max_image_dim); ctl.grid_default.set(x.grid_default);
      ctl.log_level.value = x.log_level; ctl.theme.set(x.theme); ctl.density.set(x.density); ctl.autostart.set(x.autostart); ctl.close_to_tray.set(x.close_to_tray); ctl.check_updates.set(x.check_updates);
    };
    bus.on('settings', syncAll);

    root.append(h('div', { class: 'grid', style: { gridTemplateRows: 'auto minmax(0, 1fr)' } },
      h('div', { class: 's4' }, run), h('div', { class: 's4' }, shot), h('div', { class: 's4' }, look),
      h('div', { class: 's8' }, keys), h('div', { class: 's4' }, about)));
    for (const c of root.querySelectorAll('.grid > .s4, .grid > .s8')) { c.style.display = 'flex'; c.firstChild.style.flex = '1'; }
    paintHotkeys();
    paintUpdate();
    return { show() { paintUpdate(); syncAll(); }, hide() { recording = null; } };
  },
});
