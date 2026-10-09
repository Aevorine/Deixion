// 压力：对同一个编辑框反复 type replace，每次都用靶子自己写的状态文件核对文本是否精确。
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..').split(path.sep).join('/');
const stateFile = path.join(os.tmpdir(), 'dx-stress-state.json');
fs.rmSync(stateFile, { force: true });
const N = Number(process.argv[2] || 40);
const VIA = process.argv[3] || '';
const child = spawn(`${root}/build/deixion-cli.exe`, ['mcp'], { stdio: ['pipe', 'pipe', 'inherit'] });
let buf = '', id = 1; const pend = new Map();
child.stdout.on('data', (d) => { buf += d; let i; while ((i = buf.indexOf('\n')) >= 0) { const l = buf.slice(0, i).trim(); buf = buf.slice(i + 1); if (!l) continue; const m = JSON.parse(l); pend.get(m.id)?.(m); pend.delete(m.id); } });
const rpc = (method, params = {}) => new Promise((res, rej) => { const k = id++; const t = setTimeout(() => rej(new Error('timeout ' + method)), 20000); pend.set(k, (m) => { clearTimeout(t); res(m); }); child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id: k, method, params }) + '\n'); });
const call = async (name, args) => { const r = await rpc('tools/call', { name, arguments: args }); const t = r.result?.content?.find((x) => x.type === 'text')?.text ?? ''; try { return JSON.parse(t); } catch { return { raw: t }; } };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const state = () => { try { return JSON.parse(fs.readFileSync(stateFile, 'utf8')); } catch { return null; } };

await rpc('initialize', { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'stress', version: '1' } });
await call('launch', { path: `${root}/build/dx-testapp.exe`, args: `"${stateFile}"`, wait_window_ms: 4000 });
await sleep(400);
const W = 'DX Test Target';
let bad = 0; const strat = {}; const samples = [];
const MODE = process.argv[4] || 'type';
for (let i = 0; i < N; i++) {
  const want = `T${i} 你好 abc ${'x'.repeat(i % 7)}`;
  let r;
  if (MODE === 'chord') {
    await call('type', { window: W, find: { role: 'Edit' }, text: 'old content abc', replace: true, via: 'uia_set' });
    await call('click', { window: W, find: { role: 'Edit' } });
    r = await call('key', { window: W, keys: 'ctrl+a' });
    await call('type', { window: W, find: { role: 'Edit' }, text: want, via: 'msg_char' });
  } else {
    r = await call('type', { window: W, find: { role: 'Edit' }, text: want, replace: true, ...(VIA ? { via: VIA } : {}) });
  }
  await sleep(60);
  const got = state()?.edit;
  strat[r.strategy] = (strat[r.strategy] || 0) + 1;
  if (got !== want) { bad++; if (samples.length < 4) samples.push({ i, strategy: r.strategy, want, got }); }
}
console.log(`runs=${N} bad=${bad} strategies=${JSON.stringify(strat)}`);
for (const s of samples) console.log('  BAD', JSON.stringify(s));
await call('window_op', { window: W, op: 'close' });
child.stdin.end(); await sleep(200); child.kill();
process.exit(bad ? 1 : 0);
