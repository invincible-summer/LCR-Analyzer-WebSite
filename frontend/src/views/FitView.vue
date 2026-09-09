<script setup lang="ts">
// FitView.vue — 电路辨识拟合（三引擎栏目页）
//
// 数据流：CSV 上传 / 示例生成 / 历史扫描导入 → ZPoint[]（本地）→ Web
// Worker 内执行共享 v4 C++ / WASM 核心。
// → Top-K 候选表 + 电路图（SP 树走 Schematic，非 SP 走 GraphSchematic）
// + 测量-理论叠加图。完全不经过 Python 后端；ESP32 蓝牙导入为规划项。
import { computed, reactive, ref, onUnmounted } from 'vue'
import { storeToRefs } from 'pinia'
import { useScanStore } from '../store/scan'
import * as api from '../api'
import EChart from '../components/EChart.vue'
import FigBlock from '../components/FigBlock.vue'
import Tabs from '../components/Tabs.vue'
import HelpBubble from '../components/HelpBubble.vue'
import Schematic from '../components/Schematic.vue'
import GraphSchematic from '../components/GraphSchematic.vue'
import GraphEditor from '../components/GraphEditor.vue'
import StatTile from '../components/StatTile.vue'
import { parseZCsv, toZCsv } from '../lib/csv'
import { graphToNetlist } from '../lib/adjacency'
import { polarToCartesianCov, usablePolarUncertainty, type PolarUncertainty } from '../lib/uncertainty'
import { runFitJob, cancelFitJob, FIT_AVAILABLE } from '../lib/lcrWasm'
import { DEMO_CASES, synthPoints } from '../lib/synthData'
import type {
  ComponentSpec, CompKind, FitCandidate, FitJob, FitResponse, TopoEdge, Try1Stats,
  Try2Stats, Try3Diagnostics, ZPoint,
} from '../lib/fitTypes'
import { fitErrorText, isFitOk } from '../lib/fitTypes'
import { getPalette } from '../lib/palette'
import { bodeOpt, nyquistOpt } from '../lib/charts'
import * as fmt from '../lib/format'
import {
  Upload, FileAudio, FlaskConical, History, Download, Play, Square, Network, CircuitBoard, Boxes, Puzzle,
} from '@lucide/vue'

const palette = computed(() => getPalette())
const store = useScanStore()
const { scans } = storeToRefs(store)

// ---------------------------------------------------------------------------
// 数据源
// ---------------------------------------------------------------------------

const points = ref<ZPoint[]>([])
const dataSource = ref('')
let dataRevision = 0
/** 后端扫描携带的极坐标不确定度（显式 opt-in 才转换为协方差） */
let scanUncertainty: PolarUncertainty | null = null
const useScanUncertainty = ref(false)
const parseMsg = reactive({ warnings: [] as string[], errors: [] as string[] })
const fileInputEl = ref<HTMLInputElement | null>(null)

function loadPoints(list: ZPoint[], source: string) {
  if (list.length < 4) {
    parseMsg.errors = ['有效数据点不足（至少 4 个）']
    parseMsg.warnings = []
    return
  }
  dataRevision++
  cancelFitJob()
  scanUncertainty = null
  useScanUncertainty.value = false
  points.value = [...list].sort((a, b) => a.f - b.f)
  dataSource.value = source
  parseMsg.errors = []
  parseMsg.warnings = []
  clearResults()
}

/** 显式开关：把扫描极坐标不确定度近似转换为逐点笛卡尔协方差（GLS）。 */
function toggleScanUncertainty() {
  if (!scanUncertainty) return
  if (useScanUncertainty.value) {
    points.value = points.value.map(({ cov: _cov, ...rest }) => rest)
    useScanUncertainty.value = false
    clearResults()
    return
  }
  const converted = polarToCartesianCov(points.value, scanUncertainty)
  if (!converted) {
    parseMsg.warnings = [
      '扫描不确定度无法整组转换为正定协方差（要求 ρ>0 且两个 σ>0），继续使用相对加权回退',
    ]
    return
  }
  points.value = converted
  useScanUncertainty.value = true
  clearResults()
}

function readFile(file: File) {
  const reader = new FileReader()
  reader.onload = () => {
    const r = parseZCsv(String(reader.result ?? ''))
    parseMsg.errors = r.errors
    parseMsg.warnings = r.warnings
    if (r.points.length >= 4) loadPoints(r.points, `CSV 文件 · ${file.name}`)
  }
  reader.readAsText(file)
}
function onFilePick(ev: Event) {
  const f = (ev.target as HTMLInputElement).files?.[0]
  if (f) readFile(f)
  ;(ev.target as HTMLInputElement).value = ''
}
function onDrop(ev: DragEvent) {
  const f = ev.dataTransfer?.files?.[0]
  if (f) readFile(f)
}

// 示例生成
const demoKey = ref(DEMO_CASES[0].key)
const demoNoise = ref(0.5)
function genDemo() {
  const c = DEMO_CASES.find((x) => x.key === demoKey.value) ?? DEMO_CASES[0]
  loadPoints(synthPoints(c.net, { noise: demoNoise.value / 100 }), `示例 · ${c.label}`)
}

// 历史扫描导入（保留极坐标不确定度，供显式 opt-in 转换）
const scanSel = ref('')
const scanHasUncertainty = ref(false)
async function importScan() {
  if (!scanSel.value) return
  const detail = await api.getScan(scanSel.value)
  if (!detail.measurements.length) {
    parseMsg.errors = ['该扫描没有测量点']
    parseMsg.warnings = []
    return
  }
  loadPoints(
    detail.measurements.map((m) => ({ f: m.frequency, re: m.z_real, im: m.z_imag })),
    `历史扫描 · ${detail.id}${detail.note ? ` · ${detail.note}` : ''}`,
  )
  const u: PolarUncertainty = {
    rho: detail.measurements.map((m) => m.z_sigma),
    phi: detail.measurements.map((m) => m.z_phase_sigma_deg),
  }
  scanUncertainty = usablePolarUncertainty(u) ? u : null
  scanHasUncertainty.value = !!scanUncertainty
  if (!scanUncertainty)
    parseMsg.warnings = ['该扫描的不确定度字段不完整（需要每点 z_sigma、z_phase_sigma_deg > 0），按相对加权回退']
}

