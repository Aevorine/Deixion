// 把 docs/wiki/*.md 同步到 GitHub Wiki（<仓库>.wiki.git）。docs/wiki 是唯一来源，Wiki 里不要直接改。
// 用法：node tools/sync-wiki.mjs [owner/repo]    默认 Aevorine/Deixion
// 注意：GitHub 要先在网页上手动建出 Wiki 的第一页（Wiki 标签 → Create the first page），之后才有 .wiki.git 仓库可以推送。
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const repo = process.argv[2] || 'Aevorine/Deixion';
const src = path.join(root, 'docs', 'wiki');
const url = `https://github.com/${repo}.wiki.git`;
const git = (args, cwd) => execFileSync('git', args, { cwd, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], env: { ...process.env, GIT_TERMINAL_PROMPT: '0' } });

const work = fs.mkdtempSync(path.join(os.tmpdir(), 'dx-wiki-'));
try {
  try { git(['clone', '--quiet', url, work], undefined); } catch (e) {
    console.error(`找不到 ${url}。请先在网页上建出 Wiki 的第一页：https://github.com/${repo}/wiki （Create the first page → Save），再重跑本脚本。`);
    process.exit(2);
  }
  for (const f of fs.readdirSync(work)) if (f.endsWith('.md')) fs.rmSync(path.join(work, f));
  for (const f of fs.readdirSync(src)) if (f.endsWith('.md')) fs.copyFileSync(path.join(src, f), path.join(work, f));
  git(['add', '-A'], work);
  if (!git(['status', '--porcelain'], work).trim()) { console.log('Wiki 已是最新。'); process.exit(0); }
  git(['-c', 'user.name=Aevorine', '-c', 'user.email=199806313+Aevorine@users.noreply.github.com', 'commit', '-m', 'docs: sync wiki from docs/wiki'], work);
  git(['push', '--quiet', 'origin', 'HEAD'], work);
  console.log(`已同步 ${fs.readdirSync(src).filter((f) => f.endsWith('.md')).length} 个页面到 ${url}`);
} finally {
  fs.rmSync(work, { recursive: true, force: true });
}
