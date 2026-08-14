<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { storeToRefs } from 'pinia'
import { useScanStore } from '../store/scan'
import { useAppStore } from '../store/app'
import * as api from '../api'
import ScanBar from '../components/ScanBar.vue'
import PanelStage from '../components/PanelStage.vue'
import FigBlock from '../components/FigBlock.vue'
import { ActivitySquare } from '@lucide/vue'
import StatTile from '../components/StatTile.vue'
import EChart from '../components/EChart.vue'
import Latex from '../components/Latex.vue'
import { getPalette } from '../lib/palette'
import { waveformOpt, spectrumOpt, complexPointOpt } from '../lib/charts'
import * as fmt from '../lib/format'

const store = useScanStore()
const app = useAppStore()
const { detail, measurements, currentId } = storeToRefs(store)

const idx = ref(0)
const md = ref<api.MeasurementDetail | null>(null)
const mdLoading = ref(false)
const mdError = ref('')

const cur = computed<api.Measurement | null>(() => measurements.value[idx.value] || null)

async function loadDetail() {
  md.value = null
  mdError.value = ''
  if (!cur.value || !currentId.value) return
  mdLoading.value = true
  try {
    md.value = await api.getMeasurement(currentId.value, cur.value.id)
  } catch (e: any) {
    mdError.value = e.message
  } finally {
    mdLoading.value = false
  }
}

watch(currentId, () => { idx.value = 0; loadDetail() })
watch(idx, loadDetail)
watch(measurements, () => {
  if (idx.value >= measurements.value.length) idx.value = Math.max(0, measurements.value.length - 1)
  loadDetail()
})

const p = computed(() => getPalette())
const zip = (a: number[], b: number[]) => a.map((x, i) => [x, b[i]] as [number, number])
const dcMark = (dc: number) => ({ silent: true, symbol: 'none', lineStyle: { color: p.value.text3, type: 'dotted', width: 1 }, label: { color: p.value.text3, fontSize: 10, formatter: 'DC' }, data: [{ yAxis: dc }] })
const zeroMark = () => ({ silent: true, symbol: 'none', lineStyle: { color: p.value.baseline, type: 'dashed', width: 1 }, data: [{ yAxis: 0 }] })

const tMs = computed(() => (md.value ? md.value.time.map((t) => t * 1000) : []))
const fs = computed(() => (cur.value ? 1 / cur.value.dt : 0))

// ---- stage 1: time domain ----
const vOpt = computed(() => {
  if (!md.value) return {}
  return waveformOpt(p.value, {
    series: [
      { name: '原始 u', data: zip(tMs.value, md.value.voltage), color: p.value.series[0], kind: 'scatter' },
      { name: '正弦拟合', data: zip(tMs.value, md.value.fitted_voltage), color: p.value.series[0], kind: 'line', markLine: dcMark(md.value.v_dc) },
    ],
    xLabel: '时间 (ms)', yLabel: 'u (V)',
    yFormatter: (v) => fmt.fmt(v, 3),
  })
})
const iOpt = computed(() => {
  if (!md.value) return {}
  return waveformOpt(p.value, {
    series: [
      { name: '原始 i', data: zip(tMs.value, md.value.current), color: p.value.series[1], kind: 'scatter' },
      { name: '正弦拟合', data: zip(tMs.value, md.value.fitted_current), color: p.value.series[1], kind: 'line', markLine: dcMark(md.value.i_dc) },
    ],
    xLabel: '时间 (ms)', yLabel: 'i (A)',
    yFormatter: (v) => fmt.fmt(v, 3),
  })
})
const normOpt = computed(() => {
  if (!md.value) return {}
  const va = md.value.v_amp || 1e-12
  const ia = md.value.i_amp || 1e-12
  return waveformOpt(p.value, {
    series: [
      { name: '(u−u_DC)/u₀', data: zip(tMs.value, md.value.voltage.map((x) => (x - md.value!.v_dc) / va)), color: p.value.series[0], kind: 'line' },
      { name: '(i−i_DC)/i₀', data: zip(tMs.value, md.value.current.map((x) => (x - md.value!.i_dc) / ia)), color: p.value.series[1], kind: 'line' },
    ],
    xLabel: '时间 (ms)', yLabel: '归一化幅度',
  })
})

