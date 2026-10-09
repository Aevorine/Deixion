// 全局状态：设置、引擎状态、动作流、趋势序列。页面只读这里，改动一律走 patchSettings / 调用原生。
import { bus } from './bus.js';
import { call, stats } from './bridge.js';
import { applyTheme } from './theme.js';
import { toast } from './dom.js';

const SERIES_LEN = 150;
export const state = {
  ready: false,
  settings: null,
  defaults: null,
  info: null,
  status: null,
  perf: null,
  update: null,
  claude: null,
  hotkeys: [],
  actions: [],
  series: { ext: [], cpu: [], mem: [] },
};

const push = (arr, v) => { arr.push(v); if (arr.length > SERIES_LEN) arr.shift(); };

export function fromJournal(e) {
  return { id: e.id, ts: e.ts, method: e.method, app: e.app, title: e.title, strategy: e.strategy, ok: e.ok, confirmed: e.confirmed, us: e.us, err: e.err, undoable: e.inv?.kind && e.inv.kind !== 'none' && !e.undone, undone: e.undone, params: e.params };
}

export async function refreshActions(limit = 120) {
  const r = await call('journal', { limit });
  state.actions = (r.entries || []).map(fromJournal).sort((a, b) => b.ts - a.ts);
  bus.emit('actions');
}

export async function patchSettings(patch) {
  const before = state.settings;
  state.settings = { ...state.settings, ...patch };
  bus.emit('settings', state.settings);
  applyTheme(state.settings);
  try {
    const r = await call('settings.set', { patch });
    state.settings = r.settings;
  } catch (e) {
    state.settings = before;
    toast(e.message, 'danger');
  }
  bus.emit('settings', state.settings);
  applyTheme(state.settings);
}

/** 悬停提示 + 当前绑定的快捷键，随设置里的改动实时变化。 */
export const tipKey = (text, name) => () => {
  const c = state.hotkeys.find((k) => k.name === name)?.chord;
  return c ? `${text}\n${c.split('+').map((k) => (k.length === 1 ? k.toUpperCase() : k[0].toUpperCase() + k.slice(1))).join(' + ')}` : text;
};

let prev = null;
export async function poll() {
  try {
    const [st, pf] = await Promise.all([call('status'), call('perf')]);
    const now = performance.now();
    state.status = st;
    state.perf = pf;
    if (prev) {
      const dt = (now - prev.t) / 1000;
      const self = stats.sent - prev.sent;
      const ext = Math.max(0, (st.calls - prev.calls - self) / dt);
      push(state.series.ext, ext);
      const cpuPct = ((pf.process.cpu_ms - prev.cpu) / (dt * 1000) / Math.max(1, st.cpu?.threads || 1)) * 100;
      push(state.series.cpu, Math.max(0, cpuPct));
      push(state.series.mem, pf.process.working_set / 1048576);
    }
    prev = { t: now, calls: st.calls, sent: stats.sent, cpu: pf.process.cpu_ms };
    bus.emit('status', st);
  } catch { /* 下一拍再试 */ }
}

let timer = 0;
function schedule() {
  clearTimeout(timer);
  timer = setTimeout(async () => { if (!document.hidden) await poll(); schedule(); }, 2000);
}

export async function init() {
  const [sg, info] = await Promise.all([call('settings.get'), call('app.info').catch(() => null)]);
  state.settings = sg.settings;
  state.defaults = sg.defaults;
  state.info = info;
  state.hotkeys = info?.hotkeys || [];
  state.update = info?.update || null;
  applyTheme(state.settings);
  await Promise.all([poll(), refreshActions().catch(() => {})]);
  state.ready = true;
  schedule();
  document.addEventListener('visibilitychange', () => { if (!document.hidden) poll(); });

  bus.on('ev:action', (ev) => {
    state.actions.unshift({ ...ev, undoable: !!ev.undoable, undone: false });
    if (state.actions.length > 300) state.actions.length = 300;
    bus.emit('action', ev);
  });
  bus.on('ev:settings', (s) => { state.settings = s; applyTheme(s); bus.emit('settings', s); });
  bus.on('ev:hotkeys', (h) => { state.hotkeys = h; bus.emit('hotkeys', h); });
  bus.on('ev:update', (u) => { state.update = u; bus.emit('update', u); });
  bus.on('ev:toast', (t) => toast(t.text, t.kind));
  bus.on('ev:shown', () => poll());
  bus.emit('ready');
}
