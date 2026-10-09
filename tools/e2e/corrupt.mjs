// 用法：node corrupt.mjs <in.exe> <out.exe> <条目名>  —— 翻转该条目压缩数据中间的一个字节
import fs from 'node:fs';
const [, , inp, out, want] = process.argv;
const b = fs.readFileSync(inp);
let o = -1, i = -1;
while ((i = b.indexOf('DXPL', i + 1)) >= 0) if (b.readUInt32LE(i + 4) === 1 && b.readUInt32LE(i + 8) < 64) o = i;
const n = b.readUInt32LE(o + 8); let p = o + 12; const rows = [];
for (let k = 0; k < n; k++) { const nl = b.readUInt16LE(p); const name = b.toString('utf8', p + 2, p + 2 + nl); p += 2 + nl; rows.push({ name, packed: b.readUInt32LE(p + 4), off: b.readUInt32LE(p + 12) }); p += 16; }
const r = rows.find((x) => x.name === want);
const at = p + r.off + (r.packed >> 1);
b[at] ^= 0xff;
fs.writeFileSync(out, b);
console.log(`flipped byte @${at} inside ${want}`);
