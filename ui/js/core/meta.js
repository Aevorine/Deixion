// 方法与策略的显示名、图标：多个页面共用，只在这里登记一份。
export const METHODS = {
  click: { icon: 'actions', label: '点击' },
  type: { icon: 'type', label: '输入' },
  set_value: { icon: 'type', label: '赋值' },
  key: { icon: 'keyboard', label: '按键' },
  scroll: { icon: 'scroll', label: '滚动' },
  drag: { icon: 'drag', label: '拖拽' },
  window: { icon: 'window', label: '窗口' },
  launch: { icon: 'rocket', label: '启动' },
  wait: { icon: 'wait', label: '等待' },
  batch: { icon: 'layers', label: '批量' },
  rollback: { icon: 'undo', label: '撤销' },
  capture: { icon: 'camera', label: '截图' },
  elements: { icon: 'elements', label: '元素' },
  find: { icon: 'search', label: '查找' },
  locate: { icon: 'locate', label: '定位' },
  geo: { icon: 'grid', label: '坐标' },
  read: { icon: 'eye', label: '读取' },
  windows: { icon: 'window', label: '窗口列表' },
  status: { icon: 'bolt', label: '状态' },
  perf: { icon: 'perf', label: '性能' },
};
export const methodInfo = (m) => METHODS[m] || { icon: 'bolt', label: m };

export const STRATEGIES = {
  uia: 'UIA 控件',
  uia_hit: 'UIA 命中',
  uia_set: 'UIA 赋值',
  uia_scroll: 'UIA 滚动',
  msg: '窗口消息',
  msg_char: '字符消息',
  msg_key: '按键消息',
  msg_chord: '组合键消息',
  msg_settext: '设置文本',
  hop: '短暂前台',
  real: '真实输入',
};
export const strategyName = (s) => STRATEGIES[s] || s || '—';

/** 允许的执行方法（操作页的下拉）与对应参数提示。 */
export const ACTION_FORMS = {
  click: { fields: ['window', 'at', 'find', 'button', 'double'] },
  type: { fields: ['window', 'at', 'find', 'text'] },
  set_value: { fields: ['window', 'find', 'value'] },
  key: { fields: ['window', 'keys'] },
  scroll: { fields: ['window', 'at', 'dy'] },
  drag: { fields: ['window', 'from', 'to'] },
  window: { fields: ['window', 'op'] },
  launch: { fields: ['path'] },
  wait: { fields: ['ms'] },
};

/** Claude 能用的工具：指南页与接入页共用。 */
export const TOOLS = [
  ['windows', '列出可操作的窗口', 'filter="记事本"'],
  ['capture', '截图（可带经纬网格、控件框、局部放大）', 'window="title:记事本", code="K7Q"'],
  ['elements', '读出窗口的全部控件与坐标', 'window, query="保存"'],
  ['find', '按名称模糊查找控件', 'text="确定", role="Button"'],
  ['locate', '查看某个坐标处是什么控件', 'at="0.42,0.31"'],
  ['click', '点击（控件 / 经纬度 / 像素）', 'find={text:"确定"} 或 at="0.5,0.5"'],
  ['type', '输入文字', 'text="你好"'],
  ['set_value', '直接给输入框赋值', 'find={role:"Edit"}, value="…"'],
  ['key', '按键与组合键', 'keys="ctrl+s"'],
  ['scroll', '滚动', 'at, dy=-3'],
  ['drag', '拖拽', 'from, to'],
  ['window', '窗口控制', 'op="maximize"'],
  ['launch', '启动程序', 'path="notepad.exe"'],
  ['wait', '等界面安静 / 窗口出现 / 控件出现', 'for="settle"'],
  ['batch', '一次发多步，省往返', 'steps=[{do:"click",…}]'],
  ['rollback', '撤销已做的操作', 'count=1'],
];
