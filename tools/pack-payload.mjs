// 把安装文件逐个 DEFLATE 压缩并附 CRC32，打成一个包，再生成 RCDATA 资源脚本，编译时嵌进 deixion-setup.exe。
// 用法：node tools/pack-payload.mjs <输出 .bin> <输出 .rc> <相对名=源文件> ...
// 包格式：'DXPL' u32 版本 u32 条数 | 每条 u16 名长 名(utf8) u32 原长 u32 压缩长 u32 crc u32 偏移 | 数据区
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';

const [, , outBin, outRc, ...specs] = process.argv;
if (!outBin || !outRc || specs.length === 0) {
  console.error('usage: node pack-payload.mjs <out.bin> <out.rc> <name=source> ...');
  process.exit(2);
}

const entries = [];
for (const spec of specs) {
  const i = spec.indexOf('=');
  if (i < 1) throw new Error(`bad spec: ${spec}`);
  const name = spec.slice(0, i).replaceAll('\\', '/');
  const src = spec.slice(i + 1);
  if (!fs.existsSync(src)) throw new Error(`missing payload file: ${src}`);
  const raw = fs.readFileSync(src);
  const packed = zlib.deflateRawSync(raw, { level: 9 });
  entries.push({ name, raw, packed: packed.length < raw.length ? packed : raw, stored: packed.length >= raw.length, crc: zlib.crc32(raw) >>> 0 });
}
entries.sort((a, b) => a.name.localeCompare(b.name));

let table = 12;
for (const e of entries) table += 2 + Buffer.byteLength(e.name) + 16;
const head = Buffer.alloc(table);
head.write('DXPL', 0, 'ascii');
head.writeUInt32LE(1, 4);
head.writeUInt32LE(entries.length, 8);
let off = 12;
let data = 0;
for (const e of entries) {
  const nb = Buffer.from(e.name, 'utf8');
  head.writeUInt16LE(nb.length, off); off += 2;
  nb.copy(head, off); off += nb.length;
  head.writeUInt32LE(e.raw.length, off); off += 4;
  head.writeUInt32LE(e.packed.length, off); off += 4;
  head.writeUInt32LE(e.crc, off); off += 4;
  head.writeUInt32LE(data, off); off += 4;
  data += e.packed.length;
}

fs.mkdirSync(path.dirname(outBin), { recursive: true });
fs.writeFileSync(outBin, Buffer.concat([head, ...entries.map((e) => e.packed)]));
fs.writeFileSync(outRc, `PAYLOAD RCDATA "${outBin.split(path.sep).join('/')}"\n`);
const rawTotal = entries.reduce((n, e) => n + e.raw.length, 0);
console.log(`payload: ${entries.length} files, ${(rawTotal / 1024).toFixed(0)} KB -> ${((head.length + data) / 1024).toFixed(0)} KB`);
