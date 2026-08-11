<script setup lang="ts">
import * as echarts from 'echarts'
import { ref, onMounted, onBeforeUnmount, watch } from 'vue'

const props = withDefaults(defineProps<{ option: any; height?: number }>(), { height: 240 })

const el = ref<HTMLDivElement | null>(null)
let chart: echarts.ECharts | null = null
let ro: ResizeObserver | null = null

function ensure(): echarts.ECharts | null {
  if (!el.value) return null
  if (!chart) {
    chart = echarts.init(el.value, undefined, { renderer: 'canvas' })
    ro = new ResizeObserver(() => chart?.resize())
    ro.observe(el.value)
  }
  return chart
}

function render() {
  const c = ensure()
  if (c && props.option) c.setOption(props.option, true)
}

onMounted(render)
watch(() => props.option, render, { deep: true })
onBeforeUnmount(() => {
  ro?.disconnect()
  chart?.dispose()
  chart = null
})
</script>

<template>
  <div ref="el" class="chart" :style="{ height: height + 'px' }"></div>
</template>
