<script setup lang="ts">
// DocsView.vue — 使用文档阅读器：左栏文档列表（分组）/ 中栏正文 /
// 右栏当前文档目录（markdown 标题层级）。两侧栏 sticky 随页面滚动，
// 右栏用 IntersectionObserver 做 scrollspy 高亮。
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { DOCS } from '../docs'
import { renderDoc, type TocEntry } from '../lib/docToc'

const route = useRoute()
const router = useRouter()

const activeKey = computed(() => {
  const k = route.query.d as string
  return DOCS.some((d) => d.key === k) ? k : DOCS[0].key
})
const activeDoc = computed(() => DOCS.find((d) => d.key === activeKey.value) ?? DOCS[0])

const groups = computed(() => {
  const seen: { group: string; items: typeof DOCS }[] = []
  for (const d of DOCS) {
    let g = seen.find((x) => x.group === d.group)
    if (!g) {
      g = { group: d.group, items: [] }
      seen.push(g)
    }
    g.items.push(d)
  }
  return seen
})

const rendered = computed(() => renderDoc(activeDoc.value.source))
const toc = computed<TocEntry[]>(() => rendered.value.toc.filter((t) => t.level <= 3))

function select(key: string) {
  router.push({ query: { d: key } })
}

// scrollspy — highlight the heading nearest the top of the reading zone
const contentEl = ref<HTMLElement | null>(null)
const activeHeading = ref('')
let observer: IntersectionObserver | null = null

function setupObserver() {
  observer?.disconnect()
  activeHeading.value = toc.value[0]?.id ?? ''
  nextTick(() => {
    const root = contentEl.value
    if (!root) return
    const headings = [...root.querySelectorAll('h1[id], h2[id], h3[id]')]
    if (!headings.length) return
    // The scroll container is .content in App.vue; observe against viewport.
    const visible = new Map<string, number>()
    const update = () => {
      let best: { id: string; top: number } | null = null
      for (const h of headings) {
        const top = (h as HTMLElement).getBoundingClientRect().top
        if (top < 140 && (!best || top > best.top)) {
          best = { id: h.id, top }
        }
      }
      if (best) activeHeading.value = best.id
      void visible
    }
    observer = new IntersectionObserver(update, { rootMargin: '-120px 0px -60% 0px', threshold: 0 })
    headings.forEach((h) => observer!.observe(h))
    update()
  })
}
watch([activeKey, toc], setupObserver, { immediate: true })
onMounted(setupObserver)
onBeforeUnmount(() => observer?.disconnect())

function scrollTo(id: string) {
  document.getElementById(id)?.scrollIntoView({ behavior: 'smooth', block: 'start' })
}
</script>

<template>
  <div class="docs-view">
    <!-- 左栏：文档列表 -->
    <aside class="docs-side">
      <div v-for="g in groups" :key="g.group" class="docs-group">
        <div class="docs-group-title">{{ g.group }}</div>
        <button
          v-for="d in g.items" :key="d.key"
          type="button"
          class="docs-item"
          :class="{ active: d.key === activeKey }"
          @click="select(d.key)"
        >
          {{ d.title }}
        </button>
      </div>
    </aside>

    <!-- 中栏：正文 -->
    <main ref="contentEl" class="docs-main md">
      <h1 class="docs-title">{{ activeDoc.title }}</h1>
      <!-- eslint-disable-next-line vue/no-v-html — 渲染自托管 markdown（html:false） -->
      <div v-html="rendered.html" />
    </main>

    <!-- 右栏：当前文档目录 -->
    <aside class="docs-toc">
      <div class="docs-toc-title">目录</div>
      <button
        v-for="t in toc" :key="t.id"
        type="button"
        class="toc-item"
        :class="[`lv${t.level}`, { active: t.id === activeHeading }]"
        @click="scrollTo(t.id)"
      >
        {{ t.text }}
      </button>
      <div v-if="!toc.length" class="hint">（无标题）</div>
    </aside>
  </div>
</template>

<style scoped>
.docs-view {
  display: grid;
  grid-template-columns: 210px minmax(0, 1fr) 190px;
  gap: 22px;
  max-width: 1520px;
  margin: 0 auto;
  align-items: start;
}

/* 左栏 */
.docs-side {
  position: sticky;
  top: 0;
  max-height: calc(100vh - 40px);
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 14px;
  padding: 4px 0 20px;
}
.docs-group { display: flex; flex-direction: column; gap: 1px; }
.docs-group-title {
  font-size: 10px;
  color: var(--text-3);
  text-transform: uppercase;
  letter-spacing: 0.08em;
  font-weight: 600;
  padding: 0 8px 4px;
}
.docs-item {
  appearance: none;
  border: none;
  background: transparent;
  text-align: left;
  cursor: pointer;
  padding: 6px 8px;
  border-radius: var(--r-sm);
  font-size: 12px;
  color: var(--text-2);
  border-left: 2px solid transparent;
}
.docs-item:hover { background: var(--panel-2); color: var(--text); }
.docs-item.active {
  background: rgba(36, 86, 166, 0.08);
  color: var(--accent);
  border-left-color: var(--accent);
  font-weight: 600;
}

/* 中栏 */
.docs-main {
  background: var(--panel);
  border: 1px solid var(--border);
  border-radius: var(--r);
  padding: 30px 38px 44px;
  min-height: 60vh;
  max-width: 860px;
  width: 100%;
  scroll-margin-top: 20px;
}
.docs-main :deep(h1) { margin-top: 0; }
.docs-main :deep(h2) { border-bottom: 1px solid var(--grid); padding-bottom: 6px; }
.docs-main :deep(h1[id], h2[id], h3[id]) { scroll-margin-top: 76px; }
.docs-title {
  font-size: 20px;
  font-weight: 700;
  margin: 0 0 4px;
  color: var(--text);
}
.docs-title + :deep(h1) { display: none; }

/* 右栏 */
.docs-toc {
  position: sticky;
  top: 0;
  max-height: calc(100vh - 40px);
  overflow-y: auto;
  padding: 4px 0 20px;
}
.docs-toc-title {
  font-size: 10px;
  color: var(--text-3);
  text-transform: uppercase;
  letter-spacing: 0.08em;
  font-weight: 600;
  padding: 0 8px 6px;
}
.toc-item {
  appearance: none;
  border: none;
  background: transparent;
  text-align: left;
  cursor: pointer;
  display: block;
  width: 100%;
  padding: 4px 8px;
  font-size: 11.5px;
  line-height: 1.45;
  color: var(--text-3);
  border-left: 2px solid var(--grid);
}
.toc-item.lv1 { font-weight: 600; color: var(--text-2); }
.toc-item.lv2 { padding-left: 16px; }
.toc-item.lv3 { padding-left: 26px; font-size: 11px; }
.toc-item:hover { color: var(--text); }
.toc-item.active { color: var(--accent); border-left-color: var(--accent); }

@media (max-width: 980px) {
  .docs-view { grid-template-columns: 1fr; }
  .docs-side, .docs-toc { position: static; max-height: none; }
  .docs-toc { display: none; }
}
</style>
