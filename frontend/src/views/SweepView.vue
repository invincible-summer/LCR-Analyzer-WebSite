<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { storeToRefs } from 'pinia'
import { useScanStore } from '../store/scan'
import * as api from '../api'
import ScanBar from '../components/ScanBar.vue'
import EChart from '../components/EChart.vue'
import FigBlock from '../components/FigBlock.vue'
import { LineChart } from '@lucide/vue'
import { getPalette } from '../lib/palette'
import { bodeOpt, nyquistOpt } from '../lib/charts'
import * as fmt from '../lib/format'

const store = useScanStore()
const { measurements, currentId } = storeToRefs(store)
const p = computed(() => getPalette())

const latestFit = ref<api.FitOut | null>(null)
async function loadFit() {
  latestFit.value = null
  if (!currentId.value) return
  const fits = await api.listFits(currentId.value)
  if (fits.length) latestFit.value = await api.getFit(fits[0].id)
}
watch(currentId, loadFit)

const measured = computed(() =>
  measurements.value.map((m) => ({
    f: m.frequency, mag: m.z_mag, phase: m.z_phase_deg,
    re: m.z_real, im: m.z_imag, sigma: m.z_sigma,
  })),
)
const theory = computed(() => latestFit.value?.theory ?? null)

const magOpt = computed(() => bodeOpt(p.value, {
  mode: 'mag',
  measured: measured.value.map((m) => ({ f: m.f, v: m.mag, sigma: m.sigma })),
  theory: theory.value ? { f: theory.value.frequency, v: theory.value.z_mag } : undefined,
  yLabel: '|Z| (Ω)',
  zoom: true,
  showSigma: true,
}))
const phaseOpt = computed(() => bodeOpt(p.value, {
  mode: 'phase',
  measured: measured.value.map((m) => ({ f: m.f, v: m.phase })),
  theory: theory.value ? { f: theory.value.frequency, v: theory.value.z_phase_deg } : undefined,
  yLabel: '相位 (°)',
  zoom: true,
}))
const nyqOpt = computed(() => nyquistOpt(p.value, {
  measured: measured.value.map((m) => ({ re: m.re, im: m.im })),
  theory: theory.value ? { re: theory.value.z_real, im: theory.value.z_imag } : undefined,
  zoom: true,
}))
</script>

<template>
  <div class="view">
    <ScanBar />
    <div v-if="!currentId" class="panel empty">
      <LineChart />
      <div>选择一个扫描查看扫频结果。</div>
    </div>
    <template v-else>
      <div v-if="latestFit" class="panel panel-body row tight">
        <span class="muted hint">叠加最近拟合：</span>
        <span class="badge">{{ latestFit.model }} · {{ latestFit.kind === 'vf' ? '矢量拟合' : '固定拓扑' }}</span>
        <span class="muted mono">χ²_red {{ fmt.fmt(latestFit.chi2_red, 2) }}</span>
        <span class="muted mono">RMSE {{ fmt.fmt(latestFit.rmse, 3) }} Ω</span>
        <span class="hint">（在「等效电路拟合」页可重新拟合）</span>
      </div>
      <FigBlock no="Fig. 1" title="幅频特性 |Z|(f)" unit="对数横轴 · 误差棒 = ±1σ"
        caption="实心点为测量值（每点由该频率下的时域正弦拟合得到），细竖线为双通道残差传播出的 1σ 不确定度；橙色曲线为最近一次拟合的理论模型。">
        <EChart :option="magOpt" :height="320" /></FigBlock>
      <FigBlock no="Fig. 2" title="相频特性 ∠Z(f)" unit="对数横轴">
        <EChart :option="phaseOpt" :height="260" /></FigBlock>
      <FigBlock no="Fig. 3" title="Nyquist 图" unit="Re(Z) − (−Im(Z))"
        caption="容性弧落在上半平面（电化学惯例）。曲线为拟合模型，散点为测量。">
        <EChart :option="nyqOpt" :height="340" /></FigBlock>
    </template>
  </div>
</template>
