import { createRouter, createWebHashHistory } from 'vue-router'

const routes = [
  { path: '/', redirect: '/analysis' },
  { path: '/analysis', name: 'analysis', component: () => import('./views/AnalysisView.vue'), meta: { title: '时域分析', sub: '采集 → 去直流 → 正弦拟合 → 残差 → 频谱 → 阻抗' } },
  { path: '/sweep', name: 'sweep', component: () => import('./views/SweepView.vue'), meta: { title: '扫频 Bode / Nyquist', sub: '跨频率 |Z|(f) · 相位(f) · 复平面' } },
  { path: '/fit', name: 'fit', component: () => import('./views/FitView.vue'), meta: { title: '电路辨识拟合', sub: 'C++ 引擎（WASM）· Try1 未知辨识 / Try2 已知元件 / Try3 已知拓扑' } },
  { path: '/docs', name: 'docs', component: () => import('./views/DocsView.vue'), meta: { title: '使用文档', sub: '数据格式 · 三个引擎 · 约束与算法' } },
  { path: '/live', name: 'live', component: () => import('./views/LiveView.vue'), meta: { title: '实时监测', sub: 'WebSocket 在线波形' } },
  { path: '/calibrate', name: 'calibrate', component: () => import('./views/CalibrateView.vue'), meta: { title: 'OSL 校准', sub: '开路 / 短路 / 负载' } },
  { path: '/history', name: 'history', component: () => import('./views/HistoryView.vue'), meta: { title: '实验历史', sub: '扫描记录 · 导出 CSV/JSON' } },
]

export default createRouter({ history: createWebHashHistory(), routes })
