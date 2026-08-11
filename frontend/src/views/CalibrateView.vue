<script setup lang="ts">
import ScanBar from '../components/ScanBar.vue'
import Markdown from '../components/Markdown.vue'
import Latex from '../components/Latex.vue'

const calibIntro = String.raw`OSL（**O**pen / **S**hort / **L**oad）校准是获得可信容性/感性相位与 $D/Q$ 的前提：模拟前端在 V、I 两通道上引入不同相移，线缆引入寄生参数。校准数据**按频率存储**后，对每次测量自动扣除。

> 本向导为交互骨架，校准数据的采集与存储接口随 ESP32 模拟前端硬件定型后一并启用（后端 \`CalibSet\` 模型与 \`dsp/calibration.py\` 的 OSL 校正算法已就位）。`

const steps = [
  { no: 1, title: '短路 Short', desc: String.raw`DUT 短接（$0\,\Omega$），测各频率点 $Z_\text{short}$，扣除引线与电流回路的**串联阻抗**。`, tex: String.raw`Z_{\text{DUT}} = Z_{\text{meas}} - Z_{\text{short}}` },
  { no: 2, title: '开路 Open', desc: String.raw`DUT 开路，测 $Z_\text{open}$，扣除夹具的**并联寄生导纳**（寄生电容）。`, tex: String.raw`Y_{\text{DUT}} = Y_{\text{meas}} - Y_{\text{open}}` },
  { no: 3, title: '负载 Load', desc: String.raw`接已知精密电阻（应读 $\angle\,0°$），消除两通道的**增益误差**与残余**相位偏移**。`, tex: String.raw`Z_{\text{DUT}} = k\cdot Z_{\text{meas}}` },
]
</script>

<template>
  <div class="view">
    <ScanBar />
    <section class="panel">
      <div class="panel-head"><h3>OSL 校准向导</h3><span class="tag">开路 · 短路 · 负载</span><div class="spacer" /></div>
      <div class="panel-body">
        <Markdown :source="calibIntro" />
        <div class="stat-grid cols-3" style="margin-top:16px">
          <div v-for="s in steps" :key="s.no" class="panel" style="padding:16px;box-shadow:var(--shadow-sm)">
            <div class="stage-no">{{ s.no }}</div>
            <div class="stage-title" style="margin-top:10px">{{ s.title }}</div>
            <div class="stage-desc" style="margin-top:6px"><Markdown :source="s.desc" /></div>
            <div class="eq-block" style="margin-top:10px"><Latex :tex="s.tex" :display="true" /></div>
            <button class="btn sm" style="margin-top:10px" disabled>采集（待硬件）</button>
          </div>
        </div>
      </div>
    </section>
  </div>
</template>
