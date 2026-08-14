<script setup lang="ts">
defineProps<{
  no: string            // e.g. "Fig. 1"
  title: string
  caption?: string
  unit?: string
}>()
</script>

<template>
  <section class="fig">
    <div class="fig-head">
      <span class="fig-no">{{ no }}</span>
      <span class="fig-title">{{ title }}</span>
      <span class="cunit" v-if="unit">{{ unit }}</span>
      <slot name="actions" />
    </div>
    <slot />
    <div class="fig-caption" v-if="caption">{{ caption }}</div>
  </section>
</template>
