<script setup lang="ts">
import { ref } from 'vue'
import ScanBar from '../components/ScanBar.vue'

const step = ref<'idle' | 'short' | 'open' | 'load' | 'done'>('idle')
const steps = [
  { key: 'short', no: 1, title: '短路校准', desc: '将测试端短接（DUT = 0Ω），测量各频率点 Z_short(f)，扣除引线与电流回路的串联阻抗。' },
  { key: 'open', no: 2, title: '开路校准', desc: '将测试端开路，测量 Z_open(f)，扣除夹具的并联寄生导纳（寄生电容）。' },
  { key: 'load', no: 3, title: '负载校准', desc: '接入已知精密电阻（应读 ∠0°），据此消除两通道的增益误差与残余相位偏移。' },
] as const
</script>

<template>
  <div class="view">
    <ScanBar />
    <section class="panel">
      <div class="panel-head"><h3>OSL 校准向导</h3><span class="tag">开路 · 短路 · 负载</span><div class="spacer" /></div>
      <div class="panel-body">
        <div class="hint" style="margin-bottom:14px;max-width:760px">
          OSL 校准是获得可信容性/感性相位与 D/Q 的前提：模拟前端在 V、I 两通道上引入了不同的相移，线缆引入了寄生参数。校准数据按频率存储，后续每次测量自动扣除。
          <br/><br/>
          <strong style="color:var(--warning)">本向导为交互骨架，校准数据的采集与存储接口随 ESP32 模拟前端硬件定型后一并启用</strong>（后端 CalibSet 模型与 dsp/calibration.py 的 OSL 校正算法已就位）。
        </div>
        <div class="stat-grid cols-3">
          <div v-for="s in steps" :key="s.key" class="panel" style="padding:14px">
            <div class="stage-no" style="margin-bottom:8px">{{ s.no }}</div>
            <div class="stage-title">{{ s.title }}</div>
            <div class="stage-desc">{{ s.desc }}</div>
            <button class="btn sm" style="margin-top:10px" disabled>采集（待硬件）</button>
          </div>
        </div>
      </div>
    </section>
  </div>
</template>
