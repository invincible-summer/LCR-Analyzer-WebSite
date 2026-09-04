<script setup lang="ts">
// GraphEditor.vue — csacademy-graph-editor-style topology input for Try3.
//
// Four modes (mirroring csacademy): 绘制 (click canvas = new node, click two
// nodes = new edge), 拖动 (reposition), 编辑 (click an edge to change its
// component kind / delete), 删除 (click to remove).  Nodes 0 and 1 are the
// one-port terminals — fixed, highlighted, undeletable.  A synced text panel
// (left) accepts "u v R|L|C" lines, csacademy-style.  Emits the edge list for
// the Try3 engine via v-model:edges.
import { computed, reactive, ref, watch } from 'vue'
import type { CompKind, TopoEdge } from '../lib/fitTypes'
import { circleLayout, edgePath, GRAPH_NODE_R } from '../lib/graphLayout'

const props = defineProps<{ edges: TopoEdge[]; height?: number }>()
const emit = defineEmits<{ (e: 'update:edges', v: TopoEdge[]): void }>()

const W = 620
const H = 380

type Mode = 'draw' | 'move' | 'edit' | 'delete'
const mode = ref<Mode>('draw')
const currentKind = ref<CompKind>('R')
const pendingNode = ref<number | null>(null)
const selectedEdge = ref<number | null>(null)

interface EEdge { id: number; u: number; v: number; kind: CompKind }
interface ENode { id: number; x: number; y: number }

const nodes = reactive<ENode[]>([])
const edges = reactive<EEdge[]>([])
let nextEdgeId = 1

// ---- sync with parent -------------------------------------------------------

function serialize(es: EEdge[]): string {
  return [...es]
    .map((e) => ({ ...e, a: Math.min(e.u, e.v), b: Math.max(e.u, e.v) }))
    .sort((x, y) => x.a - y.a || x.b - y.b || x.kind.localeCompare(y.kind))
    .map((e) => `${e.a} ${e.b} ${e.kind}`)
    .join('\n')
}

function ensureNode(id: number) {
  if (!nodes.some((n) => n.id === id)) {
    const others = nodes.filter((n) => n.id !== 0 && n.id !== 1)
    // place near the top arc, avoiding existing nodes
    const ang = Math.PI * (0.35 + 0.3 * ((id % 7) / 6))
    nodes.push({
      id,
      x: W / 2 + (W / 2 - 52) * Math.cos(ang) * (id % 2 ? 1 : -1),
      y: 44 + (H - 96) * (0.25 + 0.5 * ((id >> 1) % 3) / 2),
    })
    void others
  }
}

function importEdges(list: TopoEdge[]) {
  nodes.length = 0
  edges.length = 0
  nextEdgeId = 1
  const ids = new Set<number>([0, 1])
  for (const e of list) {
    ids.add(e.u)
    ids.add(e.v)
  }
  const laid = circleLayout([...ids], W, H)
  nodes.push(...laid.map((n) => ({ ...n })))
  for (const e of list) edges.push({ id: nextEdgeId++, u: e.u, v: e.v, kind: e.kind })
}

watch(
  () => props.edges,
  (list) => {
    const incoming = serialize(list.map((e) => ({ id: 0, u: e.u, v: e.v, kind: e.kind })))
    if (incoming !== serialize(edges)) importEdges(list)
  },
  { immediate: true, deep: true },
)

function pushUpdate() {
  emit(
    'update:edges',
    edges.map((e) => ({ u: e.u, v: e.v, kind: e.kind })),
  )
}

// ---- edit operations --------------------------------------------------------

const nodeCount = computed(() => nodes.length)
const edgeCount = computed(() => edges.length)
const nodeLimitWarn = computed(() => nodeCount.value > 8)
const edgeLimitWarn = computed(() => edgeCount.value > 12)

function addNode(x: number, y: number): number {
  const used = new Set(nodes.map((n) => n.id))
  let id = 2
  while (used.has(id)) id++
  nodes.push({ id, x: Math.min(Math.max(x, 24), W - 24), y: Math.min(Math.max(y, 24), H - 24) })
  return id
}
function addEdge(u: number, v: number, kind: CompKind) {
  if (u === v) return
  if (edges.length >= 16) return
  if (Math.max(u, v) >= 16) return
  edges.push({ id: nextEdgeId++, u, v, kind })
  pushUpdate()
}
function deleteNode(id: number) {
  if (id === 0 || id === 1) return
  const i = nodes.findIndex((n) => n.id === id)
  if (i >= 0) nodes.splice(i, 1)
  for (let j = edges.length - 1; j >= 0; j--) {
    if (edges[j].u === id || edges[j].v === id) edges.splice(j, 1)
  }
  if (pendingNode.value === id) pendingNode.value = null
  pushUpdate()
}
function deleteEdge(id: number) {
  const i = edges.findIndex((e) => e.id === id)
  if (i >= 0) edges.splice(i, 1)
  if (selectedEdge.value === id) selectedEdge.value = null
  pushUpdate()
}
function cycleKind(id: number) {
  const e = edges.find((x) => x.id === id)
  if (!e) return
  e.kind = e.kind === 'R' ? 'L' : e.kind === 'L' ? 'C' : 'R'
  pushUpdate()
}