function exportCsv() {
  if (!points.value.length) return
  const blob = new Blob([toZCsv(points.value)], { type: 'text/csv' })
  const a = document.createElement('a')
  a.href = URL.createObjectURL(blob)
  a.download = 'measurements.csv'
  a.click()
  URL.revokeObjectURL(a.href)
}

const stats = computed(() => {
  const ps = points.value
  if (!ps.length) return null
  const mags = ps.map((z) => Math.hypot(z.re, z.im))
  return {
    n: ps.length,
    fMin: Math.min(...ps.map((z) => z.f)),
    fMax: Math.max(...ps.map((z) => z.f)),
    zMin: Math.min(...mags),
    zMax: Math.max(...mags),
  }
})

const measured = computed(() =>
  points.value.map((z) => ({
    f: z.f,
    mag: Math.hypot(z.re, z.im),
    phase: (Math.atan2(z.im, z.re) * 180) / Math.PI,
    re: z.re,
    im: z.im,
  })),
)

const previewMagOpt = computed(() =>
  bodeOpt(palette.value, {
    mode: 'mag',
    measured: measured.value.map((m) => ({ f: m.f, v: m.mag })),
    yLabel: '|Z| (Ω)',
  }),
)
const previewPhaseOpt = computed(() =>
  bodeOpt(palette.value, {
    mode: 'phase',
    measured: measured.value.map((m) => ({ f: m.f, v: m.phase })),
    yLabel: '相位 (°)',
  }),
)
const previewNyqOpt = computed(() =>
  nyquistOpt(palette.value, { measured: measured.value.map((m) => ({ re: m.re, im: m.im })) }),
)

// ---------------------------------------------------------------------------
// 运行状态（共享 worker）
// ---------------------------------------------------------------------------

type TabKey = 'try1' | 'try2' | 'try3'
const tab = ref<TabKey>('try1')
onUnmounted(() => cancelFitJob())
const running = reactive<Record<TabKey, boolean>>({ try1: false, try2: false, try3: false })
const results = reactive<Record<TabKey, FitResponse | null>>({ try1: null, try2: null, try3: null })
const selectedRank = reactive<Record<TabKey, number>>({ try1: 1, try2: 1, try3: 1 })
const runError = reactive<Record<TabKey, string>>({ try1: '', try2: '', try3: '' })
const elapsed = reactive<Record<TabKey, number>>({ try1: 0, try2: 0, try3: 0 })

function clearResults() {
  results.try1 = results.try2 = results.try3 = null
  runError.try1 = runError.try2 = runError.try3 = ''
}

async function execute(job: FitJob) {
  if (Object.values(running).some(Boolean)) return
  job = {
    ...job,
    mode: searchMode.value,
    seconds: timeLimit.value,
    robust: robust.value,
    budget: advBudget.value || 0,
  }
  if (anyAdvanced.value)
    job = {
      ...job,
      starts: maybeNum(advStarts.value),
      iterations: maybeNum(advIterations.value),
      seed: maybeNum(advSeed.value),
      equivalenceTolerance: maybeNum(advEquivalenceTolerance.value),
    }
  const key = `try${job.try}` as TabKey
  running[key] = true
  runError[key] = ''
  results[key] = null
  const revision = dataRevision
  const t0 = performance.now()
  try {
    const resp = await runFitJob(job)
    if (revision !== dataRevision) return
    results[key] = resp
    if (!isFitOk(resp)) runError[key] = fitErrorText(resp)
    selectedRank[key] = 1
  } catch (e) {
    if (revision === dataRevision) runError[key] = e instanceof Error ? e.message : String(e)
  } finally {
    running[key] = false
    elapsed[key] = (performance.now() - t0) / 1000
  }
}
function cancel(key: TabKey) {
  cancelFitJob()
  runError[key] = '已取消'
}

// ---------------------------------------------------------------------------
// Try1：未知辨识
// ---------------------------------------------------------------------------

const searchMode = ref<'Strict' | 'Fast'>('Strict')
const timeLimit = ref(0)
const tolerance = ref(0)
const dcrTolerance = ref(0)
const robust = ref(false)
// 高级搜索设置（空 = 引擎默认：starts 16 / iterations 160 / seed 1 / 等价容差 1e-6）
const advBudget = ref(0)
const advStarts = ref('')
const advIterations = ref('')
const advSeed = ref('')
const advEquivalenceTolerance = ref('')
const maybeNum = (s: string | number): number | undefined => {
  // v-model on number inputs auto-casts to number; accept both shapes.
  const t = String(s ?? '').trim()
  if (t === '') return undefined
  const n = Number(t)
  return Number.isFinite(n) ? n : undefined
}
const anyAdvanced = computed(() =>
  [advStarts.value, advIterations.value, advSeed.value, advEquivalenceTolerance.value].some(
    (s) => String(s ?? '').trim() !== '',
  ),
)
const anyRunning = computed(() => Object.values(running).some(Boolean))
const exactN = ref('')
const maxNInput = ref('')
const maxDepthInput = ref('')
const topK1 = ref(5)
/** 数据噪声模型徽标：协方差整组提供时走 GLS，否则相对加权回退。 */
const noiseModel = computed(() => {
  const ps = points.value
  if (!ps.length) return null
  if (ps.every((p) => p.cov))
    return ps[0].cov?.source === 'scan_polar_approx'
      ? { text: '扫描不确定度近似 (GLS)', cls: 'warn' }
      : { text: '逐点协方差 (GLS)', cls: 'good' }
  return { text: '相对加权回退', cls: '' }
})
function runTry1() {
  const n = Number(exactN.value)
  execute({
    try: 1,
    points: points.value,
    exactN: String(exactN.value).trim() !== '' ? n : undefined,
    maxN: maybeNum(maxNInput.value),
    maxDepth: maybeNum(maxDepthInput.value),
    topK: topK1.value,
  })
}

// ---------------------------------------------------------------------------
// Try2：已知元件
// ---------------------------------------------------------------------------

