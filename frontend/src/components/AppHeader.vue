<script setup lang="ts">
import { computed } from 'vue'
import { useRoute } from 'vue-router'
import { useAppStore } from '../store/app'

const route = useRoute()
const app = useAppStore()
const title = computed(() => (route.meta.title as string) || '')
const sub = computed(() => (route.meta.sub as string) || '')
</script>

<template>
  <header class="topbar">
    <div class="title-block">
      <div class="title">{{ title }}</div>
      <div class="subtitle">{{ sub }}</div>
    </div>
    <div class="spacer" />
    <div class="row tight">
      <span class="badge" :class="app.deviceOnline ? 'good' : ''">
        <span class="dot" :class="app.deviceOnline ? 'good' : 'idle'"></span>
        {{ app.device }} · {{ app.deviceOnline ? 'ONLINE' : 'OFFLINE' }}
      </span>
    </div>
  </header>
</template>
