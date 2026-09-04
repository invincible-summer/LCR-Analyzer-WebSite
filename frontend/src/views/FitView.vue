<script setup lang="ts">
// FitView.vue — 电路辨识拟合（三引擎栏目页）
//
// 数据流：CSV 上传 / 示例生成 / 历史扫描导入 → ZPoint[]（本地）→ Web
// Worker 内的 WASM 引擎（Try1 未知辨识 / Try2 已知元件 / Try3 已知拓扑）
// → Top-K 候选表 + 电路图（SP 树走 Schematic，非 SP 走 GraphSchematic）
// + 测量-理论叠加图。完全不经过 Python 后端；ESP32 蓝牙导入为规划项。
import { computed, reactive, ref } from 'vue'
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
import { runFitJob, cancelFitJob } from '../lib/lcrWasm'
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
const parseMsg = reactive({ warnings: [] as string[], errors: [] as string[] })
const fileInputEl = ref<HTMLInputElement | null>(null)

function loadPoints(list: ZPoint[], source: string) {
  if (list.length < 4) {
    parseMsg.errors = ['有效数据点不足（至少 4 个）']
    parseMsg.warnings = []
    return
  }
  points.value = [...list].sort((a, b) => a.f - b.f)
  dataSource.value = source
  parseMsg.errors = []
  parseMsg.warnings = []
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

// 历史扫描导入
const scanSel = ref('')
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
  const key = `try${job.try}` as TabKey
  running[key] = true
  runError[key] = ''
  const t0 = performance.now()
  try {
    const resp = await runFitJob(job)
    results[key] = resp
    if (!isFitOk(resp)) runError[key] = fitErrorText(resp)
    selectedRank[key] = 1
  } catch (e) {
    runError[key] = e instanceof Error ? e.message : String(e)
  } finally {
    running[key] = false
    elapsed[key] = (performance.now() - t0) / 1000
  }
}
function cancel(key: TabKey) {
  cancelFitJob()
  running[key] = false
  runError[key] = '已取消'
}

// ---------------------------------------------------------------------------
// Try1：未知辨识
// ---------------------------------------------------------------------------

