// 端到端脚本的隔离模式：设 DX_E2E_ISOLATED=1，就把 deixion-cli 复制到项目 .scratch 下并放 portable.flag，
// 用 --inproc 在自己的进程里跑引擎。命名管道每个用户只有一个，不隔离的话脚本会连上已经在运行的 Deixion（比如已安装的旧版本），测的不是 build 里的新二进制；
// 隔离之后既不碰正在运行的 Deixion，也不碰真实的设置与数据。
import fs from 'node:fs';

export const isolated = process.env.DX_E2E_ISOLATED === '1';

export function cliPlan(root) {
  const src = process.env.DX_E2E_CLI || `${root}/build/deixion-cli.exe`; // DX_E2E_CLI：换成别的 deixion-cli.exe（比如旧版本）做对照
  if (!isolated) return { cmd: src, mcpArgs: ['mcp'], cleanup() {} };
  fs.mkdirSync(`${root}/.scratch`, { recursive: true });
  const dir = fs.mkdtempSync(`${root}/.scratch/e2e-`);
  const cmd = `${dir}/deixion-cli.exe`;
  fs.copyFileSync(src, cmd);
  fs.writeFileSync(`${dir}/portable.flag`, '');
  return { cmd, mcpArgs: ['--inproc', 'mcp'], cleanup() { fs.rmSync(dir, { recursive: true, force: true, maxRetries: 10, retryDelay: 150 }); } };
}
