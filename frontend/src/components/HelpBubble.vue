<script setup lang="ts">
// HelpBubble.vue — the circular "？" floating helper: click to open a popover
// explaining the attached feature: what it does, valid magnitudes, engine
// limits.  Closes on outside click / Esc.
import { onBeforeUnmount, onMounted, ref } from 'vue'
import { HelpCircle } from '@lucide/vue'

defineProps<{
  title: string
  /** short intro paragraph */
  intro?: string
  /** constraint rows [label, value] shown as a mini table */
  rows?: [string, string][]
  /** extra free-form bullet lines */
  bullets?: string[]
}>()

const open = ref(false)
const rootEl = ref<HTMLElement | null>(null)

function onDoc(e: MouseEvent) {
  if (open.value && rootEl.value && !rootEl.value.contains(e.target as Node)) open.value = false
}
function onKey(e: KeyboardEvent) {
  if (e.key === 'Escape') open.value = false
}
onMounted(() => {
  document.addEventListener('mousedown', onDoc)
  document.addEventListener('keydown', onKey)
})
onBeforeUnmount(() => {
  document.removeEventListener('mousedown', onDoc)
  document.removeEventListener('keydown', onKey)
})
</script>

<template>
  <span ref="rootEl" class="help-wrap">
    <button
      class="help-btn"
      :class="{ active: open }"
      :aria-label="`帮助：${title}`"
      :title="title"
      type="button"
      @click="open = !open"
    >
      <HelpCircle />
    </button>
    <transition name="help-pop">
      <div v-if="open" class="help-pop" role="dialog">
        <div class="help-head">{{ title }}</div>
        <p v-if="intro" class="help-intro">{{ intro }}</p>
        <table v-if="rows && rows.length" class="help-rows">
          <tbody>
            <tr v-for="(r, i) in rows" :key="i">
              <td class="k">{{ r[0] }}</td>
              <td class="v">{{ r[1] }}</td>
            </tr>
          </tbody>
        </table>
        <ul v-if="bullets && bullets.length" class="help-bullets">
          <li v-for="(b, i) in bullets" :key="i">{{ b }}</li>
        </ul>
        <slot />
      </div>
    </transition>
  </span>
</template>

<style scoped>
.help-wrap { position: relative; display: inline-flex; }

.help-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 20px;
  height: 20px;
  padding: 0;
  border-radius: 50%;
  border: 1px solid var(--border-strong);
  background: var(--panel-2);
  color: var(--text-3);
  cursor: pointer;
  transition: all .15s ease;
}
.help-btn svg { width: 12px; height: 12px; }
.help-btn:hover, .help-btn.active {
  color: #fff;
  background: var(--accent);
  border-color: var(--accent);
}

.help-pop {
  position: absolute;
  top: calc(100% + 8px);
  right: 0;
  z-index: 40;
  width: 340px;
  max-width: min(340px, 86vw);
  background: var(--surface);
  border: 1px solid var(--border-strong);
  border-radius: var(--r);
  box-shadow: var(--shadow-sm);
  padding: 12px 14px;
  font-size: 11.5px;
  line-height: 1.55;
  color: var(--text-2);
}
.help-head { font-weight: 600; font-size: 12.5px; color: var(--text); margin-bottom: 4px; }
.help-intro { margin: 2px 0 8px; }
.help-rows { width: 100%; border-collapse: collapse; margin: 4px 0; }
.help-rows td { padding: 3px 6px 3px 0; border-bottom: 1px solid var(--grid); vertical-align: top; }
.help-rows tr:last-child td { border-bottom: none; }
.help-rows .k { color: var(--text-3); white-space: nowrap; width: 40%; }
.help-rows .v { color: var(--text); font-family: 'IBM Plex Mono', monospace; font-size: 10.5px; }
.help-bullets { margin: 4px 0 0; padding-left: 1.2em; }
.help-bullets li { margin: 3px 0; }

.help-pop-enter-active, .help-pop-leave-active { transition: opacity .12s ease, transform .12s ease; }
.help-pop-enter-from, .help-pop-leave-to { opacity: 0; transform: translateY(-4px); }
</style>
