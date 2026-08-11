<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { storeToRefs } from 'pinia'
import { useScanStore } from '../store/scan'
import { useAppStore } from '../store/app'
import * as api from '../api'
import ScanBar from '../components/ScanBar.vue'
import EChart from '../components/EChart.vue'
import Markdown from '../components/Markdown.vue'
import { getPalette } from '../lib/palette'
import { bodeOpt, nyquistOpt } from '../lib/charts'
import * as fmt from '../lib/format'

const store = useScanStore()
const app = useAppStore()
const { measurements, currentId } = storeToRefs(store)
const p = computed(() => getPalette(app.theme))

const sweepIntro = String.raw`扫频视图把所有频率点的阻抗聚合：**幅频** $|Z|(f)$ 与 **相频** $\angle Z(f)$（对数横轴），以及 **Nyquist** 图（横轴 $\mathrm{Re}(Z)$、纵轴 $-\mathrm{Im}(Z)$，容性弧落在上半平面）。若该扫描已拟合，会叠加**理论曲线**便于对照。`

const latestFit = ref<api.FitOut | null>(null)
async function loadFit() {
  latestFit.value = null
  if (!currentId.value) return
  const fits = await api.listFits(currentId.value)
  latestFit.value = (fits[0] as api.FitOut) || null
}
watch(currentId, loadFit)

const measured = computed(() => measurements.value.map((m) => ({ f: m.frequency, mag: m.z_mag, phase: m.z_phase_deg, re: m.z_real, im: m.z_imag })))
const theory = computed(() => latestFit.value?.theory ?? null)

const magOpt = computed(() => bodeOpt(p.value, {
  mode: 'mag',
  measured: measured.value.map((m) => ({ f: m.f, v: m.mag })),
  theory: theory.value ? { f: theory.value.frequency, v: theory.value.z_mag } : undefined,
  yLabel: '|Z| (Ω)',
}))
const phaseOpt = computed(() => bodeOpt(p.value, {
  mode: 'phase',
  measured: measured.value.map((m) => ({ f: m.f, v: m.phase })),
  theory: theory.value ? { f: theory.value.frequency, v: theory.value.z_phase_deg } : undefined,
  yLabel: '相位 (°)',
}))
const nyqOpt = computed(() => nyquistOpt(p.value, {
  measured: measured.value.map((m) => ({ re: m.re, im: m.im })),
  theory: theory.value ? { re: theory.value.z_real, im: theory.value.z_imag } : undefined,
}))
</script>

<template>
  <div class="view">
    <ScanBar />
    <div v-if="!currentId" class="panel empty"><div class="big">⋀</div><div>选择一个扫描查看扫频结果。</div></div>
    <template v-else>
      <div class="panel"><div class="panel-body">
        <Markdown :source="sweepIntro" />
      </div></div>
      <div v-if="latestFit" class="panel panel-body row tight">
        <span class="muted hint">叠加最近拟合：</span>
        <span class="badge good">{{ latestFit.model }}</span>
        <span class="muted">RMSE {{ fmt.fmt(latestFit.rmse, 3) }} Ω</span>
        <span class="muted">精度 {{ fmt.pct(latestFit.accuracy, 2) }}</span>
        <span class="hint">（在「等效电路拟合」页可重新拟合）</span>
      </div>
      <div class="chart-card"><div class="ctitle">幅频特性 |Z|(f) <span class="cunit">对数横轴</span></div><EChart :option="magOpt" :height="300" /></div>
      <div class="chart-card"><div class="ctitle">相频特性 ∠Z(f) <span class="cunit">对数横轴</span></div><EChart :option="phaseOpt" :height="260" /></div>
      <div class="chart-card"><div class="ctitle">Nyquist 图 <span class="cunit">Re(Z) vs −Im(Z)</span></div><EChart :option="nyqOpt" :height="320" /></div>
    </template>
  </div>
</template>
