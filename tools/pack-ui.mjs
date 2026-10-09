// 把 ui/ 目录打成一个二进制包，再生成 RCDATA 资源脚本，编译时嵌进 Deixion.exe。
// 用法：node tools/pack-ui.mjs <ui 目录> <输出 .pack> <输出 .rc>
import fs from 'node:fs';
import path from 'node:path';

const [, , uiDir, outPack, outRc] = process.argv;
if (!uiDir || !outPack || !outRc) {
  console.error('usage: node pack-ui.mjs <uiDir> <out.pack> <out.rc>');
  process.exit(2);
}

const files = [];
(function walk(dir) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p);
    else files.push(p);
  }
})(uiDir);
files.sort();

const entries = files.map((f) => ({ name: '/' + path.relative(uiDir, f).split(path.sep).join('/'), data: fs.readFileSync(f) }));
let tableSize = 8;
for (const e of entries) tableSize += 2 + Buffer.byteLength(e.name) + 8;
const header = Buffer.alloc(tableSize);
header.write('DXUP', 0, 'ascii');
header.writeUInt32LE(entries.length, 4);
let off = 8;
let dataOff = tableSize;
for (const e of entries) {
  const nb = Buffer.from(e.name, 'utf8');
  header.writeUInt16LE(nb.length, off);
  off += 2;
  nb.copy(header, off);
  off += nb.length;
  header.writeUInt32LE(dataOff, off);
  off += 4;
  header.writeUInt32LE(e.data.length, off);
  off += 4;
  dataOff += e.data.length;
}
fs.mkdirSync(path.dirname(outPack), { recursive: true });
fs.writeFileSync(outPack, Buffer.concat([header, ...entries.map((e) => e.data)]));
const rcPath = outPack.split(path.sep).join('/');
fs.writeFileSync(outRc, `UIPACK RCDATA "${rcPath}"\n`);
console.log(`ui pack: ${entries.length} files, ${(dataOff / 1024).toFixed(0)} KB`);
