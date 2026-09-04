<script setup lang="ts">
// GraphSchematic.vue — read-only generic multigraph circuit rendering (the
// fallback when a result adjacency is NOT series-parallel — e.g. Try2 bridge
// wirings — and the primary renderer for Try3 results, whose layout mirrors
// the editor's).  Nodes on a circle (ports 0/1 left/right), each edge carries
// an IEC symbol + engineering value; parallel edges fan out as arcs.
import { computed } from 'vue'
import type { Adjacency } from '../lib/fitTypes'
import { circleLayout, edgePath, GRAPH_NODE_R } from '../lib/graphLayout'
import { eng } from '../lib/format'

const props = defineProps<{ adjacency: Adjacency | null }>()

const W = 640
const H = 360

interface EdgeDraw {
  u: number
  v: number
  kind: 'R' | 'L' | 'C'
  value: string
  dcr?: number
  path: string
  mx: number
  my: number
  angle: number
}

const layout = computed(() => {
  const adj = props.adjacency
  if (!adj || adj.slots.length === 0) return { nodes: [], edges: [] as EdgeDraw[] }

  const ids = new Set<number>([0, 1])
  for (const s of adj.slots) {
    ids.add(s.u)
    ids.add(s.j)
  }
  const pos = new Map(circleLayout([...ids], W, H).map((n) => [n.id, n]))

  const edges: EdgeDraw[] = []
  for (const slot of adj.slots) {
    slot.edges.forEach((e, k) => {
      const a = pos.get(slot.u)
      const b = pos.get(slot.j)
      if (!a || !b) return
      const d = edgePath(a, b, k, slot.edges.length, GRAPH_NODE_R + 26)
      const angle = (Math.atan2(b.y - a.y, b.x - a.x) * 180) / Math.PI
      const value =
        e.t === 'L' && e.d > 0
          ? `${eng(e.p, 'H', 3)} · DCR ${eng(e.d, 'Ω', 2)}`
          : eng(e.p, e.t === 'R' ? 'Ω' : e.t === 'C' ? 'F' : 'H', 3)
      edges.push({
        u: slot.u,
        v: slot.j,
        kind: e.t,
        value,
        dcr: e.d > 0 ? e.d : undefined,
        path: d.path,
        mx: d.mx,
        my: d.my,
        angle,
      })
    })
  }
  return { nodes: [...pos.values()], edges }
})

function inductorBody(x: number, y: number): string {
  const w = 30
  const humps = 4
  const step = w / humps
  const r = step / 2
  let d = `M ${-w / 2} 0`
  for (let i = 0; i < humps; i++) d += ` a ${r} ${r} 0 0 1 ${step} 0`
  return d
}
</script>

<template>
  <div class="gschematic">
    <div v-if="!adjacency || adjacency.slots.length === 0" class="empty" style="padding: 30px">
      无电路数据
    </div>
    <svg v-else :viewBox="`0 0 ${W} ${H}`" class="gsvg" role="img" aria-label="电路图（图论视图）">
      <g v-for="(e, i) in layout.edges" :key="i">
        <path class="gs-wire" :d="e.path" />
        <g :transform="`translate(${e.mx}, ${e.my}) rotate(${e.angle})`">
          <rect v-if="e.kind === 'R'" class="gs-sym" x="-15" y="-8" width="30" height="16" rx="2" />
          <path v-else-if="e.kind === 'L'" class="gs-sym" :d="inductorBody(e.mx, e.my)" />
          <g v-else class="gs-c">
            <line class="gs-sym gs-plate" x1="-2.5" y1="-10" x2="-2.5" y2="10" />
            <line class="gs-sym gs-plate" x1="2.5" y1="-10" x2="2.5" y2="10" />
          </g>
        </g>
        <g
          class="gs-val"
          :class="`k-${e.kind}`"
          :transform="`translate(${e.mx}, ${e.my + (Math.abs(e.angle) > 90 ? 30 : -22)})`"
        >
          <text text-anchor="middle">{{ e.value }}</text>
        </g>
      </g>
      <g v-for="n in layout.nodes" :key="n.id" class="gs-node" :class="{ port: n.id === 0 || n.id === 1 }" :transform="`translate(${n.x}, ${n.y})`">
        <circle :r="GRAPH_NODE_R" />
        <text y="4" text-anchor="middle">{{ n.id }}</text>
        <text v-if="n.id === 0 || n.id === 1" class="gs-port-tag" :y="GRAPH_NODE_R + 15" text-anchor="middle">
          端口{{ n.id }}
        </text>
      </g>
    </svg>
  </div>
</template>

<style scoped>
.gschematic { display: flex; justify-content: center; overflow-x: auto; padding: 8px 4px; }
.gsvg { max-width: 100%; height: auto; font-family: 'IBM Plex Mono', monospace; user-select: none; }

.gs-wire { stroke: var(--text-2); stroke-width: 1.5; fill: none; }
.gs-sym { stroke: var(--text); stroke-width: 1.5; fill: var(--panel); stroke-linecap: round; }
.gs-plate { stroke-width: 2; }
.gs-val text {
  font-size: 10px;
  font-weight: 600;
  font-family: 'IBM Plex Mono', monospace;
}
.gs-val.k-R text { fill: var(--series-1); }
.gs-val.k-L text { fill: var(--series-2); }
.gs-val.k-C text { fill: var(--series-3); }

.gs-node circle { fill: var(--surface); stroke: var(--text-2); stroke-width: 1.5; }
.gs-node text { font-size: 11px; font-weight: 600; fill: var(--text); }
.gs-node.port circle { stroke: var(--accent); stroke-width: 2.2; fill: rgba(36, 86, 166, .07); }
.gs-node.port text { fill: var(--accent); }
.gs-port-tag { font-size: 9.5px; fill: var(--accent); font-weight: 600; }
</style>
