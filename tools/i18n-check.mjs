// 多语言完整性检查：扫 ui/js 里的中文字符串字面量，要求英文词典里都有对应词条，且占位符一致。
// 用法：node tools/i18n-check.mjs        （有问题时退出码 1）
// 某一行故意保留中文（例如注释以外的示例数据）时，在行尾加  // i18n-ignore
// 加 --unwrapped：另列出没有被 t(...) 直接包住的中文字面量（常量表里的合法，其余多半是漏包），供人工过一遍。
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const jsDir = path.join(root, 'ui', 'js');
const CJK = /[㐀-鿿＀-￯　-〿]/;
const CJK_CH = /[㐀-鿿]/;

/** 取出源码里的字符串字面量（跳过注释与正则），模板字符串里 ${} 的部分用 {} 占位。 */
function literals(src) {
  const out = [];
  let i = 0, line = 1, prev = '';
  const n = src.length;
  const startsRegex = () => prev === '' || '(,=:[!&|?{};+-*%<>~^'.includes(prev);
  while (i < n) {
    const c = src[i];
    if (c === '\n') { line++; i++; continue; }
    if (c === '/' && src[i + 1] === '/') { while (i < n && src[i] !== '\n') i++; continue; }
    if (c === '/' && src[i + 1] === '*') { i += 2; while (i < n && !(src[i] === '*' && src[i + 1] === '/')) { if (src[i] === '\n') line++; i++; } i += 2; continue; }
    if (c === '/' && startsRegex()) {
      i++;
      let cls = false;
      while (i < n && (src[i] !== '/' || cls) && src[i] !== '\n') { if (src[i] === '\\') i++; else if (src[i] === '[') cls = true; else if (src[i] === ']') cls = false; i++; }
      i++;
      while (/[a-z]/i.test(src[i] || '')) i++;
      prev = ')';
      continue;
    }
    if (c === '"' || c === "'") {
      const at = line;
      let j = i + 1, s = '';
      while (j < n && src[j] !== c && src[j] !== '\n') { if (src[j] === '\\') { s += src[j + 1]; j += 2; } else s += src[j++]; }
      out.push({ text: s, line: at, kind: 'str', callee: callee(src, i) });
      i = j + 1; prev = c; continue;
    }
    if (c === '`') {
      const at = line;
      let j = i + 1, s = '', interp = false;
      while (j < n && src[j] !== '`') {
        if (src[j] === '\\') { s += src[j + 1]; j += 2; continue; }
        if (src[j] === '$' && src[j + 1] === '{') {
          interp = true;
          let d = 1; j += 2; let inner = '';
          while (j < n && d) { if (src[j] === '{') d++; else if (src[j] === '}') { d--; if (!d) break; } if (src[j] === '\n') line++; inner += src[j++]; }
          s += '{' + inner.replace(/\s+/g, ' ').slice(0, 30) + '}'; j++; continue;
        }
        if (src[j] === '\n') line++;
        s += src[j++];
      }
      out.push({ text: s, line: at, kind: interp ? 'tpl' : 'str', callee: callee(src, i) });
      i = j + 1; prev = '`'; continue;
    }
    if (!/\s/.test(c)) prev = c;
    i++;
  }
  return out;
}

/** 字面量前面紧挨着的是不是 t( 调用。 */
function callee(src, i) {
  let j = i - 1;
  while (j >= 0 && /\s/.test(src[j])) j--;
  if (src[j] !== '(') return '';
  j--;
  while (j >= 0 && /\s/.test(src[j])) j--;
  let e = j + 1;
  while (j >= 0 && /[\w$.]/.test(src[j])) j--;
  return src.slice(j + 1, e);
}

const files = [];
(function walk(d) {
  for (const e of fs.readdirSync(d, { withFileTypes: true })) {
    const p = path.join(d, e.name);
    if (e.isDirectory()) { if (e.name !== 'i18n') walk(p); } else if (e.name.endsWith('.js') && e.name !== 'i18n.js') files.push(p);
  }
})(jsDir);

const { EN, SOURCES } = await import(pathToFileURL(path.join(jsDir, 'i18n', 'en.js')).href);
const problems = [];
const unwrapped = [];
const used = new Set();
const rel = (f) => path.relative(root, f).split(path.sep).join('/');
const vars = (s) => [...new Set([...String(s).matchAll(/\{(\w+)\}/g)].map((m) => m[1]))].sort().join(',');

for (const f of files) {
  const src = fs.readFileSync(f, 'utf8');
  const lines = src.split(/\r?\n/);
  for (const lit of literals(src)) {
    if (!CJK_CH.test(lit.text)) continue;
    if (/i18n-ignore/.test(lines[lit.line - 1] || '')) continue;
    const where = `${rel(f)}:${lit.line}`;
    if (lit.kind === 'tpl') { problems.push(`${where}  模板字符串里有中文，改成 t('…{x}…', { x })：${lit.text.slice(0, 40)}`); continue; }
    used.add(lit.text);
    if (lit.callee !== 't') unwrapped.push(`${where}  ${lit.text.slice(0, 40)}`);
    if (!(lit.text in EN)) problems.push(`${where}  缺英文词条：${lit.text.slice(0, 50)}`);
    else if (vars(lit.text) !== vars(EN[lit.text])) problems.push(`${where}  占位符不一致：${lit.text.slice(0, 40)} → ${EN[lit.text].slice(0, 40)}`);
    else if (CJK_CH.test(EN[lit.text])) problems.push(`${where}  英文词条里还有中文：${EN[lit.text].slice(0, 50)}`);
  }
}

const seen = new Map();
for (const [name, dict] of Object.entries(SOURCES)) {
  for (const [k, v] of Object.entries(dict)) {
    if (seen.has(k) && seen.get(k).v !== v) problems.push(`词典冲突：「${k.slice(0, 30)}」在 ${seen.get(k).name} 与 ${name} 里译法不同`);
    seen.set(k, { v, name });
    if (!used.has(k)) problems.push(`词典里多余（代码里没用到）：${name}：${k.slice(0, 40)}`);
    if (typeof v !== 'string' || !v.trim()) problems.push(`英文为空：${name}：${k.slice(0, 40)}`);
  }
}

if (process.argv.includes('--unwrapped')) console.log(`没有被 t() 直接包住的中文字面量 ${unwrapped.length} 处：\n${unwrapped.join('\n')}\n`);
if (problems.length) {
  console.log(problems.join('\n'));
  console.log(`\n共 ${problems.length} 个问题（扫描 ${files.length} 个文件，词条 ${Object.keys(EN).length} 条）`);
  process.exit(1);
}
console.log(`i18n 检查通过：${files.length} 个文件，词条 ${Object.keys(EN).length} 条，中文字面量 ${used.size} 种`);
