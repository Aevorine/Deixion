// 界面语言：中文是源语言（代码里直接写中文），英文走 i18n/en.js 的词典。
// 设置里 language = auto | zh | en；auto 跟随系统界面语言：中文系统显示中文，其余一律显示英文。
import { EN } from '../i18n/en.js';

let lang = 'zh';
const warned = new Set();

const browserLang = () => ((navigator.language || '').toLowerCase().startsWith('zh') ? 'zh' : 'en');

/** 设置值 + 系统语言 → 实际语言。系统语言取不到时看浏览器语言。 */
export function resolveLang(setting, system) {
  if (setting === 'zh' || setting === 'en') return setting;
  return system === 'zh' || system === 'en' ? system : browserLang();
}

export function setLang(l) {
  lang = l === 'en' ? 'en' : 'zh';
  document.documentElement.lang = lang === 'en' ? 'en' : 'zh-CN';
}
export const getLang = () => lang;

/**
 * t('中文原文')：中文界面原样返回，英文界面查词典。
 * 参数用 {name} 占位：t('已选 {n} 项', { n })。
 * 英文词条里用 || 分隔单复数：'{n} client||{n} clients'，vars.n === 1 取前者。
 * 同一个中文词在不同场合要不同英文时，在前面加「场合¦」：t('鼠标¦按键')。中文界面只显示 ¦ 后面的部分。
 */
export function t(zh, vars) {
  const cut = zh.indexOf('¦');
  let s = cut >= 0 ? zh.slice(cut + 1) : zh;
  if (lang === 'en') {
    const hit = EN[zh];
    if (hit != null) s = hit;
    else if (/[㐀-鿿]/.test(zh) && !warned.has(zh)) { warned.add(zh); console.warn('[i18n] 缺英文词条：', zh); }
    const bar = s.indexOf('||');
    if (bar >= 0) s = vars && +vars.n === 1 ? s.slice(0, bar) : s.slice(bar + 2);
  }
  return vars ? s.replace(/\{(\w+)\}/g, (m, k) => (vars[k] == null ? m : String(vars[k]))) : s;
}
