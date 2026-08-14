<script setup lang="ts">
import { ref, computed, onMounted, onBeforeUnmount } from 'vue'
import { useAppStore } from '../store/app'
import ScanBar from '../components/ScanBar.vue'
import StatTile from '../components/StatTile.vue'
import EChart from '../components/EChart.vue'
import { getPalette } from '../lib/palette'
import FigBlock from '../components/FigBlock.vue'
import { Radio } from '@lucide/vue'
import { bodeOpt } from '../lib/charts'
import * as api from '../api'
import * as fmt from '../lib/format'

const app = useAppStore()
const p = computed(() => getPalette())
const status = ref<'idle' | 'connecting' | 'connected' | 'error' | 'closed'>('idle')
const points = ref<{ f: number; mag: number; phase: number; t: number }[]>([])
let ws: WebSocket | null = null
let firstT = 0

function connect() {
  disconnect()
  status.value = 'connecting'
  try {
    ws = new WebSocket(api.wsLiveUrl())
  } catch {
    status.value = 'error'
    return
  }
  ws.onopen = () => { status.value = 'connected'; app.deviceOnline = true; app.device = 'ESP32_LCR_LIVE' }
  ws.onmessage = (ev) => {
    try {
      const m = JSON.parse(ev.data)
      if (m.type === 'point') {
        const t = Date.now()
        if (!firstT) firstT = t
        points.value.push({ f: m.frequency, mag: m.z_mag, phase: m.z_phase_deg, t: (t - firstT) / 1000 })
        if (points.value.length > 300) points.value.shift()
      }
    } catch { /* ignore */ }
  }
  ws.onerror = () => { status.value = 'error' }
  ws.onclose = () => { status.value = 'closed'; app.deviceOnline = false }
}
function disconnect() {
  if (ws) { ws.close(); ws = null }
  app.deviceOnline = false
}
onMounted(connect)
onBeforeUnmount(disconnect)

const last = computed(() => points.value[points.value.length - 1] || null)
const statusMeta = computed(() => {
  switch (status.value) {
    case 'connected': return { cls: 'good', txt: '已连接' }
    case 'connecting': return { cls: 'warn', txt: '连接中…' }
    case 'error': return { cls: 'crit', txt: '连接错误' }
    case 'closed': return { cls: 'idle', txt: '已断开' }
    default: return { cls: 'idle', txt: '空闲' }
  }
})
const magOpt = computed(() => bodeOpt(p.value, { mode: 'mag', measured: points.value.map((pp) => ({ f: pp.f, v: pp.mag })), yLabel: '|Z| (Ω)' }))
</script>

<template>
  <div class="view">
    <ScanBar />
    <section class="panel">
      <div class="panel-head">
        <h3>WebSocket 实时数据流</h3>
        <span class="badge" :class="statusMeta.cls"><span class="dot" :class="statusMeta.cls === 'good' ? 'good' : 'idle'"></span>{{ statusMeta.txt }}</span>
        <span class="hint" style="margin-left:8px">/ws/live</span>
        <div class="spacer" />
        <button class="btn sm" @click="connect">重连</button>
        <button class="btn sm" @click="disconnect">断开</button>
      </div>
      <div class="panel-body">
        <div class="hint" style="margin-bottom:12px">ESP32（或模拟器）每上传一个频率点，后端会通过 WebSocket 推送该点的 |Z| 与相位。同时打开本页并运行上传即可看到实时流。</div>
        <div class="stat-grid cols-4">
          <StatTile k="已接收点数" :v="points.length" />
          <StatTile k="最新频率" :v="last ? fmt.fmtHz(last.f) : '—'" accent />
          <StatTile k="最新 |Z|" :v="last ? fmt.fmt(last.mag, 4) + ' Ω' : '—'" />
          <StatTile k="最新 ∠Z" :v="last ? fmt.degFromDeg(last.phase) : '—'" />
        </div>
      </div>
    </section>

    <FigBlock v-if="points.length" no="Fig. 1" title="实时 |Z|(f)" unit="流式散点">
      <EChart :option="magOpt" :height="300" />
    </FigBlock>
    <div v-else class="panel empty"><Radio /><div>等待数据流…运行「生成示例」或模拟器上传即可在此看到实时点。</div></div>
  </div>
</template>