// ---- stage 2: residuals ----
const snrV = computed(() => md.value ? 20 * Math.log10((md.value.v_amp || 1e-12) / (md.value.resid_rms_v || 1e-12)) : 0)
const snrI = computed(() => md.value ? 20 * Math.log10((md.value.i_amp || 1e-12) / (md.value.resid_rms_i || 1e-12)) : 0)
const residDiag = computed(() => {
  const s = snrV.value
  if (s > 45) return { t: '残差极小，信噪比高，u 接近纯正弦', cls: 'good' }
  if (s > 30) return { t: '残差较小，拟合良好', cls: 'good' }
  if (s > 20) return { t: '存在一定噪声或弱谐波，拟合仍有效', cls: 'warn' }
  return { t: '残差偏大：可能含明显谐波（u 非纯正弦）或强噪声', cls: 'crit' }
})
const residVOpt = computed(() => {
  if (!md.value) return {}
  return waveformOpt(p.value, {
    series: [{ name: 'u 残差', data: zip(tMs.value, md.value.resid_v), color: p.value.series[0], kind: 'line', width: 1, markLine: zeroMark() }],
    xLabel: '时间 (ms)', yLabel: 'Δu (V)', yFormatter: (v) => fmt.fmt(v, 2),
  })
})
const residIOpt = computed(() => {
  if (!md.value) return {}
  return waveformOpt(p.value, {
    series: [{ name: 'i 残差', data: zip(tMs.value, md.value.resid_i), color: p.value.series[1], kind: 'line', width: 1, markLine: zeroMark() }],
    xLabel: '时间 (ms)', yLabel: 'Δi (A)', yFormatter: (v) => fmt.fmt(v, 2),
  })
})

// ---- stage 3: spectrum ----
const specVOpt = computed(() => {
  if (!md.value) return {}
  return spectrumOpt(p.value, {
    series: [{ name: '|U(f)|', freqs: md.value.fft_freqs, mag: md.value.fft_mag_v, color: p.value.series[0], area: true }],
    excitation: cur.value?.frequency, yLabel: '电压幅值 (V)',
  })
})
const specIOpt = computed(() => {
  if (!md.value) return {}
  return spectrumOpt(p.value, {
    series: [{ name: '|I(f)|', freqs: md.value.fft_freqs, mag: md.value.fft_mag_i, color: p.value.series[1], area: true }],
    excitation: cur.value?.frequency, yLabel: '电流幅值 (A)',
  })
})

// ---- stage 4: impedance point ----
const typeTag = computed(() => {
  if (!cur.value) return ''
  const X = cur.value.X
  const rel = Math.abs(X) / (cur.value.z_mag || 1e-12)
  if (rel < 0.05) return '阻性 Resistive'
  return X < 0 ? '容性 Capacitive' : '感性 Inductive'
})
const cpOpt = computed(() => {
  if (!cur.value) return {}
  return complexPointOpt(p.value, { re: cur.value.z_real, im: cur.value.z_imag })
})
</script>