interface CompRow { kind: CompKind; value: string; dcr: string; count: string }
const compRows = ref<CompRow[]>([
  { kind: 'R', value: '1k', dcr: '', count: '1' },
  { kind: 'C', value: '100n', dcr: '', count: '1' },
  { kind: 'L', value: '1m', dcr: '2', count: '1' },
])
const SI_PREFIX: Record<string, number> = { f: 1e-15, p: 1e-12, n: 1e-9, u: 1e-6, µ: 1e-6, m: 1e-3, k: 1e3, K: 1e3, M: 1e6, G: 1e9 }
function parseSI(s: string): number | null {
  const t = s.trim()
  if (/^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$/.test(t)) return Number(t)
  const m = t.match(/^([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)\s*([fpnuµmkKMG])$/)
  if (m) return Number(m[1]) * SI_PREFIX[m[2]]
  return null
}
const compErrors = computed(() => {
  const errs: string[] = []
  for (let i = 0; i < compRows.value.length; i++) {
    const r = compRows.value[i]
    const v = parseSI(r.value)
    if (v === null || !(v > 0)) errs.push(`第 ${i + 1} 行：数值非法（支持 1e-3 / 1m / 1k 等写法）`)
    const cnt = Number(r.count)
    if (!Number.isInteger(cnt) || cnt < 1 || cnt > 8) errs.push(`第 ${i + 1} 行：个数须为 1..8（引擎硬上限 8 个元件）`)
    if (r.kind === 'L') {
      const d = r.dcr.trim() === '' ? 0 : parseSI(r.dcr)
      if (d === null || d < 0) errs.push(`第 ${i + 1} 行：DCR 非法（需 ≥ 0）`)
    } else if (r.dcr.trim() !== '') {
      errs.push(`第 ${i + 1} 行：只有电感可填 DCR`)
    }
  }
  return errs
})
const compTotal = computed(() => compRows.value.reduce((s, r) => s + (Number(r.count) || 0), 0))
const compSummary = computed(() => {
  const byKind: Record<string, number> = { R: 0, L: 0, C: 0 }
  for (const r of compRows.value) byKind[r.kind] += Number(r.count) || 0
  return `R×${byKind.R} · L×${byKind.L} · C×${byKind.C}`
})
function addRow() {
  if (compRows.value.length >= 8) return
  compRows.value.push({ kind: 'R', value: '', dcr: '', count: '1' })
}
function runTry2() {
  if (compErrors.value.length) return
  const components: ComponentSpec[] = compRows.value.map((r) => ({
    kind: r.kind,
    value: parseSI(r.value)!,
    dcr: r.kind === 'L' ? (r.dcr.trim() === '' ? 0 : parseSI(r.dcr)!) : 0,
    count: Number(r.count),
  }))
  execute({ try: 2, points: points.value, components, topK: topK1.value, tolerance: tolerance.value / 100, dcrTolerance: dcrTolerance.value })
}

// ---------------------------------------------------------------------------
// Try3：已知拓扑
// ---------------------------------------------------------------------------

const try3Edges = ref<TopoEdge[]>([])
function runTry3() {
  if (!try3Edges.value.length) {
    runError.try3 = '请先在图编辑器中添加边（节点 0/1 为端口）'
    return
  }
  execute({ try: 3, points: points.value, edges: try3Edges.value })
}

// ---------------------------------------------------------------------------
// 结果呈现（按当前 tab）
// ---------------------------------------------------------------------------

const activeResult = computed(() => {
  const r = results[tab.value]
  return r && isFitOk(r) ? r : null
})
const activeCandidates = computed<FitCandidate[]>(() => activeResult.value?.candidates ?? [])
const activeCandidate = computed<FitCandidate | null>(
  () => activeCandidates.value.find((c) => c.rank === selectedRank[tab.value]) ?? activeCandidates.value[0] ?? null,
)
const activeStats1 = computed<Try1Stats | null>(() => (activeResult.value?.try === 1 ? activeResult.value.stats : null))
const activeStats2 = computed<Try2Stats | null>(() => (activeResult.value?.try === 2 ? activeResult.value.stats : null))
const activeDiag3 = computed<Try3Diagnostics | null>(() =>
  activeResult.value?.try === 3 ? activeResult.value.try3 : null,
)

const candNetlist = computed(() => (activeCandidate.value ? graphToNetlist(activeCandidate.value.adjacency) : null))
const candIsSp = computed(() => candNetlist.value !== null)
/** 校准 ΔAICc 仅在 AICc 主准则成立时展示；否则该列显示 —。 */
const qualifiedAicc = computed(() => {
  const s = activeResult.value?.search
  return !!s && s.selection.criterion === 'AICc' && s.selection.qualified
})
const searchStateText = computed(() => {
  const s = activeResult.value?.search
  if (!s) return ''
  if (tab.value === 'try3')
    return s.complete ? '多起点局部优化完成' : s.termination === 'budget_exhausted' ? '预算中止' : '部分优化结果'
  return s.complete ? '枚举完成' : '部分搜索结果'
})
const selectionText = computed(() => {
  const s = activeResult.value?.search
  if (!s) return ''
  const name: Record<string, string> = {
    AICc: 'AICc（校准模型选择）',
    AICc_PROVISIONAL_ORDER: 'AICc 顺序（含未收敛候选，未形成校准模型选择结论）',
    RSS_EXACT: '共同精确目标（有限空间严格搜索）',
    RSS_COMMON: '共同 RSS 目标',
    RSS_DIAGNOSTIC_FALLBACK: 'RSS 诊断回退（诊断排序，非校准模型选择）',
    NONE: '未进行跨模型选择',
  }
  return name[s.selection.criterion] ?? s.selection.criterion
})

const candTheory = computed(() => {
  const c = activeCandidate.value
  if (!c) return null
  const mag = c.theory.f.map((_, i) => Math.hypot(c.theory.re[i], c.theory.im[i]))
  const phase = c.theory.f.map((_, i) => (Math.atan2(c.theory.im[i], c.theory.re[i]) * 180) / Math.PI)
  return {
    mag: { f: c.theory.f, v: mag },
    phase: { f: c.theory.f, v: phase },
    nyq: { re: c.theory.re, im: c.theory.im },
  }
})
const fitMagOpt = computed(() =>
  bodeOpt(palette.value, {
    mode: 'mag',
    measured: measured.value.map((m) => ({ f: m.f, v: m.mag })),
    theory: candTheory.value?.mag,
    yLabel: '|Z| (Ω)',
    zoom: true,
  }),
)
const fitPhaseOpt = computed(() =>
  bodeOpt(palette.value, {
    mode: 'phase',
    measured: measured.value.map((m) => ({ f: m.f, v: m.phase })),
    theory: candTheory.value?.phase,
    yLabel: '相位 (°)',
    zoom: true,
  }),
)
const fitNyqOpt = computed(() =>
  nyquistOpt(palette.value, {
    measured: measured.value.map((m) => ({ re: m.re, im: m.im })),
    theory: candTheory.value?.nyq,
    zoom: true,
  }),
)

function errText(v: number): string {
  if (!Number.isFinite(v)) return '∞'
  if (v < 1e-12) return '<1e-12'
  return v.toExponential(1)
}
</script>

<template>
  <div class="view">
    <div class="panel panel-body" role="status">浏览器本地 C++ / WASM · v4</div>
    <!-- ============ 数据面板 ============ -->
    <section class="panel">
      <div class="panel-head">
        <span class="tag">DATA</span>
        <h3>测量数据</h3>
        <div class="spacer" />
        <HelpBubble
          title="测量数据输入"
          intro="三个拟合引擎共用同一份测量数据：每个频点的频率 f 与复阻抗 Z = Re + j·Im。"
          :rows="[
            ['CSV 格式', '每行 3 个逗号分隔数：f[Hz], Re(Z)[Ω], Im(Z)[Ω]'],
            ['f 约束', '> 0，建议 10 Hz – 10 MHz 对数分布'],
            ['点数', '≥ 4，建议 ≥ 20；Try3 建议 ≥ 4×储能元件数'],
            ['兼容', 'AlgorithmLcr measurements.txt（首行点数）可直接上传'],
            ['蓝牙', 'ESP32 蓝牙传输为规划功能，稍后提供'],
          ]"
        />
      </div>
      <div class="panel-body col">
        <div class="row">
          <input ref="fileInputEl" type="file" accept=".csv,.txt" hidden @change="onFilePick" />
          <button class="btn primary" type="button" @click="fileInputEl?.click()">
            <Upload />上传 CSV
          </button>
          <button class="btn" type="button" disabled title="ESP32 蓝牙传输 · 规划中，稍后支持">
            <FileAudio />蓝牙导入（稍后）
          </button>
          <span class="sep" />
          <select v-model="demoKey" class="demo-select">
            <option v-for="c in DEMO_CASES" :key="c.key" :value="c.key">{{ c.label }}</option>
          </select>
          <label class="field" style="flex-direction: row; align-items: center">
            <span>噪声 {{ demoNoise.toFixed(1) }}%</span>
            <input v-model.number="demoNoise" type="range" min="0" max="2" step="0.1" style="width: 90px" />
          </label>
          <button class="btn" type="button" @click="genDemo"><FlaskConical />生成示例</button>
          <span class="sep" />
          <select v-model="scanSel" class="scan-select">
            <option value="" disabled>选择历史扫描…</option>
            <option v-for="s in scans" :key="s.id" :value="s.id">
              {{ s.id }} · {{ s.note || s.device }} · {{ s.measurement_count }} 点
            </option>
          </select>
          <button class="btn" type="button" :disabled="!scanSel" @click="importScan"><History />导入扫描</button>
          <div class="spacer" />
          <button class="btn ghost sm" type="button" :disabled="!points.length" @click="exportCsv">
            <Download />导出 CSV
          </button>
        </div>
        <div v-if="!points.length" class="dropzone" @dragover.prevent @drop.prevent="onDrop">
          <Upload />
          <div>拖入或点击「上传 CSV」载入测量文件 —— 每行 <code>f, Re(Z), Im(Z)</code></div>
          <div class="hint">也可用「生成示例」或从历史扫描导入</div>
        </div>
        <template v-else>
          <div class="row tight">
            <span class="badge">{{ dataSource }}</span>
            <span v-if="noiseModel" class="qpill" :class="noiseModel.cls" :title="'噪声模型：' + noiseModel.text">
              噪声模型 · {{ noiseModel.text }}
            </span>
            <button
              v-if="scanHasUncertainty"
              class="btn sm ghost" type="button" :disabled="anyRunning"
              :title="'把后端扫描的极坐标不确定度 z_sigma/z_phase_sigma_deg 近似传播为逐点笛卡尔协方差（J·diag(σρ²,σφ²)·Jᵀ）。这是误差传播近似，不是完整波形最小二乘协方差；任何点不满足条件则整组回退相对加权。'"
              @click="toggleScanUncertainty"
            >
              {{ useScanUncertainty ? '停用扫描不确定度' : '使用扫描不确定度（近似）' }}
            </button>
          </div>
          <div v-if="parseMsg.warnings.length" class="qpill warn" style="align-self: flex-start">
            {{ parseMsg.warnings.join('；') }}
          </div>
          <div v-if="parseMsg.errors.length" class="qpill crit" style="align-self: flex-start">
            {{ parseMsg.errors.join('；') }}
          </div>
          <div class="stat-grid cols-4">
            <StatTile k="数据点" :v="stats!.n" />
            <StatTile k="频率范围" :v="`${fmt.eng(stats!.fMin, 'Hz', 3)} – ${fmt.eng(stats!.fMax, 'Hz', 3)}`" />
            <StatTile k="|Z| 范围" :v="`${fmt.eng(stats!.zMin, 'Ω', 3)} – ${fmt.eng(stats!.zMax, 'Ω', 3)}`" />
            <StatTile k="数据来源" :v="dataSource.split('·')[0].trim()" :sub="dataSource" />
          </div>
          <div class="preview-grid">
            <FigBlock no="D.1" title="数据预览 · |Z|(f)"><EChart :option="previewMagOpt" :height="230" /></FigBlock>
            <FigBlock no="D.2" title="数据预览 · ∠Z(f)"><EChart :option="previewPhaseOpt" :height="230" /></FigBlock>
            <FigBlock no="D.3" title="数据预览 · Nyquist"><EChart :option="previewNyqOpt" :height="230" /></FigBlock>
          </div>
          <details>
            <summary class="hint">前 5 行数据</summary>
            <table class="data">
              <thead>
                <tr><th>#</th><th class="num">f (Hz)</th><th class="num">Re(Z) (Ω)</th><th class="num">Im(Z) (Ω)</th></tr>
              </thead>
              <tbody>
                <tr v-for="(z, i) in points.slice(0, 5)" :key="i">
                  <td>{{ i + 1 }}</td>
                  <td class="num mono">{{ z.f.toPrecision(8) }}</td>
                  <td class="num mono">{{ z.re.toPrecision(8) }}</td>
                  <td class="num mono">{{ z.im.toPrecision(8) }}</td>
                </tr>
              </tbody>
            </table>
          </details>
        </template>
      </div>
    </section>

    <section class="panel"><div class="panel-body row" style="gap: 16px; flex-wrap: wrap">
      <template v-if="tab !== 'try3'">
        <label class="field"><span>搜索模式</span><select v-model="searchMode" :disabled="anyRunning"><option>Strict</option><option>Fast</option></select></label>
        <label class="field">
          <span>候选预算（0 = 模式默认）</span>
          <input v-model.number="advBudget" type="number" min="0" :disabled="anyRunning" style="width: 130px" />
        </label>
      </template>
      <span v-else class="qpill">Try 3 · 多起点局部优化（无离散枚举，预算对候选数不生效）</span>
      <label class="field"><span>时间预算 / 秒（0 不限）</span><input v-model.number="timeLimit" type="number" min="0" :disabled="anyRunning" /></label>
      <label class="field">
        <span>
          <input
            v-model="robust" type="checkbox" :disabled="anyRunning || (tab === 'try2' && tolerance <= 0)"
          />
          稳健拟合
        </span>
        <span v-if="tab === 'try2' && tolerance <= 0" class="hint">仅用于 Try2 容差模式（先启用元件容差）</span>
      </label>
      <template v-if="tab === 'try2'">
        <label class="field"><span>元件容差 ±%（0 = Exact）</span><input v-model.number="tolerance" type="number" min="0" max="99" :disabled="anyRunning" /></label>
        <label class="field"><span>DCR 绝对容差 / Ω</span><input v-model.number="dcrTolerance" type="number" min="0" :disabled="anyRunning" /></label>
      </template>
      <details style="align-self: center">
        <summary class="hint">高级设置（starts / iterations / seed / 等价容差）</summary>
        <div class="row tight" style="margin-top: 6px">
          <label class="field">起点数<input v-model="advStarts" type="number" min="1" placeholder="16" style="width: 90px" /></label>
          <label class="field">迭代上限<input v-model="advIterations" type="number" min="1" placeholder="160" style="width: 90px" /></label>
          <label class="field">随机种子<input v-model="advSeed" type="number" min="0" placeholder="1" style="width: 90px" /></label>
          <label class="field">等价容差<input v-model="advEquivalenceTolerance" type="number" min="0" step="any" placeholder="1e-6" style="width: 110px" /></label>
        </div>
      </details>
      <span class="hint">Strict 默认不限候选数；Fast 最多评估 1000 个候选。计算可随时取消。</span>
    </div></section>
    <div v-if="anyRunning" class="qpill" role="status">算法正在本地计算；可继续浏览页面。
      <button class="btn sm" @click="cancelFitJob()">取消当前计算</button>
    </div>
    <!-- ============ 三引擎栏目 ============ -->
    <Tabs
      :tabs="[
        { key: 'try1', label: 'Try 1 · 未知辨识', sub: '拓扑+参数全未知' },
        { key: 'try2', label: 'Try 2 · 已知元件', sub: '类型/数值/数量已知' },
        { key: 'try3', label: 'Try 3 · 已知拓扑', sub: '结构与位置已知' },
      ]"
      v-model="tab"
    />

    <!-- Try 1 -->
    <section v-if="tab === 'try1'" class="panel">
      <div class="panel-head">
        <span class="tag">TRY 1</span>
        <h3>完全未知单端口辨识</h3>
        <div class="spacer" />
        <HelpBubble
          title="Try 1 · 未知辨识"
          intro="在声明的串并联规范树库（引擎 A）中枚举拓扑并拟合参数，辅以有理拟合 + Foster 综合回传（引擎 B）；排序准则以运行结果的选择状态为准（AICc 校准排名或 RSS 诊断回退）。"
          :rows="[
            ['器件数约束', '1 – 12；不填 = 自由搜索（默认库上限 4）'],
            ['SP 深度', '默认 4，与器件数上限相互独立'],
            ['器件计数', 'R/C 各 1 个；电感 L+DCR 绑定算 1 个器件'],
            ['参数箱', 'R 1e-3–1e7 Ω · L 1e-10–10 H · C 1e-13–1e-3 F · DCR 0–1e7 Ω'],
            ['输出', 'Top-K 等价类：wRMSE / maxRel / AICc + 邻接矩阵电路图'],
          ]"
          :bullets="['候选须同时满足 AICc 有效、优化收敛、满秩且无触界参数才参与校准 ΔAICc 排名；其余为诊断候选', '低残差与局部满秩不能证明唯一内部接线']"
        />
      </div>
      <div class="panel-body col">
        <div class="row">
          <label class="field">
            规范等效模型器件数约束（可选，1–12）
            <input v-model="exactN" type="number" min="1" max="12" placeholder="不填 = 自由搜索" style="width: 180px" />
          </label>
          <label class="field">
            器件数上限 maxN（可选，1–12）
            <input v-model="maxNInput" type="number" min="1" max="12" placeholder="默认 4" style="width: 140px" />
          </label>
          <label class="field">
            SP 深度 maxDepth（可选，1–12）
            <input v-model="maxDepthInput" type="number" min="1" max="12" placeholder="默认 4（独立于 maxN）" style="width: 170px" />
          </label>
          <label class="field">
            Top-K
            <select v-model.number="topK1" style="width: 90px">
              <option :value="3">3</option>
              <option :value="5">5</option>
              <option :value="8">8</option>
            </select>
          </label>
          <button class="btn primary" type="button" :disabled="!FIT_AVAILABLE || points.length < 4 || anyRunning" @click="runTry1">
            <Play />{{ running.try1 ? '计算中…' : '运行 Try 1' }}
          </button>
          <button v-if="running.try1" class="btn" type="button" @click="cancel('try1')"><Square />取消</button>
          <span v-if="elapsed.try1 && !running.try1" class="hint mono">耗时 {{ elapsed.try1.toFixed(2) }} s</span>
        </div>
        <div v-if="runError.try1" class="qpill crit" style="align-self: flex-start">{{ runError.try1 }}</div>
        <div v-if="activeStats1" class="hint">
          引擎报告：生成 {{ activeStats1.generated }} · 结构 {{ activeStats1.structures }} · 已评估 {{ activeStats1.evaluated }} · 等价类 {{ activeStats1.classes }}
        </div>
      </div>
    </section>

    <!-- Try 2 -->
    <section v-if="tab === 'try2'" class="panel">
      <div class="panel-head">
        <span class="tag">TRY 2</span>
        <h3>已知元件多重集 · 穷举接线</h3>
        <div class="spacer" />
        <HelpBubble
          title="Try 2 · 已知元件"
          intro="元件类型、数值、数量全部已知（电感带串联 DCR），引擎穷举所有可能接线（含桥式/重边）：Exact 按共同精确目标排序；启用容差后在每元件容差箱内连续精调。"
          :rows="[
            ['元件总数 E', '硬上限 8；行数最多 8，单行个数 1..8'],
            ['成本提示', 'E ≥ 7 时结构数以百万计，建议 Fast 模式或候选预算'],
            ['数量级约束', '数值 > 0 即可，建议落在常规箱（R 1e-3–1e7 Ω 等）内'],
            ['必备条件', '支持纯 R；不同接线可能具有相同端口响应'],
            ['数值写法', '支持 1e-3 / 1m / 1k / 100n 等 SI 前缀'],
          ]"
        />
      </div>
      <div class="panel-body col">
        <table class="data">
          <thead>
            <tr><th>#</th><th>类型</th><th>数值（R[Ω] L[H] C[F]）</th><th>DCR [Ω]（仅 L）</th><th class="num">个数</th><th></th></tr>
          </thead>
          <tbody>
            <tr v-for="(r, i) in compRows" :key="i">
              <td>{{ i + 1 }}</td>
              <td>
                <select v-model="r.kind" style="width: 70px">
                  <option value="R">R</option>
                  <option value="L">L</option>
                  <option value="C">C</option>
                </select>
              </td>
              <td><input v-model="r.value" placeholder="如 1k / 100n / 1e-3" style="width: 150px" /></td>
              <td><input v-model="r.dcr" :disabled="r.kind !== 'L'" placeholder="0" style="width: 110px" /></td>
              <td><input v-model="r.count" type="number" min="1" max="8" style="width: 80px" /></td>
              <td>
                <button class="btn sm ghost" type="button" :disabled="compRows.length <= 1" @click="compRows.splice(i, 1)">删除</button>
              </td>
            </tr>
          </tbody>
        </table>
        <div class="row tight">
          <button class="btn sm" type="button" :disabled="compRows.length >= 8" @click="addRow"><Boxes />添加元件</button>
          <span class="qpill" :class="compTotal > 6 ? 'warn' : ''">{{ compSummary }} · 共 {{ compTotal }} 个</span>
          <span v-if="compTotal > 8" class="qpill crit">超过硬上限 8，无法运行</span>
          <span v-else-if="compTotal >= 7" class="qpill warn">E ≥ 7：结构数以百万计，建议 Fast 模式或设置候选预算</span>
        </div>
        <div v-if="compErrors.length" class="qpill crit" style="align-self: flex-start">{{ compErrors[0] }}</div>
        <div class="row">
          <button
            class="btn primary" type="button"
            :disabled="!FIT_AVAILABLE || points.length < 4 || anyRunning || !!compErrors.length || compTotal > 8 || compTotal < 1"
            @click="runTry2"
          >
            <Play />{{ running.try2 ? '计算中…' : '运行 Try 2' }}
          </button>
          <button v-if="running.try2" class="btn" type="button" @click="cancel('try2')"><Square />取消</button>
          <span v-if="elapsed.try2 && !running.try2" class="hint mono">耗时 {{ elapsed.try2.toFixed(2) }} s</span>
        </div>
        <div v-if="runError.try2" class="qpill crit" style="align-self: flex-start">{{ runError.try2 }}</div>
        <div v-if="activeStats2" class="hint">
          引擎报告：生成 {{ activeStats2.generated }} · 结构 {{ activeStats2.structures }} · 已评估 {{ activeStats2.evaluated }} · 等价类 {{ activeStats2.classes }}
        </div>
      </div>
    </section>

    <!-- Try 3 -->
    <section v-if="tab === 'try3'" class="panel">
      <div class="panel-head">
        <span class="tag">TRY 3</span>
        <h3>已知拓扑 · 参数反演</h3>
        <div class="spacer" />
        <HelpBubble
          title="Try 3 · 已知拓扑"
          intro="拓扑与每条边的元件类型已知，引擎对 log 参数做多起点箱约束最小二乘，输出每条边（或合并群）的拟合数值与可辨识性诊断。"
          :rows="[
            ['节点 0 / 1', '单端口两端点，必须出现在边集中'],
            ['规模建议', '节点 ≤ 8，边 ≤ 12'],
            ['频点建议', '≥ max(4×储能元件数, 2×参数数)'],
            ['自动减支', '并联 R/C 合并 / 同型串联合并 / R 折入 DCR / 完整割点死区删除；聚合参数的有效域按表达式传播，可超出单器件全局箱'],
            ['输出', '单结果：wRMSE / AICc / 群参数 + 有效域、表达式、固定/弱/触边界与 Jacobian 秩诊断'],
          ]"
          :bullets="['输入方式参照 csacademy graph editor：绘制/拖动/编辑/删除四种模式，左侧边表可直接键入 u v R|L|C']"
        />
      </div>
      <div class="panel-body col">
        <GraphEditor v-model:edges="try3Edges" />
        <div class="row">
          <button class="btn primary" type="button" :disabled="!FIT_AVAILABLE || points.length < 4 || anyRunning" @click="runTry3">
            <Play />{{ running.try3 ? '计算中…' : '运行 Try 3' }}
          </button>
          <button v-if="running.try3" class="btn" type="button" @click="cancel('try3')"><Square />取消</button>
          <span v-if="elapsed.try3 && !running.try3" class="hint mono">耗时 {{ elapsed.try3.toFixed(2) }} s</span>
          <span class="hint">当前边数 {{ try3Edges.length }}</span>
        </div>
        <div v-if="runError.try3" class="qpill crit" style="align-self: flex-start">{{ runError.try3 }}</div>
      </div>
    </section>

    <!-- ============ 结果区 ============ -->
    <template v-if="activeResult">
      <div class="qpill" :class="activeResult.search.complete ? 'good' : 'warn'">
        {{ activeResult.search.mode }} · {{ searchStateText }} · {{ activeResult.search.termination }}
        · {{ activeResult.search.certified ? '有限候选空间最优已验证' : '不保证连续参数全局最优或唯一物理结构' }}
        · 选择准则 {{ selectionText }}
        · 数值失败 {{ activeResult.search.failures }}
      </div>
      <div v-if="!activeCandidates.length" class="hint">没有数值可靠的候选；可增加预算或检查输入。</div>
      <!-- 候选表 -->
      <section class="panel">
        <div class="panel-head">
          <span class="tag">RESULTS</span>
          <h3>候选结果 · Top-{{ activeCandidates.length }}</h3>
          <div class="spacer" />
          <span class="hint mono">引擎耗时 {{ (activeResult.elapsed ?? 0).toFixed(3) }} s</span>
        </div>
        <div class="panel-body">
          <table class="data cand-table">
            <thead>
              <tr>
                <th>排名</th><th class="num">器件数</th><th class="num">参数数</th>
                <th class="num">wRMSE</th><th class="num">maxRel</th>
                <th class="num">AICc</th><th class="num">ΔAICc</th>
                <th v-if="tab === 'try2'">串并联</th>
                <th>备注</th>
              </tr>
            </thead>
            <tbody>
              <tr
                v-for="c in activeCandidates" :key="c.rank"
                :class="{ sel: c.rank === activeCandidate?.rank }"
                style="cursor: pointer"
                @click="selectedRank[tab] = c.rank"
              >
                <td class="mono"><b>{{ c.rank }}</b></td>
                <td class="num mono">{{ c.devices }}</td>
                <td class="num mono">{{ c.n_params }}</td>
                <td class="num mono">{{ errText(c.wrmse) }}</td>
                <td class="num mono">{{ errText(c.max_rel) }}</td>
                <td class="num mono">{{ fmt.fmt(c.aicc, 2) }}</td>
                <td class="num mono" :class="{ muted: qualifiedAicc && c.selection?.delta != null && c.selection.delta >= 2 }">
                  <template v-if="qualifiedAicc && c.selection?.delta != null">{{ fmt.fmt(c.selection.delta, 2) }}</template>
                  <template v-else>—</template>
                </td>
                <td v-if="tab === 'try2'">
                  <span class="qpill" :class="c.sp ? 'good' : 'warn'">{{ c.sp ? 'SP' : '桥式' }}</span>
                </td>
                <td class="muted">
                  <div class="row tight" style="gap: 4px; flex-wrap: wrap">
                    <span v-if="c.selection && c.selection.criterion === 'AICc_PROVISIONAL'" class="qpill warn" :title="'未收敛候选（provisional）：优化器未达收敛判据，仅以当前 AICc 参与排序（' + c.selection.reasons.join('、') + '），不参与校准 ΔAICc，其当前 AICc 是该拓扑可达 AICc 的保守上界'">未收敛候选</span>
                    <span v-else-if="c.selection && !c.selection.eligible" class="qpill warn" :title="'诊断候选：' + c.selection.reasons.join('、')">诊断候选</span>
                    <span v-if="(c.diagnostics?.parameters ?? []).some(p => p.fixed)" class="qpill" :title="'固定参数（独立状态，不计触边界）：' + c.diagnostics!.parameters.filter(p => p.fixed).map(p => p.quantity === 'dcr' ? 'DCR' : p.kind).join('、')">
                      固定 ×{{ c.diagnostics!.parameters.filter(p => p.fixed).length }}
                    </span>
                    <span v-if="c.diagnostics?.numerical_status && c.diagnostics.numerical_status !== 'OK'" class="qpill warn">数值 {{ c.diagnostics.numerical_status }}</span>
                    <span v-if="c.diagnostics?.identifiability_status === 'RANK_DEFICIENT'" class="qpill warn">秩亏</span>
                    <span v-if="c.diagnostics?.identifiability_status === 'DATA_INSUFFICIENT'" class="qpill warn">数据不足</span>
                  </div>
                  <div>
                    <template v-if="tab === 'try1'">
                      引擎 {{ c.engine }}<template v-if="(c.n_members ?? 1) > 1"> · 等价 ×{{ c.n_members }}</template>
                    </template>
                    <template v-else-if="tab === 'try2'">
                      <template v-if="(c.n_members ?? 1) > 1">等价 ×{{ c.n_members }}</template>
                      <span class="mono muted"> {{ c.structure }}</span>
                    </template>
                    <template v-else>确定性拟合 · 群 {{ c.devices }}</template>
                    <span v-if="c.diagnostics" class="mono muted"> · {{ c.diagnostics.verdict }}</span>
                  </div>
                </td>
              </tr>
            </tbody>
          </table>
          <div class="hint" style="margin-top: 6px">
            点击行切换下方电路图与叠加曲线。
            <template v-if="!qualifiedAicc">
              当前选择准则为 {{ selectionText }}，ΔAICc 未校准故显示 —；标注「诊断候选」或「未收敛候选」的条目不参与校准 ΔAICc 排名（悬停查看原因）。
            </template>
          </div>
        </div>
      </section>

      <!-- 电路图 -->
      <section v-if="activeCandidate" class="panel">
        <div class="panel-head">
          <span class="tag">SCHEMATIC</span>
          <h3>等效电路</h3>
          <div class="spacer" />
          <label class="field" style="flex-direction: row; align-items: center; gap: 6px">
            <span>候选</span>
            <select
              :value="activeCandidate?.rank"
              style="width: 230px"
              @change="selectedRank[tab] = Number(($event.target as HTMLSelectElement).value)"
            >
              <option v-for="c in activeCandidates" :key="c.rank" :value="c.rank">
                #{{ c.rank }} · wRMSE {{ errText(c.wrmse) }}
              </option>
            </select>
          </label>
          <span v-if="!candIsSp" class="qpill warn">非串并联拓扑 · 图论视图</span>
          <CircuitBoard v-else style="width: 15px; height: 15px; color: var(--good)" />
        </div>
        <div class="panel-body">
          <div class="schematic-wrap">
            <Schematic v-if="candIsSp" :netlist="candNetlist" />
            <GraphSchematic v-else :adjacency="activeCandidate!.adjacency" />
          </div>
        </div>
      </section>

      <!-- 拟合叠加图 -->
      <div class="preview-grid">
        <FigBlock no="F.1" title="拟合对比 · |Z|(f)">
          <EChart :option="fitMagOpt" :height="280" />
        </FigBlock>
        <FigBlock no="F.2" title="拟合对比 · ∠Z(f)">
          <EChart :option="fitPhaseOpt" :height="280" />
        </FigBlock>
        <FigBlock no="F.3" title="拟合对比 · Nyquist">
          <EChart :option="fitNyqOpt" :height="280" />
        </FigBlock>
      </div>

      <!-- Try 3 诊断 -->
      <section v-if="activeDiag3" class="panel">
        <div class="panel-head">
          <span class="tag">DIAGNOSTICS</span>
          <h3>Try 3 拟合诊断</h3>
          <div class="spacer" />
          <span class="qpill" :class="activeDiag3.jac_rank < (activeCandidate?.n_params ?? 0) ? 'warn' : 'good'">
            Jacobian 秩 {{ activeDiag3.jac_rank }} / {{ activeCandidate?.n_params }}
          </span>
          <span class="qpill" :class="activeDiag3.jac_cond === null || activeDiag3.jac_cond > 1e6 ? 'warn' : ''">
            条件数 {{ activeDiag3.jac_cond === null ? '不可用' : activeDiag3.jac_cond.toExponential(1) }}
          </span>
          <span class="qpill">多起点 {{ activeDiag3.n_starts_used }} 次</span>
          <Network style="width: 14px; height: 14px; color: var(--text-3)" />
        </div>
        <div class="panel-body col">
          <table class="data">
            <thead>
              <tr>
                <th>群</th><th>类型</th><th>节点对</th><th>聚合表达式</th>
                <th class="num">聚合数值（有效域）</th><th>成员边</th><th>参数状态</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="g in activeDiag3.groups" :key="g.gid">
                <td class="mono">#{{ g.gid }}</td>
                <td><span class="qpill" :class="`k-${g.kind}`">{{ g.kind }}</span></td>
                <td class="mono">{{ g.u }} — {{ g.v }}</td>
                <td class="mono muted">
                  {{ g.expression.value }}<template v-if="g.expression.dcr"> · DCR = {{ g.expression.dcr }}</template>
                </td>
                <td class="num mono">
                  {{
                    g.kind === 'R' ? fmt.eng(g.value.v1, 'Ω', 4)
                    : g.kind === 'C' ? fmt.eng(g.value.v1, 'F', 4)
                    : `${fmt.eng(g.value.v1, 'H', 4)} + ${fmt.eng(g.value.v2, 'Ω', 3)} DCR`
                  }}
                  <span class="muted" style="font-size: 11px">
                    （{{ g.kind === 'C' ? 'F' : 'Ω' }} 域 {{ fmt.eng(g.value_bounds?.lo ?? 0, '', 1) }}–{{ fmt.eng(g.value_bounds?.hi ?? 0, '', 1) }}<template v-if="g.dcr_bounds && g.kind === 'L'">；DCR 域 {{ fmt.eng(g.dcr_bounds.lo, 'Ω', 1) }}–{{ fmt.eng(g.dcr_bounds.hi, 'Ω', 1) }}</template>）
                  </span>
                </td>
                <td class="mono muted">{{ g.members.map((m: number) => m + 1).join(', ') }}</td>
                <td>
                  <span v-if="g.fixed.length" class="qpill" :title="'固定参数：' + g.fixed.join('、')">固定（{{ g.fixed.join('、') }}）</span>
                  <span v-if="g.weak.length" class="qpill warn">弱参数</span>
                  <span v-if="g.at_bound.length" class="qpill warn">触边界</span>
                  <span v-if="!g.weak.length && !g.at_bound.length && !g.fixed.length" class="qpill good">良好</span>
                </td>
              </tr>
            </tbody>
          </table>
          <div v-if="activeDiag3.notes.length" class="col">
            <div v-for="(n, i) in activeDiag3.notes" :key="i" class="hint mono">· {{ n }}</div>
          </div>
          <details>
            <summary class="hint">逐边状态（fitted / merged / dropped）</summary>
            <table class="data">
              <thead><tr><th>边</th><th>类型</th><th>状态</th><th>所属群</th><th>说明</th></tr></thead>
              <tbody>
                <tr v-for="e in activeDiag3.edges" :key="e.index">
                  <td class="mono">#{{ e.index + 1 }}</td>
                  <td>{{ e.kind }}</td>
                  <td>
                    <span class="qpill" :class="e.status === 'fitted' ? 'good' : e.status === 'merged' ? 'warn' : 'crit'">{{ e.status }}</span>
                  </td>
                  <td class="mono muted">{{ e.group >= 0 ? `#${e.group}` : '—' }}</td>
                  <td class="muted">{{ e.note || '—' }}</td>
                </tr>
              </tbody>
            </table>
          </details>
        </div>
      </section>
    </template>

    <!-- 无结果空态 -->
    <section v-else-if="points.length" class="panel empty">
      <Puzzle />
      <div>
        数据已就绪（{{ points.length }} 点）—— 在上方「{{ { try1: 'Try 1 · 未知辨识', try2: 'Try 2 · 已知元件', try3: 'Try 3 · 已知拓扑' }[tab] }}」栏目可查看输入约束；点击运行开始本地计算（至少需要 4 个频点）。
      </div>
    </section>
  </div>
