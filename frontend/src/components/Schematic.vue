<script setup lang="ts">
// Renders the Foster netlist tree as an SVG schematic (IEC-style symbols:
// resistor = rectangle, inductor = arcs, capacitor = plates). The layout is
// a deterministic recursive packing: series chains go left-to-right,
// parallel children stack vertically between two rails.
import { computed } from 'vue'
import type { Netlist } from '../api'
import { eng } from '../lib/format'

const props = defineProps<{ netlist: Netlist }>()

const CELL_W = 92
const CELL_H = 30
const GAP = 14
const PGAP = 10

interface Size { w: number; h: number }

function isSeries(n: Netlist): n is Extract<Netlist, { type: 'series' }> {
  return n.type === 'series'
}
function isParallel(n: Netlist): n is Extract<Netlist, { type: 'parallel' }> {
  return n.type === 'parallel'
}

function measure(n: Netlist): Size {
  if (isSeries(n)) {
    if (!n.children.length) return { w: CELL_W, h: CELL_H }
    const sizes = n.children.map(measure)
    return {
      w: sizes.reduce((a, s) => a + s.w, 0) + GAP * (sizes.length - 1),
      h: Math.max(...sizes.map((s) => s.h), CELL_H),
    }
  }
  if (isParallel(n)) {
    if (!n.children.length) return { w: CELL_W, h: CELL_H }
    const sizes = n.children.map(measure)
    return {
      w: Math.max(...sizes.map((s) => s.w)),
      h: sizes.reduce((a, s) => a + s.h, 0) + PGAP * (sizes.length - 1),
    }
  }
  return { w: CELL_W, h: CELL_H }
}

function esc(s: string): string {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
}

function countElements(n: Netlist): number {
  if (n.type === 'series' || n.type === 'parallel') {
    return n.children.reduce((a, c) => a + countElements(c), 0)
  }
  return 1
}

function draw(n: Netlist, x: number, y: number, out: string[]): Size {
  const sz = measure(n)
  const cy = y + sz.h / 2

  if (isSeries(n)) {
    let cx = x
    n.children.forEach((child, i) => {
      const csz = draw(child, cx, y + (sz.h - measure(child).h) / 2, out)
      if (i < n.children.length - 1) {
        // connecting wire handled by leaf stubs; add explicit wire
        out.push(`<line class="wire" x1="${cx + csz.w}" y1="${cy}" x2="${cx + csz.w + GAP}" y2="${cy}"/>`)
      }
      cx += csz.w + GAP
    })
    return sz
  }

  if (isParallel(n)) {
    const mids: number[] = []
    let cyy = y
    for (const child of n.children) {
      const cysz = measure(child)
      const childCy = cyy + cysz.h / 2
      mids.push(childCy)
      draw(child, x, cyy, out)
      cyy += cysz.h + PGAP
    }
    const mid = (Math.min(...mids) + Math.max(...mids)) / 2
    // left rail connects entry (x, mid) to every child entry; children are
    // drawn with their own entry stubs at (x, childCy), so the rail is a
    // vertical line spanning them
    out.push(`<line class="wire" x1="${x}" y1="${Math.min(...mids, mid)}" x2="${x}" y2="${Math.max(...mids, mid)}"/>`)
    out.push(`<line class="wire" x1="${x + sz.w}" y1="${Math.min(...mids, mid)}" x2="${x + sz.w}" y2="${Math.max(...mids, mid)}"/>`)
    out.push(`<circle class="node-dot" cx="${x}" cy="${mid}" r="2.3"/>`)
    out.push(`<circle class="node-dot" cx="${x + sz.w}" cy="${mid}" r="2.3"/>`)
    return sz
  }

  // leaf: stub wire + symbol + stub wire, labels above
  const symW = 40
  const stub = (sz.w - symW) / 2
  const sx = x + stub
  out.push(`<line class="wire" x1="${x}" y1="${cy}" x2="${sx}" y2="${cy}"/>`)
  out.push(`<line class="wire" x1="${sx + symW}" y1="${cy}" x2="${x + sz.w}" y2="${cy}"/>`)

  const key = n.type as 'R' | 'L' | 'C'
  const val = (n as any)[key] as number
  const valStr = eng(val, key === 'R' ? 'Ω' : key === 'L' ? 'H' : 'F', 3)
  const label = `${key}<tspan class="comp-value" x="${sx + symW / 2}" dy="11">${esc(valStr)}</tspan>`

  if (key === 'R') {
    out.push(`<rect class="wire" x="${sx}" y="${cy - 8}" width="${symW}" height="16" rx="1"/>`)
  } else if (key === 'L') {
    const r = 6.7
    const step = symW / 3
    for (let i = 0; i < 3; i++) {
      const ax = sx + step * i + step / 2
      out.push(`<path class="wire" d="M ${ax - step / 2} ${cy} a ${r} ${r} 0 0 1 ${step} 0"/>`)
    }
    out.push(`<line class="wire" x1="${sx}" y1="${cy}" x2="${sx}" y2="${cy}" stroke-width="0"/>`)
  } else {
    const gap = 4
    out.push(`<line class="wire" x1="${sx + symW / 2 - gap}" y1="${cy - 10}" x2="${sx + symW / 2 - gap}" y2="${cy + 10}"/>`)
    out.push(`<line class="wire" x1="${sx + symW / 2 + gap}" y1="${cy - 10}" x2="${sx + symW / 2 + gap}" y2="${cy + 10}"/>`)
  }
  out.push(
    `<text class="comp-label" x="${sx + symW / 2}" y="${cy - 14}" text-anchor="middle">${label}</text>`,
  )
  return sz
}

const svg = computed(() => {
  const root = props.netlist
  if (!root) return ''
  const m = measure(root)
  const padX = 26
  const padY = 26
  const out: string[] = []
  // terminals
  out.push(`<line class="wire" x1="${padX - 14}" y1="${padY + m.h / 2}" x2="${padX}" y2="${padY + m.h / 2}"/>`)
  out.push(`<line class="wire" x1="${padX + m.w}" y1="${padY + m.h / 2}" x2="${padX + m.w + 14}" y2="${padY + m.h / 2}"/>`)
  draw(root, padX, padY, out)
  return (
    `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${m.w + padX * 2 + 14} ${m.h + padY * 2}" ` +
    `width="${m.w + padX * 2 + 14}" height="${m.h + padY * 2}">${out.join('')}</svg>`
  )
})

const nElements = computed(() => (props.netlist ? countElements(props.netlist) : 0))
</script>

<template>
  <div class="schematic">
    <div v-html="svg"></div>
  </div>
</template>
