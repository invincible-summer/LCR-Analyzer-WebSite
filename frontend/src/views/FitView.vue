<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue'
import { storeToRefs } from 'pinia'
import { useScanStore } from '../store/scan'
import * as api from '../api'
import ScanBar from '../components/ScanBar.vue'
import StatTile from '../components/StatTile.vue'
import EChart from '../components/EChart.vue'
import FigBlock from '../components/FigBlock.vue'
import Latex from '../components/Latex.vue'
import Schematic from '../components/Schematic.vue'
import { Network, Download } from '@lucide/vue'
import { getPalette } from '../lib/palette'
import { bodeOpt, nyquistOpt, poleZeroOpt, residualsOpt } from '../lib/charts'
import { topologyToNetlist } from '../lib/modelTopologies'
import * as fmt from '../lib/format'

const store = useScanStore()
const { measurements, currentId } = storeToRefs(store)
const p = computed(() => getPalette())

const models = ref<api.ModelDef[]>([])
const selected = ref('auto')
const fit = ref<api.FitOut | null>(null)
const fitting = ref(false)
const error = ref('')
const fits = ref<api.FitSummary[]>([])
const showRanking = ref(true)

onMounted(async () => { models.value = await api.listModels() })
watch(currentId, async () => {
  fit.value = null
  if (currentId.value) fits.value = await api.listFits(currentId.value)
})

async function run() {
  if (!currentId.value) return
  fitting.value = true; error.value = ''
  try {
    fit.value = await api.runFit(currentId.value, selected.value)
    fits.value = await api.listFits(currentId.value)
  } catch (e: any) { error.value = e.message } finally { fitting.value = false }
}

async function loadHistory(f: api.FitSummary) {
  fit.value = await api.getFit(f.id)
}

function downloadSpice() {
  if (!fit.value?.spice) return
  const blob = new Blob([fit.value.spice], { type: 'text/plain' })
  const a = document.createElement('a')
  a.href = URL.createObjectURL(blob)
  a.download = `lcr_fit_${fit.value.id}.subckt`
  a.click()
  URL.revokeObjectURL(a.href)
}

const units: Record<string, string> = { R: 'Ω', Rs: 'Ω', Rp: 'Ω', L: 'H', C: 'F', d: 'Ω', e: 'H' }
const paramEntries = computed(() => Object.entries(fit.value?.params ?? {}))

const selectedModelDef = computed(() => models.value.find((m) => m.name === selected.value))
const modelTex = computed(() => selectedModelDef.value?.tex ?? '')

const measured = computed(() =>
  measurements.value.map((m) => ({
    f: m.frequency, mag: m.z_mag, phase: m.z_phase_deg,
    re: m.z_real, im: m.z_imag, sigma: m.z_sigma,
  })))
const theory = computed(() => fit.value?.theory ?? null)

const magOpt = computed(() => bodeOpt(p.value, {
  mode: 'mag',
  measured: measured.value.map((m) => ({ f: m.f, v: m.mag, sigma: m.sigma })),
  theory: theory.value ? { f: theory.value.frequency, v: theory.value.z_mag } : undefined,
  yLabel: '|Z| (Ω)', zoom: true, showSigma: true,
}))
const phaseOpt = computed(() => bodeOpt(p.value, {
  mode: 'phase',
  measured: measured.value.map((m) => ({ f: m.f, v: m.phase })),
  theory: theory.value ? { f: theory.value.frequency, v: theory.value.z_phase_deg } : undefined,
  yLabel: '相位 (°)', zoom: true,
}))
const nyqOpt = computed(() => nyquistOpt(p.value, {
  measured: measured.value.map((m) => ({ re: m.re, im: m.im })),
  theory: theory.value ? { re: theory.value.z_real, im: theory.value.z_imag } : undefined,
  zoom: true,
}))
const pzOpt = computed(() => {
  if (!fit.value?.poles) return {}
  const w = 2 * Math.PI
  const fmin = Math.min(...measurements.value.map((m) => m.frequency), 1)
  const fmax = Math.max(...measurements.value.map((m) => m.frequency), 10)
  return poleZeroOpt(p.value, {
    poles: fit.value.poles,
    zeros: fit.value.zeros ?? [],
    band: [w * fmin, w * fmax],
  })
})
const residOpt = computed(() => {
  const r = fit.value?.residuals
  if (!r) return {}
  return residualsOpt(p.value, {
    freqs: r.frequency,
    re: r.re, im: r.im,
    sigma: measurements.value.map((m) => m.z_sigma || 0),
  })
})

