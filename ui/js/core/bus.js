// 极简事件总线：模块之间只通过事件和函数签名通信。
const subs = new Map();
export const bus = {
  on(type, fn) {
    let s = subs.get(type);
    if (!s) subs.set(type, (s = new Set()));
    s.add(fn);
    return () => s.delete(fn);
  },
  emit(type, data) {
    const s = subs.get(type);
    if (!s) return;
    for (const fn of [...s]) {
      try { fn(data); } catch (e) { console.error(`[bus:${type}]`, e); }
    }
  },
};
