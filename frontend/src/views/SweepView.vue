<script setup lang="ts">
// SweepView.vue — 扫频曲线页
//
// 两个数据源（互相独立，见 frontend/src/store/device.ts）：
//   1. 服务器历史扫描（原 ScanBar 流程，|Z|/∠Z/Nyquist）；
//   2. BLE 设备的双端口数据集（lcr-h-csv-v1）：真源是复数 H = Vout/Vin，
//      Bode gain/phase 与 Nyquist 全部由复 H 推导（plan.md §5.3/§7.3），
//      不把 BLE 数据伪装成后端 scan id。
import { computed } from 'vue'
import { storeToRefs } from 'pinia'
import { useScanStore } from '../store/scan'
import { useDeviceStore } from '../store/device'
import ScanBar from '../components/ScanBar.vue'
import EChart from '../components/EChart.vue'
import FigBlock from '../components/FigBlock.vue'
import { LineChart, Bluetooth, Download } from '@lucide/vue'
import { getPalette } from '../lib/palette'
import { bodeOpt, nyquistOpt } from '../lib/charts'
import { parseHCsv, type TwoPortParseResult } from '../lib/twoPortCsv'

const store = useScanStore()
const { measurements, currentId } = storeToRefs(store)
const device = useDeviceStore()
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

// ---------------------------------------------------------------------------
// BLE 设备双端口数据集
// ---------------------------------------------------------------------------
const deviceH = computed<TwoPortParseResult | null>(() => {
  const ds = device.dataset
  if (!ds || ds.kind !== 'TWO_PORT_H') return null
  return parseHCsv(ds.csvText)
})

const deviceBusyLabel = computed(() => {
  switch (device.phase) {
    case 'chooser': return '选择设备…'
    case 'connecting': return '连接中…'
    case 'receiving': return `接收中 ${Math.round(device.progress * 100)}%`
    case 'validating': return '校验中…'
    case 'complete': return '再次导入设备曲线'
    default: return '导入设备曲线（BLE）'
  }
})

async function importDevice() {
  const ds = await device.importFromDevice()
  if (!ds) return
  if (ds.kind !== 'TWO_PORT_H') {
    // 单端口数据集与本页曲线语义不符：明确提示去拟合页（不静默丢弃）
    device.phase = 'error'
    device.error = '收到的是单端口阻抗数据集（ONE_PORT_Z）：请在「电路辨识拟合」页蓝牙导入'
    return
  }
  const r = parseHCsv(ds.csvText)
  if (r.errors.length) {
    device.phase = 'error'
    device.error = `设备 CSV 非法：${r.errors[0]}`
  }
}

function saveDeviceCsv() {
  const ds = device.dataset
  if (!ds) return
  const blob = new Blob([ds.csvText], { type: 'text/csv' })
  const a = document.createElement('a')
  a.href = URL.createObjectURL(blob)
  a.download = `lcr-two-port-h-${ds.metadata.session_id}.csv`
  a.click()
  URL.revokeObjectURL(a.href)
}

const hGainOpt = computed(() => {
  const r = deviceH.value
  if (!r) return null
  return bodeOpt(p.value, {
    mode: 'mag',
    measured: r.points.map((q) => ({ f: q.f, v: q.gainDb })),
    yLabel: '增益 20log₁₀|H| (dB)',
    zoom: true,
  })
})
const hPhaseOpt = computed(() => {
  const r = deviceH.value
  if (!r) return null
  return bodeOpt(p.value, {
    mode: 'phase',
    measured: r.points.map((q) => ({ f: q.f, v: q.phaseDeg })),
    yLabel: '相位 arg(H) (°)',
    zoom: true,
  })
})
const hNyqOpt = computed(() => {
  const r = deviceH.value
  if (!r) return null
  return nyquistOpt(p.value, {
    measured: r.points.map((q) => ({ re: q.re, im: q.im })),
    zoom: true,
  })
})
</script>

<template>
  <div class="view">
    <!-- BLE 设备双端口曲线（本地 device store，与服务器扫描分离） -->
    <section class="panel">
      <div class="panel-head">
        <span class="tag">DEVICE</span>
        <h3>BLE 设备 · 双端口传递函数</h3>
        <div class="spacer" />
        <button
          class="btn" type="button"
          :disabled="!device.supported || device.busy"
          :title="device.supported
            ? '连接 LCR 设备（BLE），接收封存的双端口 lcr-h-csv-v1 数据集'
            : '当前浏览器不支持 Web Bluetooth：需要桌面 Chrome/Edge 且 HTTPS 或 localhost'"
          @click="importDevice"
        >
          <Bluetooth />{{ deviceBusyLabel }}
        </button>
        <button
          v-if="device.dataset?.kind === 'TWO_PORT_H'" class="btn ghost sm" type="button"
          @click="saveDeviceCsv"
        >
          <Download />保存设备 CSV
        </button>
      </div>
      <div class="panel-body col">
        <span v-if="!device.supported" class="hint">
          此浏览器不支持 Web Bluetooth（需 Chrome/Edge + HTTPS/localhost），设备导入功能不可用。
        </span>
        <span v-else-if="device.phase === 'error'" class="qpill crit" :title="device.error">{{ device.error }}</span>
        <template v-if="deviceH && deviceH.points.length">
          <div class="row tight">
            <span class="badge">
              设备 · fw {{ deviceH.headers['firmware'] || device.dataset?.metadata.firmware }}
              · {{ deviceH.points.length }} 点
              · cal {{ deviceH.headers['calibration_id'] || device.dataset?.metadata.calibration_id }}
            </span>
            <span v-for="w in deviceH.warnings" :key="w" class="qpill warn">{{ w }}</span>
          </div>
          <div class="preview-grid">
            <FigBlock no="H.1" title="设备幅频 20log₁₀|H|(f)" unit="H = Vout/Vin · 对数横轴">
              <EChart v-if="hGainOpt" :option="hGainOpt" :height="260" /></FigBlock>
            <FigBlock no="H.2" title="设备相频 arg(H)(f)" unit="由复 H 推导">
              <EChart v-if="hPhaseOpt" :option="hPhaseOpt" :height="260" /></FigBlock>
            <FigBlock no="H.3" title="设备 Nyquist（复 H 平面）" unit="Re(H) − Im(H)">
              <EChart v-if="hNyqOpt" :option="hNyqOpt" :height="260" /></FigBlock>
          </div>
        </template>
      </div>
    </section>

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
