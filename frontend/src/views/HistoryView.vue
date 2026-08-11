<script setup lang="ts">
import { onMounted } from 'vue'
import { storeToRefs } from 'pinia'
import { useScanStore } from '../store/scan'
import * as api from '../api'
import ScanBar from '../components/ScanBar.vue'
import * as fmt from '../lib/format'

const store = useScanStore()
const { scans } = storeToRefs(store)

onMounted(() => store.loadScans())

async function remove(id: string) {
  if (!confirm(`删除扫描 ${id.slice(0, 8)}？`)) return
  await api.deleteScan(id)
  await store.loadScans()
}
</script>

<template>
  <div class="view">
    <ScanBar />
    <section class="panel">
      <div class="panel-head"><h3>实验记录</h3><span class="tag">{{ scans.length }} 次扫描</span><div class="spacer" /></div>
      <table class="data">
        <thead>
          <tr>
            <th>ID</th><th>设备</th><th>备注</th><th class="num">频率点</th><th>状态</th><th>时间</th><th></th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="s in scans" :key="s.id">
            <td class="mono">{{ s.id.slice(0, 8) }}</td>
            <td>{{ s.device }}</td>
            <td>{{ s.note || '—' }}</td>
            <td class="num mono">{{ s.measurement_count }}</td>
            <td><span class="badge" :class="s.status === 'done' ? 'good' : ''">{{ s.status }}</span></td>
            <td class="hint">{{ new Date(s.created_at).toLocaleString() }}</td>
            <td class="row tight" style="justify-content:flex-end">
              <button class="btn sm" @click="store.select(s.id); $router.push('/analysis')">分析</button>
              <a class="btn sm" :href="api.exportScanUrl(s.id, 'csv')" target="_blank">CSV</a>
              <a class="btn sm" :href="api.exportScanUrl(s.id, 'json')" target="_blank">JSON</a>
              <button class="btn sm" @click="remove(s.id)">删除</button>
            </td>
          </tr>
          <tr v-if="!scans.length"><td colspan="7" class="hint" style="text-align:center;padding:30px">暂无记录，用「生成示例」或在实时页上传数据。</td></tr>
        </tbody>
      </table>
    </section>
  </div>
</template>
