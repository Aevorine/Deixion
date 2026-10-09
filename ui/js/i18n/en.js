// 英文词典：每个页面一份，合并成一张表。键是代码里的中文原文，值是英文；同一个键出现在多份里时后者覆盖前者（检查器会报不一致）。
import core from './en/core.js';
import settings from './en/settings.js';
import overview from './en/overview.js';
import locate from './en/locate.js';
import elements from './en/elements.js';
import actions from './en/actions.js';
import evolve from './en/evolve.js';
import logs from './en/logs.js';
import perf from './en/perf.js';
import claude from './en/claude.js';
import guide from './en/guide.js';

export const SOURCES = { core, settings, overview, locate, elements, actions, evolve, logs, perf, claude, guide };
export const EN = Object.assign({}, ...Object.values(SOURCES));
