<script setup lang="ts">
// Renders a true SVG circuit schematic from a recursive SP-tree netlist.
// Layout is handled by the pure engine in `lib/schematic.ts`; this component
// only maps the layout primitives to Vue SVG elements, ensuring perfect
// connectivity, polished styling, and automatic light/dark theme support.
import { computed } from 'vue'
import type { Netlist } from '../api'
import { layoutSchematic, type SchematicPrimitive } from '../lib/schematic'

const props = defineProps<{ netlist: Netlist | null }>()

const layout = computed(() => layoutSchematic(props.netlist))

const wires = computed(() => layout.value.primitives.filter((p) => p.kind === 'wire') as Extract<SchematicPrimitive, { kind: 'wire' }>[])
const dots = computed(() => layout.value.primitives.filter((p) => p.kind === 'dot') as Extract<SchematicPrimitive, { kind: 'dot' }>[])
const symbols = computed(() => layout.value.primitives.filter((p) => p.kind === 'symbol') as Extract<SchematicPrimitive, { kind: 'symbol' }>[])
const labels = computed(() => layout.value.primitives.filter((p) => p.kind === 'label') as Extract<SchematicPrimitive, { kind: 'label' }>[])
const terminals = computed(() => layout.value.primitives.filter((p) => p.kind === 'terminal') as Extract<SchematicPrimitive, { kind: 'terminal' }>[])

// Generate smooth IEC symbol SVG path data (4 semicircular humps for inductor)
function getInductorPath(x: number, y: number, width: number): string {
  const humps = 4
  const step = width / humps
  const radius = step / 2
  let d = `M ${x} ${y}`
  for (let i = 0; i < humps; i++) {
    d += ` a ${radius} ${radius} 0 0 1 ${step} 0`
  }
  return d
}
</script>

<template>
  <div class="schematic">
    <svg
      :viewBox="layout.viewBox"
      :width="layout.width"
      :height="layout.height"
      class="schematic-svg"
      role="img"
      aria-label="Equivalent circuit schematic"
    >
      <!-- Connecting wires -->
      <line
        v-for="(w, i) in wires"
        :key="`w-${i}`"
        class="sch-wire"
        :x1="w.x1"
        :y1="w.y1"
        :x2="w.x2"
        :y2="w.y2"
      />

      <!-- Solder dots at junctions -->
      <circle
        v-for="(d, i) in dots"
        :key="`d-${i}`"
        class="sch-dot"
        :cx="d.cx"
        :cy="d.cy"
        :r="d.r"
      />

      <!-- Component symbols -->
      <g v-for="(s, i) in symbols" :key="`s-${i}`">
        <!-- IEC Resistor: Rectangle -->
        <rect
          v-if="s.compKind === 'R'"
          class="sch-symbol sch-symbol-body"
          :x="s.x"
          :y="s.y - 10"
          :width="s.width"
          :height="20"
          rx="2"
        />
        <!-- IEC Inductor: Smooth semicircular arcs -->
        <path
          v-else-if="s.compKind === 'L'"
          class="sch-symbol"
          :d="getInductorPath(s.x, s.y, s.width)"
        />
        <!-- IEC Capacitor: Two parallel plates -->
        <g v-else-if="s.compKind === 'C'">
          <line
            class="sch-symbol sch-plate"
            :x1="s.x + s.width / 2 - 4"
            :y1="s.y - 12"
            :x2="s.x + s.width / 2 - 4"
            :y2="s.y + 12"
          />
          <line
            class="sch-symbol sch-plate"
            :x1="s.x + s.width / 2 + 4"
            :y1="s.y - 12"
            :x2="s.x + s.width / 2 + 4"
            :y2="s.y + 12"
          />
        </g>
      </g>

      <!-- Component labels -->
      <g v-for="(l, i) in labels" :key="`l-${i}`" class="sch-label-group">
        <text class="sch-label-des" :x="l.x" :y="l.y" text-anchor="middle">{{ l.designator }}</text>
        <text class="sch-label-val" :x="l.x" :y="l.y + 13" text-anchor="middle">{{ l.value }}</text>
      </g>

      <!-- Port Terminals -->
      <g v-for="(t, i) in terminals" :key="`t-${i}`" class="sch-terminal">
        <circle class="sch-term-circle" :cx="t.x" :cy="t.y" r="5.5" />
        <text
          class="sch-term-label"
          :x="t.x + (t.align === 'left' ? -14 : 14)"
          :y="t.y + 4"
          :text-anchor="t.align === 'left' ? 'end' : 'start'"
        >{{ t.name }}</text>
      </g>
    </svg>
  </div>
</template>

<style scoped>
.schematic {
  display: flex;
  justify-content: center;
  align-items: center;
  width: 100%;
  min-height: 80px;
}

.schematic-svg {
  max-width: 100%;
  height: auto;
  font-family: 'IBM Plex Mono', monospace;
  user-select: none;
}

.sch-wire {
  stroke: var(--text-2);
  stroke-width: 1.5;
  stroke-linecap: round;
  fill: none;
}

.sch-dot {
  fill: var(--text-2);
}

.sch-symbol {
  stroke: var(--text);
  stroke-width: 1.5;
  fill: none;
  stroke-linecap: round;
  stroke-linejoin: round;
}

.sch-symbol-body {
  fill: var(--panel);
}

.sch-plate {
  stroke-width: 2;
}

.sch-label-des {
  font-size: 11px;
  font-weight: 600;
  fill: var(--text);
}

.sch-label-val {
  font-size: 10.5px;
  fill: var(--text-3);
}

.sch-term-circle {
  fill: var(--panel);
  stroke: var(--text-2);
  stroke-width: 1.5;
}

.sch-term-label {
  font-size: 10px;
  font-weight: 600;
  fill: var(--text-3);
}
</style>
