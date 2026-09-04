<script setup lang="ts">
import { ActivitySquare, LineChart, CircuitBoard, Radio, Crosshair, Archive, BookOpen } from '@lucide/vue'
</script>

<template>
  <aside class="sidebar">
    <div class="brand">
      <div class="logo">Z</div>
      <div>
        <div class="name">LCR Analyzer</div>
        <div class="sub">ESP32 阻抗平台</div>
      </div>
    </div>
    <nav class="nav">
      <div class="nav-section">测量分析</div>
      <router-link class="nav-item" to="/analysis"><ActivitySquare class="ic" /><span>时域分析</span></router-link>
      <router-link class="nav-item" to="/sweep"><LineChart class="ic" /><span>扫频 Bode</span></router-link>
      <router-link class="nav-item" to="/fit"><CircuitBoard class="ic" /><span>电路辨识拟合</span></router-link>
      <router-link class="nav-item" to="/live"><Radio class="ic" /><span>实时监测</span></router-link>
      <div class="nav-section">系统</div>
      <router-link class="nav-item" to="/calibrate"><Crosshair class="ic" /><span>OSL 校准</span></router-link>
      <router-link class="nav-item" to="/history"><Archive class="ic" /><span>实验历史</span></router-link>
      <router-link class="nav-item" to="/docs"><BookOpen class="ic" /><span>使用文档</span></router-link>
    </nav>
    <div class="sidebar-footer">v0.3 · C++ WASM 三引擎辨识</div>
  </aside>
</template>
