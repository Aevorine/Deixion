// 发布辅助：
//   node tools/release.mjs pack [构建目录]   生成 dist/Deixion-Setup-x64.exe 与 dist/SHA256SUMS.txt
//   node tools/release.mjs prune [保留个数]  只保留最近 N 个 GitHub Release（默认 2），删除更早的及其 tag
// 资产名与校验和格式是应用内更新器的契约（src/app/updater.cpp），不要改。
import { execFileSync } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ASSET = 'Deixion-Setup-x64.exe';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const [, , cmd, arg] = process.argv;

function pack(buildDir = path.join(root, 'build')) {
  const src = path.join(buildDir, ASSET);
  if (!fs.existsSync(src)) throw new Error(`missing ${src}; build the project first`);
  const dist = path.join(root, 'dist');
  fs.rmSync(dist, { recursive: true, force: true });
  fs.mkdirSync(dist, { recursive: true });
  fs.copyFileSync(src, path.join(dist, ASSET));
  const hash = crypto.createHash('sha256').update(fs.readFileSync(src)).digest('hex');
  fs.writeFileSync(path.join(dist, 'SHA256SUMS.txt'), `${hash}  ${ASSET}\n`);
  console.log(`dist/${ASSET}  ${(fs.statSync(src).size / 1048576).toFixed(2)} MB\nsha256 ${hash}`);
}

function prune(keep = 2) {
  const out = execFileSync('gh', ['release', 'list', '--limit', '100', '--json', 'tagName,publishedAt,isDraft'], { encoding: 'utf8' });
  const rel = JSON.parse(out).filter((r) => !r.isDraft).sort((a, b) => b.publishedAt.localeCompare(a.publishedAt));
  const drop = rel.slice(keep);
  for (const r of drop) {
    execFileSync('gh', ['release', 'delete', r.tagName, '--yes', '--cleanup-tag'], { stdio: 'inherit' });
    console.log(`deleted ${r.tagName}`);
  }
  console.log(`kept ${rel.slice(0, keep).map((r) => r.tagName).join(', ') || '(none)'}; deleted ${drop.length}`);
}

if (cmd === 'pack') pack(arg);
else if (cmd === 'prune') prune(arg ? Number(arg) : 2);
else {
  console.error('usage: node tools/release.mjs pack [buildDir] | prune [keep]');
  process.exit(2);
}
