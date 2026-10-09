// 接入：让 Claude Code 能调用 Deixion——注册 MCP 服务器、安装技能，状态一目了然。
import { h, panel, btn, asyncBtn, chip, dot, toast, copy, confirmBox } from '../core/dom.js';
import { icon } from '../core/icons.js';
import { call } from '../core/bridge.js';
import { registerPage } from '../core/registry.js';
import { TOOLS } from '../core/meta.js';

registerPage({
  id: 'claude', icon: 'claude', tip: '接入 Claude Code', order: 80,
  render(root) {
    let st = null;
    const rows = h('div', { class: 'rows', style: { minHeight: '300px' } });
    const cfgCmd = h('pre', { class: 'code selectable' });
    const cfgJson = h('pre', { class: 'code selectable' });
    const clients = h('div', { class: 'kpi' });

    const item = (ic, name, status, kind, detail, action) => h('div', { class: 'link-row' },
      h('div', { class: 'ico ' + kind }, icon(ic)),
      h('div', { class: 'txt' }, h('div', { class: 'nm' }, name), h('div', { class: 'dt', tip: detail }, detail)),
      chip(status, kind),
      action || null);

    function paint() {
      if (!st) return;
      rows.replaceChildren();
      const mcpOk = st.mcp_registered && st.mcp_path_ok;
      const skillOk = st.skill_installed && st.skill_current;
      rows.append(
        item('claude', 'Claude Code 命令行', st.cli_found ? '已找到' : '未找到', st.cli_found ? 'ok' : 'warn', st.cli_path || '没有在 PATH 中找到 claude；仍可手动接入'),
        item('link', 'MCP 服务器 deixion', mcpOk ? '已注册' : st.mcp_registered ? '路径需更新' : '未注册', mcpOk ? 'ok' : 'warn', st.mcp_command || st.bridge_exe),
        item('sparkle', '技能 deixion', skillOk ? '已安装' : st.skill_installed ? '可更新' : st.skill_available ? '未安装' : '未随包提供', skillOk ? 'ok' : 'warn', st.skill_dir),
        item('bolt', '桥接程序', st.bridge_exists ? '就绪' : '缺失', st.bridge_exists ? 'ok' : 'danger', st.bridge_exe),
      );
      clients.replaceChildren(h('div', { class: 't' }, icon('claude', 'sm'), '已连接的客户端'), h('div', { class: 'n' }, String(st.clients ?? 0)));
    }

    async function load() {
      try {
        st = await call('claude.status');
        const cfg = await call('claude.config');
        cfgCmd.textContent = cfg.command_line;
        cfgJson.textContent = JSON.stringify(JSON.parse(cfg.json), null, 2);
        paint();
      } catch (e) { toast(e.message, 'danger'); }
    }

    const install = asyncBtn({ icon: 'download', label: '一键接入', kind: 'primary', big: true, tip: '注册 MCP 服务器并安装技能' }, async () => {
      const r = await call('claude.install');
      toast(r.mcp === 'registered' ? '已接入 Claude Code，重启 Claude Code 后生效' : '已安装技能；请按右侧方式手动注册 MCP', 'ok', 5000);
      await load();
    });
    const remove = asyncBtn({ icon: 'trash', label: '移除', big: true, kind: 'danger', tip: '从 Claude Code 移除 MCP 服务器与技能' }, async () => {
      if (!(await confirmBox('移除接入', '会从 Claude Code 注销 deixion 并删除技能文件，确定吗？', true, '移除'))) return;
      await call('claude.remove');
      toast('已移除', 'ok');
      await load();
    });

    const toolList = h('div', { class: 'rows' }, TOOLS.map(([n, d]) => h('div', { class: 'row' }, h('div', { class: 'k', style: { flex: '0 0 9em' } }, chip(n, 'accent')), h('div', { class: 'v', style: { flex: 1, justifyContent: 'flex-start', color: 'var(--c-text-2)' } }, d))));
    const left = h('div', { class: 's7 stack', style: { gridTemplateRows: 'auto minmax(0, 1fr) auto' } },
      panel({ label: '接入状态', body: rows, acts: [btn({ icon: 'refresh', tip: '重新检测', kind: 'ghost', onClick: load })] }),
      panel({ label: '可用工具', body: toolList }),
      h('div', { style: { display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 'var(--sp-3)' } }, clients, install, remove));
    const right = h('div', { class: 's5 stack', style: { gridTemplateRows: 'auto minmax(0, 1fr)' } },
      panel({ label: '命令行', body: cfgCmd, acts: [btn({ icon: 'copy', tip: '复制命令', kind: 'ghost', onClick: () => copy(cfgCmd.textContent, call) })] }),
      panel({ label: 'JSON 配置', body: cfgJson, acts: [btn({ icon: 'copy', tip: '复制配置', kind: 'ghost', onClick: () => copy(cfgJson.textContent, call) })] }));
    root.append(h('div', { class: 'grid', style: { gridTemplateRows: 'minmax(0, 1fr)' } }, left, right));
    let timer = 0;
    load();
    return {
      show() { load(); clearInterval(timer); timer = setInterval(() => { if (!document.hidden) call('claude.status').then((s) => { st = s; paint(); }).catch(() => {}); }, 3000); },
      hide() { clearInterval(timer); },
    };
  },
});
