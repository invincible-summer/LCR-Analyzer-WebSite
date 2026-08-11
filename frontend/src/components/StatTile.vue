<script setup lang="ts">
defineProps<{ k: string; v: string | number; unit?: string; sub?: string; accent?: boolean }>()
</script>

<template>
  <div class="stat" :class="{ accent }">
    <div class="k">{{ k }}</div>
    <div class="v mono">
      {{ v }}<span class="u" v-if="unit">{{ unit }}</span>
    </div>
    <div class="sub" v-if="sub">{{ sub }}</div>
  </div>
</template>
