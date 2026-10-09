// 指南：操作说明、快捷键、工作原理、工具表、排错。说明性文字只放在这一页，其它页面保持干净。
import { h, panel, chip, kbd } from '../core/dom.js';
import { icon } from '../core/icons.js';
import { bus } from '../core/bus.js';
import { state } from '../core/store.js';
import { registerPage } from '../core/registry.js';
import { tex } from '../core/math.js';
import { TOOLS } from '../core/meta.js';
import { t } from '../core/i18n.js';


registerPage({
  id: 'guide', icon: 'guide', tip: '操作指南', order: 900, area: 'foot',
  render(root) {
    const chordOf = (n) => state.hotkeys.find((k) => k.name === n)?.chord || state.settings?.hotkeys?.[n] || '';
    const hk = h('tbody');
    const paintKeys = () => {
      const rows = [
        ['toggle', t('显示 / 隐藏主窗口（任何程序里都可用）')], ['mode', t('在后台模式与前台模式间切换')], ['pause', t('暂停 / 继续接收 Claude 的操作')],
        ['undo', t('撤销上一步操作')], ['shot', t('截图（带经纬网格）复制到剪贴板')], ['stop', t('紧急停止正在执行的批处理')],
      ];
      hk.replaceChildren(...rows.map(([n, d]) => h('tr', null, h('td', { class: 'fit' }, kbd(chordOf(n))), h('td', { style: { maxWidth: 'none' } }, d))),
        ...[['Ctrl + 1 … 9, 0', t('切换到第 1 … 10 个页面')], [t('托盘图标 左键'), t('显示窗口；窗口在前台时再点一次则最小化')], [t('托盘图标 右键'), t('功能菜单：模式、暂停、撤销、截图、接入、更新、退出')], ['Esc', t('关闭对话框 / 取消快捷键录制')], [t('双击日志行'), t('复制该行日志')]]
          .map(([k, d]) => h('tr', null, h('td', { class: 'fit' }, h('span', { class: 'kbd' }, k)), h('td', { style: { maxWidth: 'none' } }, d))));
    };
    bus.on('hotkeys', paintKeys);
    bus.on('settings', paintKeys);

    const sec = (id, ic, title, ...body) => h('section', { class: 'guide-sec', id: `g-${id}` }, h('h2', null, icon(ic), title), ...body);
    const p = (...c) => h('p', null, ...c);
    const li = (...c) => h('li', null, ...c);

    const content = h('div', { class: 'guide' },
      sec('start', 'rocket', t('快速开始'),
        h('ol', null,
          li(t('打开左侧“接入”页，点“一键接入”：注册 MCP 服务器并安装技能。')),
          li(t('重启 Claude Code，直接说话，例如“打开记事本，写下今天的日期并保存”。')),
          li(t('回到“总览”，实时动作流、落点雷达会同步显示每一步做了什么。')),
          li(t('不想被打扰就用后台模式（默认）；想看过程就切到前台模式。')))),
      sec('keys', 'keyboard', t('快捷键'),
        h('table', { class: 'tbl' }, hk),
        p(t('全局快捷键可在“设置”里改，点“录制”后直接按新的组合键；被其它程序占用的会标红。'))),
      sec('modes', 'eyeoff', t('后台与前台'),
        p(t('后台模式全程不抢焦点、不动你的光标：优先走控件接口，其次直接给目标窗口发消息；都不行时，如果你允许，会极短暂地切到前台再切回来。你可以一边看视频、打游戏，一边让 Claude 干活。')),
        p(t('前台模式用真实的键鼠输入，光标沿平滑路径移动，并显示落点涟漪与高亮框，适合演示和排查。')),
        p(t('每次操作的通道选择由“经验”页里的统计决定：成功率高、速度快的通道优先，偶尔试探新通道。'))),
      sec('geo', 'locate', t('经纬定位'),
        p(t('任何窗口、屏幕、控件都被归一化成 [0,1]² 的平面，横向 λ、纵向 φ，左上角是 (0,0)。同一个坐标在任何分辨率、缩放、DPI 下指向同一处内容。')),
        tex('\\lambda=\\dfrac{x-x_0}{w},\\qquad \\varphi=\\dfrac{y-y_0}{h}', { block: true }),
        p(t('每个位置还有一个层级化短码：把 λ、φ 各量化到 16 位，按位交织（Morton 码），再用 Base32 编码。码越长格子越小，前缀就是外层格子，可以由粗到细逐级放大。')),
        tex('M(\\lambda,\\varphi)=\\sum_{i=0}^{15}\\bigl(b_i(\\lambda)\\,2^{2i+1}+b_i(\\varphi)\\,2^{2i}\\bigr)', { block: true }),
        p(t('第 '), tex('k'), t(' 级码对应的格子，横向 '), tex('2^{\\lceil 5k/2\\rceil}'), t(' 等分、纵向 '), tex('2^{\\lfloor 5k/2\\rfloor}'), t(' 等分；1080p 屏幕上，2 级码的格子约 60×34 像素，3 级码约 8 像素。'))),
      sec('learn', 'evolve', t('自我进化'),
        p(t('同一类控件、同一种操作，有多条可选的执行通道。引擎把每次的成败与耗时记进经验库，并用带衰减的 UCB 规则选下一次走哪条：')),
        tex('\\mathrm{UCB}_i=\\bar r_i+c\\sqrt{\\dfrac{\\ln N}{n_i}},\\qquad n_i\\leftarrow\\gamma\\,n_i\\ (\\gamma=0.985)', { block: true }),
        p(t('衰减让旧经验慢慢淡出，应用升级后会自动重新适应。经验库是追加写、带校验的日志，断电也不会损坏。'))),
      sec('undo', 'undo', t('日志与回滚'),
        p(t('每一步操作都记在历史里。能还原的（赋值、勾选、窗口位置与状态）可以一键撤销；键盘输入只能尽力回退。撤销时会先核对目标控件是否还在、是否被改动过。'))),
      sec('tools', 'bolt', t('Claude 能用的工具'),
        h('table', { class: 'tbl' }, h('thead', null, h('tr', null, h('th', null, t('工具')), h('th', null, t('作用')), h('th', null, t('常用参数')))),
          h('tbody', null, TOOLS.map(([n, d, a]) => h('tr', null, h('td', { class: 'fit' }, chip(n, 'accent')), h('td', { style: { maxWidth: 'none' } }, d), h('td', { style: { color: 'var(--c-text-3)', maxWidth: 'none' } }, a)))))),
      sec('trouble', 'info', t('遇到问题'),
        h('ul', null,
          li(t('没有界面：需要微软 WebView2 运行时（Windows 11 自带）。没有界面时托盘与 Claude Code 调用仍可用。')),
          li(t('快捷键没反应：设置里看是否标红“被其它程序占用”，换一个组合键。')),
          li(t('某个应用点不动：看“经验”页该应用各通道的成功率；前台模式可以看到真实落点；也可以允许“短暂切前台”兜底。')),
          li(t('关掉窗口后 Claude 还能用：窗口只是缩到托盘，从托盘菜单“退出”才会完全停止。')),
          li(t('日志与数据：设置里点“打开数据目录”，日志在 logs 子目录。')))));

    const toc = [['start', t('快速开始'), 'rocket'], ['keys', t('快捷键'), 'keyboard'], ['modes', t('后台与前台'), 'eyeoff'], ['geo', t('经纬定位'), 'locate'], ['learn', t('自我进化'), 'evolve'], ['undo', t('日志与回滚'), 'undo'], ['tools', t('工具'), 'bolt'], ['trouble', t('遇到问题'), 'info']];
    const nav = h('div', { class: 'rows toc' }, toc.map(([id, t, ic]) => h('button', { type: 'button', class: 'app-item', onClick: () => content.querySelector(`#g-${id}`)?.scrollIntoView({ behavior: 'smooth', block: 'start' }) }, icon(ic, 'sm'), h('span', { class: 'nm' }, t))));
    root.append(h('div', { class: 'grid', style: { gridTemplateRows: 'minmax(0, 1fr)' } }, panel({ cls: 's3', label: t('目录'), body: nav }), panel({ cls: 's9', body: content })));
    paintKeys();
    return {};
  },
});
