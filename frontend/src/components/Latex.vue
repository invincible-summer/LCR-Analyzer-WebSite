<script setup lang="ts">
import katex from 'katex'
import { computed } from 'vue'

const props = defineProps<{ tex: string; block?: boolean; display?: boolean }>()

const html = computed(() => {
  const displayMode = props.display ?? props.block ?? false
  try {
    return katex.renderToString(props.tex, {
      throwOnError: false,
      displayMode,
      output: 'html',
    })
  } catch {
    return props.tex
  }
})

const isBlock = computed(() => props.display ?? props.block ?? false)
</script>

<template>
  <span v-if="!isBlock" class="latex inline" v-html="html" />
  <div v-else class="latex block" v-html="html" />
</template>
