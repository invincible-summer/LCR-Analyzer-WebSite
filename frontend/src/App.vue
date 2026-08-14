<script setup lang="ts">
import { onMounted } from 'vue'
import AppSidebar from './components/AppSidebar.vue'
import AppHeader from './components/AppHeader.vue'
import { useScanStore } from './store/scan'

const scan = useScanStore()

onMounted(() => {
  scan.loadScans().catch(() => {})
})
</script>

<template>
  <div class="app-shell">
    <AppSidebar />
    <div class="main">
      <AppHeader />
      <main class="content">
        <router-view />
      </main>
    </div>
  </div>
</template>
