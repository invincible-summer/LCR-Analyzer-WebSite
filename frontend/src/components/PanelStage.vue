<script setup lang="ts">
defineProps<{ no: string | number; title: string; desc?: string }>()
</script>

<template>
  <section class="panel stage">
    <aside class="stage-side">
      <div class="stage-no">{{ no }}</div>
      <div class="stage-title">{{ title }}</div>
      <div class="stage-desc" v-if="desc">{{ desc }}</div>
      <slot name="side" />
    </aside>
    <div class="stage-main">
      <slot />
    </div>
  </section>
</template>
