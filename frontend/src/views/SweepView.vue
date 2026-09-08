<script setup lang="ts">
import { computed } from 'vue'
import { storeToRefs } from 'pinia'
import { useScanStore } from '../store/scan'
import ScanBar from '../components/ScanBar.vue'
import EChart from '../components/EChart.vue'
import FigBlock from '../components/FigBlock.vue'
import { LineChart } from '@lucide/vue'
import { getPalette } from '../lib/palette'
import { bodeOpt, nyquistOpt } from '../lib/charts'

const store = useScanStore()
const { measurements, currentId } = storeToRefs(store)
const p = computed(() => getPalette())

const measured = computed(() =>
  measurements.value.map((m) => ({
    f: m.frequency, mag: m.z_mag, phase: m.z_phase_deg,
    re: m.z_real, im: m.z_imag, sigma: m.z_sigma,
  })),
)

const magOpt = computed(() => bodeOpt(p.value, {
  mode: 'mag',
  measured: measured.value.map((m) => ({ f: m.f, v: m.mag, sigma: m.sigma })),
  yLabel: '|Z| (Ω)',
  zoom: true,
  showSigma: true,
}))
const phaseOpt = computed(() => bodeOpt(p.value, {
  mode: 'phase',
  measured: measured.value.map((m) => ({ f: m.f, v: m.phase })),
  yLabel: '相位 (°)',
  zoom: true,
}))
const nyqOpt = computed(() => nyquistOpt(p.value, {
  measured: measured.value.map((m) => ({ re: m.re, im: m.im })),
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
      <FigBlock no="Fig. 1" title="幅频特性 |Z|(f)" unit="对数横轴 · 误差棒 = ±1σ"
        caption="实心点为测量值（每点由该频率下的时域正弦拟合得到），细竖线为双通道残差传播出的 1σ 不确定度。">
        <EChart :option="magOpt" :height="320" /></FigBlock>
      <FigBlock no="Fig. 2" title="相频特性 ∠Z(f)" unit="对数横轴">
        <EChart :option="phaseOpt" :height="260" /></FigBlock>
      <FigBlock no="Fig. 3" title="Nyquist 图" unit="Re(Z) − (−Im(Z))"
        caption="容性弧落在上半平面（电化学惯例）。散点为测量结果。">
        <EChart :option="nyqOpt" :height="340" /></FigBlock>
    </template>
  </div>
</template>
