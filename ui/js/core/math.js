// 公式一律用 KaTeX 渲染；脚本按需加载，只有用到公式的页面才付出这笔开销。
let loading;
export function ensureKatex() {
  if (window.katex) return Promise.resolve(window.katex);
  if (!loading) {
    loading = new Promise((res, rej) => {
      const s = document.createElement('script');
      s.src = 'vendor/katex/katex.min.js';
      s.onload = () => res(window.katex);
      s.onerror = () => rej(new Error('KaTeX 加载失败'));
      document.head.append(s);
    });
  }
  return loading;
}

/** 返回一个会在 KaTeX 就绪后填入公式的节点。 */
export function tex(src, { block = false } = {}) {
  const el = document.createElement(block ? 'div' : 'span');
  el.className = 'math' + (block ? ' block' : '');
  el.textContent = src;
  ensureKatex().then((k) => { k.render(src, el, { throwOnError: false, displayMode: block, output: 'html' }); }).catch(() => {});
  return el;
}
