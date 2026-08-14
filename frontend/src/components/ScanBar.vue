<script setup lang="ts">
import { RefreshCw } from '@lucide/vue'
import { storeToRefs } from 'pinia'
import { ref } from 'vue'
import { useScanStore } from '../store/scan'
import { postSyntheticScan, PRESETS } from '../lib/generate'

const store = useScanStore()
const { scans, currentId, loading } = storeToRefs(store)
const busy = ref(false)

async function onSelect(e: Event) {
  const v = (e.target as HTMLSelectElement).value
  await store.select(v || null)
}
async function refresh() {
  await store.refresh()
}
async function gen(key: keyof typeof PRESETS) {
  busy.value = true
  try {
    const id = await postSyntheticScan(PRESETS[key])
    await store.refresh()
    await store.select(id)
  } finally {
    busy.value = false
  }
}
</script>

<template>
  <div class="panel">
    <div class="panel-body row tight">
      <label class="field" style="flex: 1 1 280px; min-width: 240px">
        <span class="muted">当前扫描 / Scan</span>
        <select class="scan-select" :value="currentId || ''" @change="onSelect" :disabled="loading || busy">
          <option value="">— 选择扫描 —</option>
          <option v-for="s in scans" :key="s.id" :value="s.id">
            {{ s.id.slice(0, 8) }} · {{ s.device }} · {{ s.measurement_count }}点 · {{ s.note || '—' }} · {{ new Date(s.created_at).toLocaleString() }}
          </option>
        </select>
      </label>
      <button class="btn" @click="refresh" :disabled="loading"><RefreshCw /> 刷新</button>
      <span class="muted hint">生成示例：</span>
      <button class="btn sm" @click="gen('rlc')" :disabled="busy">串联 RLC</button>
      <button class="btn sm" @click="gen('rc')" :disabled="busy">RC</button>
      <button class="btn sm" @click="gen('rl')" :disabled="busy">RL</button>
      <button class="btn sm" @click="gen('rlc_harm')" :disabled="busy" title="含二次谐波，演示 u 非纯正弦">RLC+谐波</button>
    </div>
  </div>
</template>
