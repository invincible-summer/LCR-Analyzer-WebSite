<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue'
import { storeToRefs } from 'pinia'
import { useScanStore } from '../store/scan'
import { useAppStore } from '../store/app'
import * as api from '../api'
import ScanBar from '../components/ScanBar.vue'
import StatTile from '../components/StatTile.vue'
import EChart from '../components/EChart.vue'
import Latex from '../components/Latex.vue'
import { getPalette } from '../lib/palette'
import { bodeOpt, nyquistOpt } from '../lib/charts'
import * as fmt from '../lib/format'

const store = useScanStore()
const app = useAppStore()
const { measurements, currentId } = storeToRefs(store)
const p = computed(() => getPalette(app.theme))

const models = ref<api.ModelDef[]>([])
const selected = ref('series_RLC')
const fit = ref<api.FitOut | null>(null)
const fitting = ref(false)
const error = ref('')
const fits = ref<api.FitSummary[]>([])

onMounted(async () => { models.value = await api.listModels() })
watch(currentId, async () => { fit.value = null; if (currentId.value) fits.value = await api.listFits(currentId.value) })

async function run() {
  if (!currentId.value) return
  fitting.value = true; error.value = ''
  try {
    fit.value = await api.runFit(currentId.value, selected.value)
    fits.value = await api.listFits(currentId.value)
  } catch (e: any) { error.value = e.message } finally { fitting.value = false }
}

const units: Record<string, string> = { R: 'Ω', L: 'H', C: 'F' }

const MODEL_TEX: Record<string, string> = {
  series_RLC: String.raw`Z = R + j\left(\omega L - \frac{1}{\omega C}\right)`,
  series_RC: String.raw`Z = R - \frac{j}{\omega C}`,
  series_RL: String.raw`Z = R + j\omega L`,
  parallel_RLC: String.raw`Z = \cfrac{1}{\dfrac{1}{R} + j\left(\omega C - \dfrac{1}{\omega L}\right)}`,
  parallel_RC: String.raw`Z = \cfrac{1}{\dfrac{1}{R} + j\omega C}`,
  parallel_RL: String.raw`Z = \cfrac{1}{\dfrac{1}{R} - \dfrac{j}{\omega L}}`,
}
const modelTex = computed(() => MODEL_TEX[selected.value] ?? '')
const f0 = computed(() => {
  const P = fit.value?.params
  if (!P || P.L == null || P.C == null) return null
  return 1 / (2 * Math.PI * Math.sqrt(P.L * P.C))
})
const Qres = computed(() => {
  const P = fit.value?.params
  if (!P || P.R == null || P.L == null || P.C == null || P.R === 0) return null
  return Math.sqrt(P.L / P.C) / P.R
})
const accClass = computed(() => {
  const a = fit.value?.accuracy ?? 0
  if (a > 0.99) return 'good'
  if (a > 0.95) return 'warn'
  return 'crit'
})

const measured = computed(() => measurements.value.map((m) => ({ f: m.frequency, mag: m.z_mag, phase: m.z_phase_deg, re: m.z_real, im: m.z_imag })))
const magOpt = computed(() => fit.value ? bodeOpt(p.value, { mode: 'mag', measured: measured.value.map((m) => ({ f: m.f, v: m.mag })), theory: { f: fit.value.theory.frequency, v: fit.value.theory.z_mag }, yLabel: '|Z| (Ω)' }) : {})
const phaseOpt = computed(() => fit.value ? bodeOpt(p.value, { mode: 'phase', measured: measured.value.map((m) => ({ f: m.f, v: m.phase })), theory: { f: fit.value.theory.frequency, v: fit.value.theory.z_phase_deg }, yLabel: '相位 (°)' }) : {})
const nyqOpt = computed(() => fit.value ? nyquistOpt(p.value, { measured: measured.value.map((m) => ({ re: m.re, im: m.im })), theory: { re: fit.value.theory.z_real, im: fit.value.theory.z_imag } }) : {})
</script>

<template>
  <div class="view">
    <ScanBar />
    <div v-if="!currentId" class="panel empty"><div class="big">Σ</div><div>选择一个扫描后选择等效电路模型进行拟合。</div></div>
    <template v-else>
      <section class="panel">
        <div class="panel-head"><h3>等效电路拟合</h3><span class="tag">scipy.optimize.least_squares · 实部+虚部联合 · log 空间</span><div class="spacer" /></div>
        <div class="panel-body row tight">
          <label class="field"><span class="muted">模型</span>
            <select v-model="selected">
              <option v-for="m in models" :key="m.name" :value="m.name">{{ m.label }} ({{ m.params.join('·') }})</option>
            </select>
          </label>
          <button class="btn primary" @click="run" :disabled="fitting || !measurements.length">{{ fitting ? '拟合中…' : '▶ 开始拟合' }}</button>
          <span v-if="error" style="color:var(--critical)">{{ error }}</span>
        </div>
        <div class="eq-block" style="margin:0 18px 18px">
          <div class="eq-tag">所选模型的阻抗表达式</div>
          <Latex :tex="modelTex" :display="true" />
        </div>
      </section>

      <div v-if="fit" class="panel">
        <div class="panel-head"><h3>拟合结果 · {{ fit.model }}</h3>
          <span class="qpill" :class="accClass">精度 {{ fmt.pct(fit.accuracy, 2) }}</span>
          <span class="hint" style="margin-left:8px">RMSE {{ fmt.fmt(fit.rmse, 3) }} Ω</span>
          <div class="spacer" /></div>
        <div class="panel-body">
          <div class="stat-grid cols-4">
            <StatTile v-for="(val, key) in fit.params" :key="key" :k="String(key)" :v="fmt.eng(val, units[key] || '', 4)" accent />
            <StatTile v-if="f0 != null" k="谐振频率 f₀" :v="fmt.fmtHz(f0)" sub="1/(2π√LC)" />
            <StatTile v-if="Qres != null" k="谐振 Q" :v="fmt.fmt(Qres, 3)" sub="√(L/C)/R" />
            <StatTile k="数据点数" :v="measurements.length" />
          </div>
        </div>
      </div>

      <template v-if="fit">
        <div class="chart-card"><div class="ctitle">|Z|(f) · 测量点 + 拟合曲线</div><EChart :option="magOpt" :height="300" /></div>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
          <div class="chart-card"><div class="ctitle">∠Z(f) · 测量点 + 拟合曲线</div><EChart :option="phaseOpt" :height="280" /></div>
          <div class="chart-card"><div class="ctitle">Nyquist · 测量点 + 拟合曲线</div><EChart :option="nyqOpt" :height="280" /></div>
        </div>
      </template>

      <section v-if="fits.length" class="panel">
        <div class="panel-head"><h3>历史拟合</h3><div class="spacer" /></div>
        <table class="data">
          <thead><tr><th>模型</th><th class="num">RMSE</th><th class="num">精度</th><th>参数</th><th>时间</th></tr></thead>
          <tbody>
            <tr v-for="f in fits" :key="f.id">
              <td>{{ f.model }}</td>
              <td class="num mono">{{ fmt.fmt(f.rmse, 3) }} Ω</td>
              <td class="num mono">{{ fmt.pct(f.accuracy, 2) }}</td>
              <td class="mono hint">{{ Object.entries(f.params).map(([k, v]) => `${k}=${fmt.eng(v, units[k] || '', 3)}`).join('  ') }}</td>
              <td class="hint">{{ new Date(f.created_at).toLocaleString() }}</td>
            </tr>
          </tbody>
        </table>
      </section>
    </template>
  </div>
</template>
