// docs/index.ts — 使用文档清单（左侧栏）。正文是 markdown 源文件（?raw
// 原样导入），渲染管线在 lib/docToc.ts（含标题锚点与 TOC 提取）。
import overview from './overview.md?raw'
import dataFormat from './data-format.md?raw'
import try1 from './try1.md?raw'
import try2 from './try2.md?raw'
import try3 from './try3.md?raw'
import constraints from './constraints.md?raw'
import algorithms from './algorithms.md?raw'
import esp32 from './esp32.md?raw'

export interface DocEntry {
  key: string
  title: string
  group: string
  source: string
}

export const DOCS: DocEntry[] = [
  { key: 'overview', title: '总览与快速上手', group: '入门', source: overview },
  { key: 'data-format', title: '数据格式与上传', group: '入门', source: dataFormat },
  { key: 'try1', title: 'Try 1 · 未知辨识', group: '拟合引擎', source: try1 },
  { key: 'try2', title: 'Try 2 · 已知元件', group: '拟合引擎', source: try2 },
  { key: 'try3', title: 'Try 3 · 已知拓扑', group: '拟合引擎', source: try3 },
  { key: 'constraints', title: '约束与数量级总表', group: '参考', source: constraints },
  { key: 'algorithms', title: '算法背景', group: '参考', source: algorithms },
  { key: 'esp32', title: 'ESP32 接口（规划中）', group: '参考', source: esp32 },
]