// ---- pointer interaction ----------------------------------------------------

const svgEl = ref<SVGSVGElement | null>(null)
let dragNode: { id: number; dx: number; dy: number } | null = null

function svgPoint(ev: PointerEvent | MouseEvent): { x: number; y: number } {
  const svg = svgEl.value
  if (!svg) return { x: 0, y: 0 }
  const pt = svg.createSVGPoint()
  pt.x = ev.clientX
  pt.y = ev.clientY
  const m = svg.getScreenCTM()
  if (!m) return { x: 0, y: 0 }
  const p = pt.matrixTransform(m.inverse())
  return { x: p.x, y: p.y }
}

function onCanvasClick(ev: MouseEvent) {
  if (mode.value !== 'draw') return
  if ((ev.target as Element).tagName !== 'rect') return // only bare canvas
  const p = svgPoint(ev)
  if (nodeCount.value >= 16) return
  addNode(p.x, p.y)
  pushUpdate()
}

function onNodePointerDown(ev: PointerEvent, id: number) {
  ev.stopPropagation()
  if (mode.value === 'move') {
    const n = nodes.find((x) => x.id === id)
    if (!n) return
    const p = svgPoint(ev)
    dragNode = { id, dx: n.x - p.x, dy: n.y - p.y };
    (ev.target as Element).setPointerCapture(ev.pointerId)
    return
  }
  if (mode.value === 'draw') {
    if (pendingNode.value === null) {
      pendingNode.value = id
    } else if (pendingNode.value === id) {
      pendingNode.value = null
    } else {
      addEdge(pendingNode.value, id, currentKind.value)
      pendingNode.value = null
    }
    return
  }
  if (mode.value === 'delete') {
    deleteNode(id)
  }
}
function onPointerMove(ev: PointerEvent) {
  if (!dragNode) return
  const n = nodes.find((x) => x.id === dragNode!.id)
  if (!n) return
  const p = svgPoint(ev)
  n.x = Math.min(Math.max(p.x + dragNode.dx, 24), W - 24)
  n.y = Math.min(Math.max(p.y + dragNode.dy, 24), H - 24)
}
function onPointerUp() {
  dragNode = null
}

function onEdgeClick(ev: MouseEvent, id: number) {
  ev.stopPropagation()
  if (mode.value === 'delete') {
    deleteEdge(id)
  } else if (mode.value === 'edit') {
    selectedEdge.value = selectedEdge.value === id ? null : id
  } else if (mode.value === 'draw') {
    cycleKind(id) // quick edit even in draw mode
  }
}

// ---- text panel sync --------------------------------------------------------

const textPanel = computed<string>({
  get: () => serialize(edges),
  set: (val) => {
    const parsed: TopoEdge[] = []
    for (const raw of val.split(/\r?\n/)) {
      const line = raw.trim()
      if (!line || line.startsWith('#')) continue
      const m = line.split(/[\s,]+/)
      if (m.length !== 3) continue
      const u = Number(m[0])
      const v = Number(m[1])
      const k = m[2].toUpperCase()
      if (!Number.isInteger(u) || !Number.isInteger(v) || u < 0 || v < 0 || u === v) continue
      if (k !== 'R' && k !== 'L' && k !== 'C') continue
      if (Math.max(u, v) >= 16 || parsed.length >= 16) continue
      parsed.push({ u, v, kind: k })
    }
    const incoming = serialize(parsed.map((e) => ({ id: 0, ...e })))
    if (incoming !== serialize(edges)) {
      importEdges(parsed)
      pushUpdate()
    }
  },
})

// ---- exposed helpers --------------------------------------------------------

function clearAll() {
  edges.length = 0
  const laid = circleLayout([0, 1], W, H)
  nodes.length = 0
  nodes.push(...laid.map((n) => ({ ...n })))
  pendingNode.value = null
  selectedEdge.value = null
  pushUpdate()
}
function loadLadderExample() {
  importEdges([
    { u: 0, v: 2, kind: 'L' },
    { u: 2, v: 1, kind: 'C' },
    { u: 2, v: 1, kind: 'R' },
  ])
  pushUpdate()
}
function loadParallelExample() {
  importEdges([
    { u: 0, v: 1, kind: 'R' },
    { u: 0, v: 1, kind: 'C' },
    { u: 0, v: 1, kind: 'L' },
  ])
  pushUpdate()
}
defineExpose({ clearAll, loadLadderExample, loadParallelExample })

