// 网页 ⇄ 原生桥。原生里走 WebView2 消息，开发时（普通浏览器）走 /rpc。
import { bus } from './bus.js';
import { t } from './i18n.js';

const wv = window.chrome && window.chrome.webview;
export const native = !!wv;
const pending = new Map();
let seq = 0;
export const stats = { sent: 0 };

if (wv) {
  wv.addEventListener('message', (e) => {
    const m = e.data;
    if (!m) return;
    if (m.ev) { bus.emit('ev:' + m.ev, m.d); return; }
    const p = pending.get(m.id);
    if (!p) return;
    pending.delete(m.id);
    clearTimeout(p.timer);
    if (m.ok) p.res(m.r);
    else p.rej(Object.assign(new Error(m.e?.msg || t('调用失败')), { code: m.e?.code || 'internal' }));
  });
}

/** 调用原生能力：app.* / update.* / claude.* 与全部引擎方法（click、capture、settings.set …）。 */
export function call(method, params = {}, timeout = 60000) {
  stats.sent++;
  if (wv) {
    return new Promise((res, rej) => {
      const id = ++seq;
      const timer = setTimeout(() => { pending.delete(id); rej(Object.assign(new Error(t('调用超时')), { code: 'timeout' })); }, timeout);
      pending.set(id, { res, rej, timer });
      wv.postMessage({ id, m: method, p: params });
    });
  }
  return fetch('/rpc', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ m: method, p: params }) })
    .then((r) => r.json())
    .then((m) => {
      if (m.ok) return m.r;
      throw Object.assign(new Error(m.e?.msg || t('调用失败')), { code: m.e?.code || 'internal' });
    });
}
