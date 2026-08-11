<script setup lang="ts">
import { computed } from 'vue'
import MarkdownIt from 'markdown-it'
import texmath from 'markdown-it-texmath'
import katex from 'katex'

const props = defineProps<{ source: string }>()

const md = new MarkdownIt({
  html: false,
  linkify: true,
  breaks: false,
  typographer: true,
})
md.use(texmath, {
  engine: katex,
  delimiters: 'dollars',
  throwOnError: false,
})

const html = computed(() => md.render(props.source || ''))
</script>

<template>
  <div class="md" v-html="html" />
</template>