// ---- derived draw data ------------------------------------------------------

interface DrawEdge extends EEdge { d: ReturnType<typeof edgePath> }

const drawEdges = computed<DrawEdge[]>(() => {
  // group parallel edges per node pair for arc fan-out
  const groups = new Map<string, EEdge[]>()
  for (const e of edges) {
    const a = Math.min(e.u, e.v)
    const b = Math.max(e.u, e.v)
    const key = `${a}-${b}`
    const g = groups.get(key) ?? []
    g.push(e)
    groups.set(key, g)
  }
  const out: DrawEdge[] = []
  for (const [, g] of groups) {
    g.forEach((e, k) => {
      const na = nodes.find((n) => n.id === e.u)
      const nb = nodes.find((n) => n.id === e.v)
      if (!na || !nb) return
      out.push({ ...e, d: edgePath(na, nb, k, g.length, GRAPH_NODE_R) })
    })
  }
  return out
})

const modeHint = computed(
  () =>
    ({
      draw: '点击空白处添加节点；依次点击两个节点连边（边类型取右侧当前类型，点边上的字母可切换类型）',
      move: '拖动节点调整布局',
      edit: '点击边选中，在下方修改类型或删除',
      delete: '点击节点（端口除外）或边删除',
    })[mode.value],
)
</script>

<template>
  <div class="geditor">
    <div class="ge-toolbar row tight">
      <div class="btn-group">
        <button
          v-for="m in (['draw', 'move', 'edit', 'delete'] as Mode[])"
          :key="m"
          class="btn sm"
          :class="{ primary: mode === m }"
          type="button"
          @click="((mode = m), (selectedEdge = null), (pendingNode = null))"
        >
          {{ { draw: '绘制', move: '拖动', edit: '编辑', delete: '删除' }[m] }}
        </button>
      </div>
      <div class="btn-group">
        <button
          v-for="k in (['R', 'L', 'C'] as CompKind[])"
          :key="k"
          class="btn sm kind-btn"
          :class="[`kind-${k}`, { sel: currentKind === k }]"
          type="button"
          title="新边的默认类型"
          @click="currentKind = k"
        >
          {{ k }}
        </button>
      </div>
      <div class="spacer" style="flex: 1" />
      <span class="qpill" :class="edgeLimitWarn || nodeLimitWarn ? 'warn' : ''">
        节点 {{ nodeCount }}（≤8 建议）· 边 {{ edgeCount }}（≤12 建议）
      </span>
      <button class="btn sm ghost" type="button" @click="loadLadderExample">示例 · 梯形</button>
      <button class="btn sm ghost" type="button" @click="loadParallelExample">示例 · 并联</button>
      <button class="btn sm ghost" type="button" @click="clearAll">清空</button>
    </div>

    <div class="ge-main">
      <div class="ge-text">
        <div class="ge-text-head">边表（u v 类型）</div>
        <textarea v-model="textPanel" spellcheck="false" rows="12" />
      </div>
      <div class="ge-canvas">
        <svg
          ref="svgEl"
          :viewBox="`0 0 ${W} ${H}`"
          class="ge-svg"
          @click="onCanvasClick"
          @pointermove="onPointerMove"
          @pointerup="onPointerUp"
          @pointerleave="onPointerUp"
        >
          <rect class="ge-bg" :width="W" :height="H" />
          <g v-for="e in drawEdges" :key="e.id" class="ge-edge" :class="{ sel: selectedEdge === e.id }">
            <path class="ge-wire" :d="e.d.path" />
            <path
              class="ge-wire-hit"
              :d="e.d.path"
              @click="onEdgeClick($event, e.id)"
            />
            <g
              class="ge-badge"
              :class="`k-${e.kind}`"
              :transform="`translate(${e.d.mx}, ${e.d.my})`"
              @click="onEdgeClick($event, e.id)"
            >
              <circle r="9.5" />
              <text y="3.2" text-anchor="middle">{{ e.kind }}</text>
            </g>
          </g>
          <g
            v-for="n in nodes"
            :key="n.id"
            class="ge-node"
            :class="{ port: n.id === 0 || n.id === 1, pending: pendingNode === n.id }"
            :transform="`translate(${n.x}, ${n.y})`"
            @pointerdown="onNodePointerDown($event, n.id)"
          >
            <circle :r="GRAPH_NODE_R" />
            <text y="4" text-anchor="middle">{{ n.id }}</text>
            <text v-if="n.id === 0 || n.id === 1" class="ge-port-tag" :y="GRAPH_NODE_R + 15" text-anchor="middle">
              端口{{ n.id }}
            </text>
          </g>
        </svg>
        <div class="ge-hint">{{ modeHint }}</div>
        <div v-if="selectedEdge !== null" class="ge-edge-editor">
          <template v-if="edges.find((e) => e.id === selectedEdge)">
            <span class="muted">
              边 {{ Math.min(edges.find((e) => e.id === selectedEdge)!.u, edges.find((e) => e.id === selectedEdge)!.v) }}
              —
              {{ Math.max(edges.find((e) => e.id === selectedEdge)!.u, edges.find((e) => e.id === selectedEdge)!.v) }}：
            </span>
            <button
              v-for="k in (['R', 'L', 'C'] as CompKind[])"
              :key="k"
              class="btn sm kind-btn"
              :class="[`kind-${k}`, { sel: edges.find((e) => e.id === selectedEdge)!.kind === k }]"
              type="button"
              @click="((edges.find((e) => e.id === selectedEdge)!.kind = k), pushUpdate())"
            >
              {{ k }}
            </button>
            <button class="btn sm" type="button" @click="deleteEdge(selectedEdge)">删除边</button>
          </template>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.geditor { display: flex; flex-direction: column; gap: 10px; }
