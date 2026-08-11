<script setup lang="ts">
import { onMounted } from 'vue'
import AppSidebar from './components/AppSidebar.vue'
import AppHeader from './components/AppHeader.vue'
import { useAppStore } from './store/app'
import { useScanStore } from './store/scan'

const app = useAppStore()
const scan = useScanStore()

onMounted(() => {
  app.applyTheme()
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