<template>
  <div class="view">
    <ScanBar />

    <div v-if="!currentId" class="panel empty">
      <ActivitySquare />
      <div>请在上方选择一个扫描，或点击「生成示例」合成一组数据。</div>
      <div class="hint">ESP32 固件就绪后，按数据契约 POST 到 /api/scan/{id}/point 即可在此分析。</div>
    </div>

    <template v-else-if="measurements.length">
      <!-- control: frequency picker + meta -->
      <section class="panel">
        <div class="panel-body col" style="gap:14px">
          <div class="freq-picker">
            <span class="muted hint">频率选择</span>
            <input type="range" min="0" :max="Math.max(0, measurements.length - 1)" step="1" v-model.number="idx" />
            <span class="freq-readout">{{ fmt.fmtHz(cur?.frequency ?? 0) }}</span>
            <button class="btn sm" @click="idx = Math.max(0, idx - 1)">‹</button>
            <button class="btn sm" @click="idx = Math.min(measurements.length - 1, idx + 1)">›</button>
            <span class="muted hint">点 {{ idx + 1 }} / {{ measurements.length }}</span>
          </div>
          <div class="stat-grid cols-4">
            <StatTile k="激励频率 f" :v="fmt.fmtHz(cur?.frequency ?? 0)" />
            <StatTile k="采样间隔 dt" :v="fmt.fmtTime(cur?.dt ?? 0)" />
            <StatTile k="采样率 fs" :v="fmt.eng(fs, 'Hz')" />
            <StatTile k="样本数 N" :v="(cur?.n ?? 0) + 1" />
          </div>
        </div>
      </section>

      <div v-if="mdError" class="panel panel-body" style="color:var(--critical)">{{ mdError }}</div>

      <!-- stage 1 -->
      <PanelStage no="1" title="采集 · 去直流 · 正弦拟合"
        desc="对 u(t)、i(t) 各做 IEEE 1057 三参数正弦最小二乘拟合：u(t)=a·sinωt+b·cosωt+c。c 即直流偏置（自动去除），幅值/相位用于后续阻抗。">
        <template #side>
          <div class="eq-block">
            <div class="eq-tag">正弦拟合 · IEEE 1057</div>
            <Latex tex="u(t)=a\sin(\omega t)+b\cos(\omega t)+c" :display="true" />
            <div style="margin-top:6px"><Latex tex="A=\sqrt{a^2+b^2},\ \ \varphi=\operatorname{atan2}(b,a)" /></div>
          </div>
        </template>
        <div class="stat-grid cols-4" style="margin-bottom:14px">
          <StatTile k="u 幅值" :v="fmt.fmt(md?.v_amp, 4)" unit="V" />
          <StatTile k="u 相位" :v="fmt.degFromDeg(md?.v_phase_deg)" />
          <StatTile k="i 幅值" :v="fmt.fmt(md?.i_amp, 4)" unit="A" />
          <StatTile k="i 相位" :v="fmt.degFromDeg(md?.i_phase_deg)" />
          <StatTile k="u 直流偏置" :v="fmt.fmt(md?.v_dc, 3)" unit="V" sub="拟合自动扣除" />
          <StatTile k="i 直流偏置" :v="fmt.fmt(md?.i_dc, 3)" unit="A" />
          <StatTile k="∠Z = φu−φi" :v="fmt.degFromDeg(cur?.z_phase_deg)" accent />
          <StatTile k="u 残差 RMS" :v="fmt.fmt(md?.resid_rms_v, 3)" unit="V" />
        </div>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
          <FigBlock no="Fig. 1a" title="电压 u(t)" unit="原始采样点 + 正弦拟合">
            <EChart :option="vOpt" :height="220" /></FigBlock>
          <FigBlock no="Fig. 1b" title="电流 i(t)" unit="原始采样点 + 正弦拟合">
            <EChart :option="iOpt" :height="220" /></FigBlock>
        </div>
        <FigBlock no="Fig. 2" title="归一化 u / i 相位关系" unit="扣直流 ÷ 幅值，直接读相位差"
          caption="两条曲线的相位差即 ∠Z = φu − φi；幅度一致说明归一化正确。" style="margin-top:12px">
          <EChart :option="normOpt" :height="200" /></FigBlock>
      </PanelStage>

      <!-- stage 2 -->
      <PanelStage no="2" title="残差分析" desc="残差 = 原始 − 拟合。随机散布＝噪声主导；含周期结构＝存在谐波（u 非纯正弦）。">
        <template #side>
          <div class="eq-block">
            <div class="eq-tag">残差 / 信噪比</div>
            <Latex tex="r[k]=x[k]-\big(a\sin\omega t_k+b\cos\omega t_k+c\big)" :display="true" />
            <div style="margin-top:6px"><Latex tex="\mathrm{SNR}_{\mathrm{dB}}=20\log_{10}(A/\sigma_r)" /></div>
          </div>
          <div class="stat-grid cols-2" style="margin-top:14px">
            <StatTile k="u 残差 RMS" :v="fmt.fmt(md?.resid_rms_v, 3)" unit="V" />
            <StatTile k="i 残差 RMS" :v="fmt.fmt(md?.resid_rms_i, 3)" unit="A" />
            <StatTile k="u 信噪比" :v="snrV.toFixed(1)" unit="dB" />
            <StatTile k="i 信噪比" :v="snrI.toFixed(1)" unit="dB" />
          </div>
          <div style="margin-top:12px"><span class="qpill" :class="residDiag.cls">{{ residDiag.t }}</span></div>
        </template>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
          <FigBlock no="Fig. 3a" title="u 残差 Δu(t)"
            caption="随机散布＝噪声主导；周期结构＝存在谐波。">
            <EChart :option="residVOpt" :height="200" /></FigBlock>
          <FigBlock no="Fig. 3b" title="i 残差 Δi(t)">
            <EChart :option="residIOpt" :height="200" /></FigBlock>
        </div>
      </PanelStage>

      <!-- stage 3 -->
      <PanelStage no="3" title="频谱诊断 (FFT)" desc="单边幅值谱（Hann 加窗）。虚线＝激励频率 f₀。用于核对主频、观察谐波与噪声底。阻抗值不依赖 FFT，此处仅供诊断。">
        <template #side>
          <div class="eq-block">
            <div class="eq-tag">离散傅里叶变换（诊断用）</div>
            <Latex tex="X[k]=\sum_{n=0}^{N-1} x[n]\,e^{-j2\pi kn/N}" :display="true" />
          </div>
          <div class="stat-grid cols-1" style="margin-top:14px">
            <StatTile k="激励主频 f₀" :v="fmt.fmtHz(cur?.frequency ?? 0)" />
            <StatTile k="采样率 fs" :v="fmt.eng(fs, 'Hz')" sub="Nyquist = fs/2" />
          </div>
        </template>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
          <FigBlock no="Fig. 4a" title="电压频谱 |U(f)|" unit="Hann 窗 · 诊断用">
            <EChart :option="specVOpt" :height="220" /></FigBlock>
          <FigBlock no="Fig. 4b" title="电流频谱 |I(f)|">
            <EChart :option="specIOpt" :height="220" /></FigBlock>
        </div>
      </PanelStage>

      <!-- stage 4 -->
      <PanelStage no="4" title="该频率的阻抗" desc="由 u/i 的幅值比与相位差得到 Z = |Z|∠θ。容性→负虚部，感性→正虚部。">
        <template #side>
          <div class="eq-block">
            <div class="eq-tag">阻抗</div>
            <Latex tex="Z=\dfrac{V_\mathrm{amp}}{I_\mathrm{amp}}\,e^{\,j(\varphi_v-\varphi_i)}=R+jX" :display="true" />
            <div style="margin-top:6px"><Latex tex="X<0:\,C=-\tfrac{1}{\omega X}\quad X>0:\,L=\tfrac{X}{\omega}" /></div>
          </div>
          <div class="stat-grid cols-2" style="margin-top:14px">
            <StatTile k="|Z|" :v="fmt.fmt(cur?.z_mag, 4)" unit="Ω" accent />
            <StatTile k="∠Z" :v="fmt.degFromDeg(cur?.z_phase_deg)" accent />
            <StatTile k="R = Re" :v="fmt.fmt(cur?.R, 4)" unit="Ω" />
            <StatTile k="X = Im" :v="fmt.fmt(cur?.X, 4)" unit="Ω" />
            <StatTile k="D 损耗因数" :v="fmt.fmt(cur?.D, 3)" />
            <StatTile k="Q 品质因数" :v="fmt.fmt(cur?.Q, 3)" />
            <StatTile k="ESR" :v="fmt.fmt(cur?.esr, 4)" unit="Ω" />
            <StatTile k="等效 C / L" :v="cur?.C_eq ? fmt.eng(cur.C_eq,'F') : (cur?.L_eq ? fmt.eng(cur.L_eq,'H') : '—')" />
            <StatTile k="σ|Z| 不确定度" :v="fmt.fmt(cur?.z_sigma, 2)" unit="Ω" sub="1σ，由双通道残差传播" />
            <StatTile k="σ∠Z" :v="fmt.degFromDeg(cur?.z_phase_sigma_deg)" sub="1σ" />
          </div>
          <div style="margin-top:12px"><span class="badge" :class="cur && cur.X<0 ? 'good' : (cur && cur.X>0 ? 'warn':'')">{{ typeTag }}</span></div>
        </template>
        <FigBlock no="Fig. 5" title="复平面（单点）" unit="Im>0 感性 · Im<0 容性">
          <EChart :option="cpOpt" :height="280" /></FigBlock>
      </PanelStage>
    </template>

    <div v-else class="panel empty">
      <ActivitySquare />
      <div>该扫描暂无测量点。等待 ESP32 上传，或用「生成示例」合成数据。</div>
    </div>
  </div>
</template>
