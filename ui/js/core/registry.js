// 页面登记表：新增页面只需在 pages/ 下加一个文件并调用 registerPage。
const pages = [];
/**
 * registerPage({ id, icon, tip, order, area, render(root) → { show?, hide? } | void })
 * area: 'main'（上部图标栏）或 'foot'（下部图标栏）。
 */
export function registerPage(def) {
  if (pages.some((p) => p.id === def.id)) throw new Error('页面 id 重复：' + def.id); // i18n-ignore
  pages.push({ area: 'main', order: 100, ...def });
}
export const getPages = () => [...pages].sort((a, b) => a.order - b.order);
