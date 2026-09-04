<script setup lang="ts">
// Tabs.vue — minimal tab strip following the panel design language (no UI
// library in this project).  `modelValue` is the active tab key.
defineProps<{
  tabs: { key: string; label: string; sub?: string }[]
  modelValue: string
}>()
defineEmits<{ (e: 'update:modelValue', key: string): void }>()
</script>

<template>
  <div class="tabs" role="tablist">
    <button
      v-for="t in tabs"
      :key="t.key"
      type="button"
      role="tab"
      class="tab"
      :class="{ active: modelValue === t.key }"
      :aria-selected="modelValue === t.key"
      @click="$emit('update:modelValue', t.key)"
    >
      <span class="tab-label">{{ t.label }}</span>
      <span v-if="t.sub" class="tab-sub">{{ t.sub }}</span>
    </button>
  </div>
</template>

<style scoped>
.tabs {
  display: flex;
  gap: 2px;
  border-bottom: 1px solid var(--border-strong);
}
.tab {
  appearance: none;
  border: none;
  background: transparent;
  cursor: pointer;
  padding: 9px 16px 8px;
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 1px;
  border-bottom: 2px solid transparent;
  margin-bottom: -1px;
  color: var(--text-3);
}
.tab:hover { color: var(--text); background: var(--panel-2); }
.tab.active { color: var(--text); border-bottom-color: var(--accent); }
.tab-label { font-size: 12.5px; font-weight: 600; }
.tab.active .tab-label { color: var(--accent); }
.tab-sub { font-size: 10px; }
</style>
