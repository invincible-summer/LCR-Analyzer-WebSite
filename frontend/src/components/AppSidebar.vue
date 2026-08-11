<script setup lang="ts"></script>

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
      <router-link class="nav-item" to="/analysis"><span class="ic">∿</span><span>时域分析</span></router-link>
      <router-link class="nav-item" to="/sweep"><span class="ic">⋀</span><span>扫频 Bode</span></router-link>
      <router-link class="nav-item" to="/fit"><span class="ic">Σ</span><span>等效电路拟合</span></router-link>
      <router-link class="nav-item" to="/live"><span class="ic">◉</span><span>实时监测</span></router-link>
      <div class="nav-section">系统</div>
      <router-link class="nav-item" to="/calibrate"><span class="ic">⊕</span><span>OSL 校准</span></router-link>
      <router-link class="nav-item" to="/history"><span class="ic">▤</span><span>实验历史</span></router-link>
    </nav>
    <div class="sidebar-footer">v0.1 · 正弦拟合 + OSL</div>
  </aside>
</template>