.ge-toolbar { flex-wrap: wrap; }
.btn-group { display: inline-flex; }
.btn-group .btn { border-radius: 0; margin-left: -1px; }
.btn-group .btn:first-child { border-radius: var(--r-sm) 0 0 var(--r-sm); margin-left: 0; }
.btn-group .btn:last-child { border-radius: 0 var(--r-sm) var(--r-sm) 0; }

.kind-btn.sel.kind-R { border-color: var(--series-1); color: var(--series-1); background: rgba(59, 111, 182, .08); }
.kind-btn.sel.kind-L { border-color: var(--series-2); color: var(--series-2); background: rgba(217, 110, 43, .08); }
.kind-btn.sel.kind-C { border-color: var(--series-3); color: var(--series-3); background: rgba(33, 138, 99, .08); }

.ge-main { display: grid; grid-template-columns: 220px 1fr; gap: 10px; }
.ge-text { display: flex; flex-direction: column; gap: 4px; }
.ge-text-head { font-size: 10.5px; color: var(--text-3); font-weight: 600; }
.ge-text textarea {
  font-family: 'IBM Plex Mono', monospace;
  font-size: 11.5px;
  line-height: 1.6;
  resize: vertical;
  min-height: 220px;
}

.ge-canvas { position: relative; }
.ge-svg { width: 100%; height: auto; border: 1px solid var(--border); border-radius: var(--r-sm); display: block; background: var(--panel); }
.ge-bg { fill: var(--panel); cursor: crosshair; }

.ge-wire { stroke: var(--text-2); stroke-width: 1.5; fill: none; }
.ge-wire-hit { stroke: transparent; stroke-width: 14; fill: none; cursor: pointer; }
.ge-edge:hover .ge-badge circle, .ge-edge.sel .ge-badge circle { stroke-width: 2; }
.ge-edge.sel .ge-wire { stroke: var(--accent); stroke-width: 2; }

.ge-badge { cursor: pointer; }
.ge-badge circle { stroke-width: 1.5; }
.ge-badge.k-R circle { fill: rgba(59, 111, 182, .12); stroke: var(--series-1); }
.ge-badge.k-R text { fill: var(--series-1); }
.ge-badge.k-L circle { fill: rgba(217, 110, 43, .12); stroke: var(--series-2); }
.ge-badge.k-L text { fill: var(--series-2); }
.ge-badge.k-C circle { fill: rgba(33, 138, 99, .12); stroke: var(--series-3); }
.ge-badge.k-C text { fill: var(--series-3); }
.ge-badge text { font-size: 10px; font-weight: 700; font-family: 'IBM Plex Mono', monospace; }

.ge-node { cursor: pointer; }
.ge-node circle {
  fill: var(--surface);
  stroke: var(--text-2);
  stroke-width: 1.5;
}
.ge-node text { font-size: 11px; font-weight: 600; fill: var(--text); font-family: 'IBM Plex Mono', monospace; }
.ge-node.port circle { stroke: var(--accent); stroke-width: 2.2; fill: rgba(36, 86, 166, .07); }
.ge-node.port text { fill: var(--accent); }
.ge-node.pending circle { stroke: var(--warning); stroke-width: 2.5; stroke-dasharray: 3 2; }
.ge-node.pending text { fill: var(--warning); }
.ge-port-tag { font-size: 9.5px; fill: var(--accent); font-weight: 600; }
.ge-node:hover circle { stroke: var(--accent); }

.ge-hint { font-size: 10.5px; color: var(--text-3); margin-top: 6px; }
.ge-edge-editor {
  position: absolute;
  left: 8px;
  bottom: 8px;
  display: flex;
  align-items: center;
  gap: 6px;
  background: var(--surface);
  border: 1px solid var(--border-strong);
  border-radius: var(--r-sm);
  padding: 5px 8px;
  box-shadow: var(--shadow-sm);
}
</style>