const exactN = ref('')
const topK1 = ref(5)
function runTry1() {
  const n = Number(exactN.value)
  execute({
    try: 1,
    points: points.value,
    exactN: exactN.value.trim() !== '' && Number.isInteger(n) && n >= 1 ? n : undefined,
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
    if (!Number.isInteger(cnt) || cnt < 1 || cnt > 64) errs.push(`第 ${i + 1} 行：个数须为 1..64`)
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
  execute({ try: 2, points: points.value, components, topK: topK1.value })
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
const minAicc = computed(() =>
  activeCandidates.value.length ? Math.min(...activeCandidates.value.map((c) => c.aicc)) : 0,
)

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
          intro="在串并联规范树库中枚举拓扑并拟合参数（引擎 A），辅以有理拟合 + Foster 综合回传（引擎 B），按 AICc 排序输出等价类。"
          :rows="[
            ['器件数约束', '1 – 6；不填 = 自由搜索（默认库上限 4）'],
            ['器件计数', 'R/C 各 1 个；电感 L+DCR 绑定算 1 个器件'],
            ['参数箱', 'R 1e-3–1e7 Ω · L 1e-10–10 H · C 1e-13–1e-3 F · DCR 1e-6–1e7 Ω'],
            ['耗时量级', '单次 ≈ 0.1 – 1 s（浏览器内 WASM）'],
            ['输出', 'Top-K 等价类：wRMSE / maxRel / AICc + 邻接矩阵电路图'],
          ]"
          :bullets="['ΔAICc < 2 的候选视为并列最优，优先选择器件更少、可解释性更强的模型']"
        />
      </div>
      <div class="panel-body col">
        <div class="row">
          <label class="field">
            器件总数约束（可选，1–6）
            <input v-model="exactN" type="number" min="1" max="6" placeholder="不填 = 自由搜索" style="width: 180px" />
          </label>
          <label class="field">
            Top-K
            <select v-model.number="topK1" style="width: 90px">
              <option :value="3">3</option>
              <option :value="5">5</option>
              <option :value="8">8</option>
            </select>
          </label>
          <button class="btn primary" type="button" :disabled="!points.length || running.try1" @click="runTry1">
            <Play />{{ running.try1 ? '计算中…' : '运行 Try 1' }}
          </button>
          <button v-if="running.try1" class="btn" type="button" @click="cancel('try1')"><Square />取消</button>
          <span v-if="elapsed.try1 && !running.try1" class="hint mono">耗时 {{ elapsed.try1.toFixed(2) }} s</span>
        </div>
        <div v-if="runError.try1" class="qpill crit" style="align-self: flex-start">{{ runError.try1 }}</div>
        <div v-if="activeStats1" class="hint">
          引擎报告：拓扑库 {{ activeStats1.n_library }} · 剪枝保留 {{ activeStats1.n_pruned_kept }} · 等价类 {{ activeStats1.n_classes }}
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
          intro="元件类型、数值、数量全部已知（电感带串联 DCR），引擎穷举所有可能接线（含桥式/重边），按残差排序输出等价类。"
          :rows="[
            ['元件总数 E', '推荐 ≤ 6（秒级）；硬上限 8'],
            ['数量级约束', '数值 > 0 即可，建议落在常规箱（R 1e-3–1e7 Ω 等）内'],
            ['必备条件', '至少 1 个 L 或 C（纯电阻网络不可辨识）'],
            ['数值写法', '支持 1e-3 / 1m / 1k / 100n 等 SI 前缀'],
            ['输出', 'Top-K 等价类 + 串并联（SP）标注 + 电路图'],
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
              <td><input v-model="r.count" type="number" min="1" max="64" style="width: 80px" /></td>
              <td>
                <button class="btn sm ghost" type="button" :disabled="compRows.length <= 1" @click="compRows.splice(i, 1)">删除</button>
              </td>
            </tr>
          </tbody>
        </table>
        <div class="row tight">
          <button class="btn sm" type="button" @click="addRow"><Boxes />添加元件</button>
          <span class="qpill" :class="compTotal > 6 ? 'warn' : ''">{{ compSummary }} · 共 {{ compTotal }} 个</span>
          <span v-if="compTotal > 8" class="qpill crit">超过硬上限 8，无法运行</span>
        </div>
        <div v-if="compErrors.length" class="qpill crit" style="align-self: flex-start">{{ compErrors[0] }}</div>
        <div class="row">
          <button
            class="btn primary" type="button"
            :disabled="!points.length || running.try2 || !!compErrors.length || compTotal > 8 || compTotal < 1"
            @click="runTry2"
          >
            <Play />{{ running.try2 ? '计算中…' : '运行 Try 2' }}
          </button>
          <button v-if="running.try2" class="btn" type="button" @click="cancel('try2')"><Square />取消</button>
          <span v-if="elapsed.try2 && !running.try2" class="hint mono">耗时 {{ elapsed.try2.toFixed(2) }} s</span>
        </div>
        <div v-if="runError.try2" class="qpill crit" style="align-self: flex-start">{{ runError.try2 }}</div>
        <div v-if="activeStats2" class="hint">
          引擎报告：结构 {{ activeStats2.n_structures }} · 候选 {{ activeStats2.n_candidates }} · 漏斗保留 {{ activeStats2.n_funnel_kept }}
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
            ['自动减支', '并联同型合并 / 串联同型合并 / R 折入 L 的 DCR / 悬空支路删除'],
            ['输出', '单结果：wRMSE / AICc / 群参数 + 弱参数、触边界、Jacobi 秩诊断'],
          ]"
          :bullets="['输入方式参照 csacademy graph editor：绘制/拖动/编辑/删除四种模式，左侧边表可直接键入 u v R|L|C']"
        />
      </div>
      <div class="panel-body col">
        <GraphEditor v-model:edges="try3Edges" />
        <div class="row">
          <button class="btn primary" type="button" :disabled="!points.length || running.try3" @click="runTry3">
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
                <td class="num mono" :class="{ muted: c.aicc - minAicc >= 2 }">{{ fmt.fmt(c.aicc - minAicc, 2) }}</td>
                <td v-if="tab === 'try2'">
                  <span class="qpill" :class="c.sp ? 'good' : 'warn'">{{ c.sp ? 'SP' : '桥式' }}</span>
                </td>
                <td class="muted">
                  <template v-if="tab === 'try1'">
                    引擎 {{ c.engine }}<template v-if="(c.n_members ?? 1) > 1"> · 等价 ×{{ c.n_members }}</template>
                  </template>
                  <template v-else-if="tab === 'try2'">
                    <template v-if="(c.n_members ?? 1) > 1">等价 ×{{ c.n_members }}</template>
                    <span class="mono muted"> {{ c.structure }}</span>
                  </template>
                  <template v-else>确定性拟合 · 群 {{ c.devices }}</template>
                </td>
              </tr>
            </tbody>
          </table>
          <div class="hint" style="margin-top: 6px">点击行切换下方电路图与叠加曲线。</div>
        </div>
      </section>

      <!-- 电路图 -->
      <section class="panel">
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
            Jacobi 秩 {{ activeDiag3.jac_rank }} / {{ activeCandidate?.n_params }}
          </span>
          <span class="qpill" :class="activeDiag3.jac_cond > 1e6 ? 'warn' : ''">
            条件数 {{ activeDiag3.jac_cond >= 1e300 ? '∞' : activeDiag3.jac_cond.toExponential(1) }}
          </span>
          <span class="qpill">多起点 {{ activeDiag3.n_starts_used }} 次</span>
          <Network style="width: 14px; height: 14px; color: var(--text-3)" />
        </div>
        <div class="panel-body col">
          <table class="data">
            <thead>
              <tr><th>群</th><th>类型</th><th>节点对</th><th>聚合模式</th><th class="num">拟合数值</th><th>成员边</th><th>诊断</th></tr>
            </thead>
            <tbody>
              <tr v-for="g in activeDiag3.groups" :key="g.gid">
                <td class="mono">#{{ g.gid }}</td>
                <td><span class="qpill" :class="`k-${g.kind}`">{{ g.kind }}</span></td>
                <td class="mono">{{ g.u }} — {{ g.v }}</td>
                <td class="muted">{{ g.mode }}</td>
                <td class="num mono">
                  {{
                    g.kind === 'R' ? fmt.eng(g.value.v1, 'Ω', 4)
                    : g.kind === 'C' ? fmt.eng(g.value.v1, 'F', 4)
                    : `${fmt.eng(g.value.v1, 'H', 4)} + ${fmt.eng(g.value.v2, 'Ω', 3)} DCR`
                  }}
                </td>
                <td class="mono muted">{{ g.members.map((m: number) => m + 1).join(', ') }}</td>
                <td>
                  <span v-if="g.weak.length" class="qpill warn">弱参数</span>
                  <span v-if="g.at_bound.length" class="qpill warn">触边界</span>
                  <span v-if="!g.weak.length && !g.at_bound.length" class="qpill good">良好</span>
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
        数据已就绪（{{ points.length }} 点）—— 在上方「{{ { try1: 'Try 1 · 未知辨识', try2: 'Try 2 · 已知元件', try3: 'Try 3 · 已知拓扑' }[tab] }}」栏目点击运行。
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