const rankingRows = computed(() => fit.value?.ranking ?? [])
const bestRow = computed(() => rankingRows.value.find((r) => r.selected) ?? rankingRows.value[0])

// Schematic tree: VF fits carry a synthesised Foster netlist; fixed-topology
// fits are mapped from their model name + fitted params.
const schematicTree = computed(() => {
  if (!fit.value) return null
  if (fit.value.netlist) return fit.value.netlist
  return topologyToNetlist(fit.value.model, fit.value.params)
})
</script>

<template>
  <div class="view">
    <ScanBar />

    <div v-if="!currentId" class="panel empty">
      <Network />
      <div>选择一个扫描后进行等效电路拟合。</div>
    </div>

    <template v-else>
      <!-- control -->
      <section class="panel">
        <div class="panel-body row">
          <label class="field" style="min-width: 300px">
            <span class="muted">拟合模式</span>
            <select v-model="selected" :disabled="fitting">
              <option v-for="m in models" :key="m.name" :value="m.name">{{ m.label }}</option>
            </select>
          </label>
          <button class="btn primary" @click="run" :disabled="fitting || !measurements.length">
            {{ fitting ? '拟合中…' : '运行拟合' }}
          </button>
          <span class="hint" v-if="!measurements.length">该扫描没有测量点。</span>
          <div class="spacer" style="flex:1" />
          <div class="eq-block" style="margin:0; min-width: 220px" v-if="modelTex">
            <Latex :tex="modelTex" :display="true" />
          </div>
        </div>
      </section>

      <div v-if="error" class="panel panel-body mono" style="color: var(--critical)">{{ error }}</div>

      <template v-if="fit">
        <!-- headline metrics -->
        <section class="panel">
          <div class="panel-head">
            <h3>拟合结果</h3>
            <span class="tag mono">{{ fit.model }} · {{ fit.kind === 'vf' ? '矢量拟合 + Foster 综合' : '固定拓扑' }}</span>
            <div class="spacer" />
            <span class="badge" :class="fit.passive === false ? 'warn' : 'good'">
              {{ fit.passive === false ? '非无源（结果仅供参考）' : '无源可实现' }}
            </span>
            <span class="badge" v-if="!fit.converged">未收敛</span>
          </div>
          <div class="panel-body">
            <div class="stat-grid cols-4">
              <StatTile k="χ²_red" :v="fmt.fmt(fit.chi2_red, 3)" sub="≈1 表示残差即噪声水平" accent />
              <StatTile k="AICc" :v="fmt.fmt(fit.aicc, 1)" sub="越小越好（跨模型可比）" />
              <StatTile k="RMSE" :v="fmt.fmt(fit.rmse, 3)" unit="Ω" />
              <StatTile
                v-if="bestRow"
                k="候选排名"
                :v="`#1 / ${rankingRows.length}`"
                :sub="bestRow.label"
              />
            </div>

            <!-- warnings -->
            <div v-if="fit.warnings?.length" class="col" style="margin-top: 12px; gap: 4px">
              <div v-for="w in fit.warnings" :key="w" class="hint" style="color: var(--warning)">⚠ {{ w }}</div>
            </div>

            <!-- equivalent circuit schematic (VF netlist or fixed topology) -->
            <template v-if="schematicTree">
              <div style="margin-top: 16px" class="row tight">
                <h4 style="margin: 0">{{ fit.kind === 'vf' ? '综合等效电路（Foster 形式）' : '等效电路拓扑' }}</h4>
                <div class="spacer" style="flex:1"></div>
                <button v-if="fit.kind === 'vf' && fit.netlist" class="btn sm" @click="downloadSpice">
                  <Download /> 下载 SPICE .subckt
                </button>
              </div>
              <div class="panel" style="margin-top: 8px; background: var(--panel-2)">
                <div class="panel-body schematic-wrap">
                  <Schematic :netlist="schematicTree" />
                </div>
              </div>
            </template>

            <!-- parameters (+ CI for topology fits) -->
            <table class="data" style="margin-top: 16px" v-if="paramEntries.length">
              <thead>
                <tr>
                  <th>参数</th><th class="num">数值</th><th class="num">95% 置信区间</th><th>单位</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="[k, v] in paramEntries" :key="k">
                  <td class="mono">{{ k }}</td>
                  <td class="num mono">{{ fmt.fmt(v, 4) }}</td>
                  <td class="num mono" v-if="fit.param_ci && fit.param_ci[k]">
                    {{ fmt.fmt(fit.param_ci[k][0], 3) }} … {{ fmt.fmt(fit.param_ci[k][1], 3) }}
                  </td>
                  <td v-else class="muted">—</td>
                  <td class="muted">{{ units[k] ?? '' }}</td>
                </tr>
              </tbody>
            </table>
            <div class="hint" style="margin-top: 6px" v-if="fit.kind === 'vf'">
              d = 高频串联电阻，e = 串联电感；各极点支路元件值见上方原理图标注。
            </div>
          </div>
        </section>

        <!-- charts -->
        <FigBlock no="Fig. 1" title="幅频特性 |Z|(f) — 测量 vs 拟合" unit="误差棒 = ±1σ">
          <EChart :option="magOpt" :height="320" /></FigBlock>
        <FigBlock no="Fig. 2" title="相频特性 ∠Z(f) — 测量 vs 拟合">
          <EChart :option="phaseOpt" :height="260" /></FigBlock>
        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 16px">
          <FigBlock no="Fig. 3" title="Nyquist 图" unit="Re(Z) vs −Im(Z)">
            <EChart :option="nyqOpt" :height="320" /></FigBlock>
          <FigBlock v-if="fit.poles?.length" no="Fig. 4" title="s 平面极零图"
            unit="× 极点 · ○ 零点"
            caption="极点全部位于左半平面 = 稳定（无源）网络；极点在 jω 轴上的位置对应各谐振/转折频率。">
            <EChart :option="pzOpt" :height="320" /></FigBlock>
        </div>
        <FigBlock v-if="fit.residuals" no="Fig. 5" title="拟合残差 vs 频率"
          unit="虚线 = ±1σ"
          caption="残差应随机分布在 ±1σ 带内；系统性趋势说明模型阶数不足或存在未被校准的系统误差。">
          <EChart :option="residOpt" :height="240" /></FigBlock>

        <!-- model ranking (auto mode) -->
        <section class="panel" v-if="rankingRows.length">
          <div class="panel-head">
            <h3>候选模型排名</h3>
            <span class="tag">AICc 统一加权残差 · ΔAICc ≤ 2 视为不可区分</span>
            <div class="spacer" />
            <label class="row tight hint" style="cursor: pointer">
              <input type="checkbox" v-model="showRanking" style="width: auto" /> 显示
            </label>
          </div>
          <div class="panel-body" v-if="showRanking">
            <table class="data">
              <thead>
                <tr>
                  <th>#</th><th>模型</th><th>类型</th><th class="num">参数数</th>
                  <th class="num">χ²_red</th><th class="num">AICc</th><th class="num">ΔAICc</th><th></th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="r in rankingRows" :key="r.model" :style="r.selected ? 'font-weight:600' : ''">
                  <td class="mono">{{ r.rank }}</td>
                  <td>{{ r.label }}</td>
                  <td class="muted">{{ r.kind === 'vf' ? '矢量拟合' : '拓扑' }}</td>
                  <td class="num mono">{{ r.n_params }}</td>
                  <td class="num mono">{{ fmt.fmt(r.chi2_red, 2) }}</td>
                  <td class="num mono">{{ fmt.fmt(r.aicc, 1) }}</td>
                  <td class="num mono">{{ fmt.fmt(r.delta_aicc, 1) }}</td>
                  <td>{{ r.selected ? '✓ 选用' : '' }}</td>
                </tr>
              </tbody>
            </table>
          </div>
        </section>
      </template>

      <!-- history -->
      <section class="panel">
        <div class="panel-head"><h3>拟合历史</h3></div>
        <div class="panel-body">
          <table class="data" v-if="fits.length">
            <thead>
              <tr><th>ID</th><th>模型</th><th>类型</th><th class="num">χ²_red</th><th class="num">AICc</th><th>时间</th><th></th></tr>
            </thead>
            <tbody>
              <tr v-for="f in fits" :key="f.id">
                <td class="mono">#{{ f.id }}</td>
                <td>{{ f.model }}</td>
                <td class="muted">{{ f.kind === 'vf' ? '矢量拟合' : '拓扑' }}</td>
                <td class="num mono">{{ fmt.fmt(f.chi2_red, 2) }}</td>
                <td class="num mono">{{ fmt.fmt(f.aicc, 1) }}</td>
                <td class="muted">{{ new Date(f.created_at).toLocaleString() }}</td>
                <td><button class="btn sm" @click="loadHistory(f)">查看</button></td>
              </tr>
            </tbody>
          </table>
          <div v-else class="hint">暂无拟合记录。</div>
        </div>
      </section>
    </template>
  </div>
</template>