</template>

<style scoped>
.sep { width: 1px; height: 22px; background: var(--border-strong); margin: 0 2px; }
.dropzone {
  display: grid;
  place-items: center;
  gap: 8px;
  padding: 44px 20px;
  border: 1.5px dashed var(--border-strong);
  border-radius: var(--r);
  color: var(--text-3);
  font-size: 12.5px;
  cursor: pointer;
  text-align: center;
}
.dropzone:hover { border-color: var(--accent); color: var(--text-2); }
.dropzone svg { width: 26px; height: 26px; opacity: .5; }
.preview-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px; }
@media (max-width: 1100px) { .preview-grid { grid-template-columns: 1fr; } }
.demo-select { max-width: 360px; }
.cand-table tbody tr.sel td { background: rgba(36, 86, 166, 0.07); border-bottom-color: var(--accent); }
.cand-table tbody tr.sel td:first-child { box-shadow: inset 2px 0 0 var(--accent); }
:deep(.qpill.k-R) { color: var(--series-1); border-color: rgba(59, 111, 182, 0.4); }
:deep(.qpill.k-L) { color: var(--series-2); border-color: rgba(217, 110, 43, 0.4); }
:deep(.qpill.k-C) { color: var(--series-3); border-color: rgba(33, 138, 99, 0.4); }
</style>
