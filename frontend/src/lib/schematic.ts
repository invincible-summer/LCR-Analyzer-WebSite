/**
 * Pure SVG layout engine for Foster/Series-Parallel RLC netlists.
 *
 * Converts a recursive Netlist tree ({type: 'series'|'parallel'|'R'|'L'|'C', ...})
 * into a flat list of renderable SVG primitives: wires, solder dots, IEC
 * component symbols, 2-line labels (designator + engineering value), and
 * port terminals (1 and 0, matching SPICE netlists).
 *
 * Guarantees:
 * - Deterministic, recursive bounding-box sizing.
 * - Every parallel branch extends to both rails — NO hanging wire ends.
 * - Solder dots placed at all T-junctions and branch merge points.
 * - Smooth IEC inductor arcs, rectangular IEC resistor, parallel plates for C.
 * - Designators (R1, R2, L1, C1...) numbered in DFS order, consistent with SPICE output.
 */

import type { Netlist } from '../api'
import { eng } from './format'

export type ComponentKind = 'R' | 'L' | 'C'

export interface WirePrimitive {
  kind: 'wire'
  x1: number
  y1: number
  x2: number
  y2: number
}

export interface DotPrimitive {
  kind: 'dot'
  cx: number
  cy: number
  r: number
}

export interface SymbolPrimitive {
  kind: 'symbol'
  compKind: ComponentKind
  x: number     // left coordinate of symbol body (excluding leads)
  y: number     // centerline Y of symbol
  width: number // symbol body width
}

export interface LabelPrimitive {
  kind: 'label'
  designator: string // e.g. "R1"
  value: string      // e.g. "12.4 kΩ"
  x: number          // center X
  y: number          // baseline Y of the top line (designator)
}

export interface TerminalPrimitive {
  kind: 'terminal'
  x: number
  y: number
  name: string // "1" | "0"
  align: 'left' | 'right'
}

export type SchematicPrimitive =
  | WirePrimitive
  | DotPrimitive
  | SymbolPrimitive
  | LabelPrimitive
  | TerminalPrimitive

export interface SchematicLayout {
  width: number
  height: number
  viewBox: string
  primitives: SchematicPrimitive[]
  elementCount: number
}

// Layout geometry constants (all in px, designed for crisp 1.5px strokes)
const LEAF_W = 100         // Total leaf cell width (lead + 44px symbol + lead)
const SYM_W = 44           // Width of the component symbol body
const LEAF_H = 58          // Total leaf height: 26px label zone above + 32px below
const LEAF_ENTRY_Y = 38    // Centerline Y within a leaf box (leaves 26px for 2-line label)

const PARALLEL_PAD_X = 22  // Horizontal rail margin before and after branches
const PARALLEL_ROW_GAP = 14// Vertical spacing between parallel branch boxes
const SERIES_GAP = 0       // Series children connect flush; internal leads provide spacing

const PADDING_X = 46       // Outer canvas padding for port stubs and terminal labels
const PADDING_Y = 24       // Outer canvas vertical padding
const TERMINAL_STUB = 20   // Length of lead wire connecting port terminal to root

interface BoxSize {
  w: number
  h: number
  entryY: number // Y offset from top of box to the horizontal entry/exit centerline
}

interface CounterState {
  R: number
  L: number
  C: number
}

function isSeries(n: Netlist): n is Extract<Netlist, { type: 'series' }> {
  return n.type === 'series'
}

function isParallel(n: Netlist): n is Extract<Netlist, { type: 'parallel' }> {
  return n.type === 'parallel'
}

function isLeaf(n: Netlist): n is Extract<Netlist, { type: 'R' | 'L' | 'C' }> {
  return n.type === 'R' || n.type === 'L' || n.type === 'C'
}

/**
 * First pass: recursively compute the bounding box and entry centerline of every subtree.
 */
function measure(n: Netlist): BoxSize {
  if (isLeaf(n)) {
    return { w: LEAF_W, h: LEAF_H, entryY: LEAF_ENTRY_Y }
  }

  if (isSeries(n)) {
    if (!n.children.length) {
      return { w: 40, h: LEAF_H, entryY: LEAF_ENTRY_Y } // empty series wire
    }
    const sizes = n.children.map(measure)
    const maxAbove = Math.max(...sizes.map((s) => s.entryY))
    const maxBelow = Math.max(...sizes.map((s) => s.h - s.entryY))
    const totalW = sizes.reduce((sum, s) => sum + s.w, 0) + SERIES_GAP * (sizes.length - 1)
    return {
      w: totalW,
      h: maxAbove + maxBelow,
      entryY: maxAbove,
    }
  }

  if (isParallel(n)) {
    if (!n.children.length) {
      return { w: 40, h: LEAF_H, entryY: LEAF_ENTRY_Y }
    }
    const sizes = n.children.map(measure)
    const maxBranchW = Math.max(...sizes.map((s) => s.w))
    const totalBranchH = sizes.reduce((sum, s) => sum + s.h, 0) + PARALLEL_ROW_GAP * (sizes.length - 1)
    // Entry centerline: with an odd branch count, align with the middle branch
    // so the port stub lands exactly on a branch row (no extra rail tap).
    let entryY = totalBranchH / 2
    if (sizes.length % 2 === 1) {
      const mid = (sizes.length - 1) / 2
      let offset = 0
      for (let i = 0; i < mid; i++) offset += sizes[i].h + PARALLEL_ROW_GAP
      entryY = offset + sizes[mid].entryY
    }
    return {
      w: maxBranchW + PARALLEL_PAD_X * 2,
      h: totalBranchH,
      entryY,
    }
  }

  return { w: LEAF_W, h: LEAF_H, entryY: LEAF_ENTRY_Y }
}

function getUnit(kind: ComponentKind): string {
  switch (kind) {
    case 'R': return 'Ω'
    case 'L': return 'H'
    case 'C': return 'F'
  }
}

/**
 * Second pass: place primitives.
 * (x, y) is the top-left of the bounding box assigned to subtree `n`.
 */
function layoutNode(
  n: Netlist,
  x: number,
  y: number,
  prims: SchematicPrimitive[],
  counters: CounterState,
): BoxSize {
  const sz = measure(n)
  const cy = y + sz.entryY

  if (isLeaf(n)) {
    const kind = n.type as ComponentKind
    counters[kind] += 1
    const designator = `${kind}${counters[kind]}`
    const rawVal = (n as any)[kind] as number
    // A real inductor (L + series DCR) stays ONE device: the DCR rides along
    // in the value line instead of becoming a separate R symbol.
    const dcr = kind === 'L' ? (n as { dcr?: number }).dcr : undefined
    const valStr =
      kind === 'L' && dcr && dcr > 0
        ? `${eng(rawVal, 'H', 3)} · DCR ${eng(dcr, 'Ω', 2)}`
        : eng(rawVal, getUnit(kind), 3)

    const stub = (LEAF_W - SYM_W) / 2
    const sx = x + stub

    // Lead wires (left & right)
    prims.push({ kind: 'wire', x1: x, y1: cy, x2: sx, y2: cy })
    prims.push({ kind: 'wire', x1: sx + SYM_W, y1: cy, x2: x + LEAF_W, y2: cy })

    // IEC Component symbol
    prims.push({ kind: 'symbol', compKind: kind, x: sx, y: cy, width: SYM_W })

    // 2-line Label (designator on top line, value on bottom line).
    // Value baseline sits 14px above the centerline: clear of the resistor
    // body (top at cy-10), inductor humps (cy-11) and capacitor plates (cy-12).
    prims.push({
      kind: 'label',
      designator,
      value: valStr,
      x: sx + SYM_W / 2,
      y: cy - 27, // designator baseline; value is rendered ~13px lower
    })

    return sz
  }

  if (isSeries(n)) {
    if (!n.children.length) {
      prims.push({ kind: 'wire', x1: x, y1: cy, x2: x + sz.w, y2: cy })
      return sz
    }

    let curX = x
    for (let i = 0; i < n.children.length; i++) {
      const child = n.children[i]
      const csz = measure(child)
      // Align each child's entryY with this series block's entryY (cy)
      const childY = cy - csz.entryY
      layoutNode(child, curX, childY, prims, counters)
      curX += csz.w + SERIES_GAP
    }
    return sz
  }

  if (isParallel(n)) {
    if (!n.children.length) {
      prims.push({ kind: 'wire', x1: x, y1: cy, x2: x + sz.w, y2: cy })
      return sz
    }

    const leftRailX = x + PARALLEL_PAD_X
    const rightRailX = x + sz.w - PARALLEL_PAD_X
    const branchEntryYs: number[] = []

    let curY = y
    for (const child of n.children) {
      const csz = measure(child)
      // Center branch horizontally inside the available inner span
      const innerW = sz.w - PARALLEL_PAD_X * 2
      const branchX = leftRailX + (innerW - csz.w) / 2
      const branchCy = curY + csz.entryY
      branchEntryYs.push(branchCy)

      layoutNode(child, branchX, curY, prims, counters)

      // CRITICAL: Extend horizontal lead wires from branch ends to both vertical rails.
      // This eliminates the dangling wire bug for branches narrower than the widest branch.
      if (branchX > leftRailX) {
        prims.push({ kind: 'wire', x1: leftRailX, y1: branchCy, x2: branchX, y2: branchCy })
      }
      if (branchX + csz.w < rightRailX) {
        prims.push({ kind: 'wire', x1: branchX + csz.w, y1: branchCy, x2: rightRailX, y2: branchCy })
      }

      curY += csz.h + PARALLEL_ROW_GAP
    }

    const topBranchY = Math.min(...branchEntryYs)
    const bottomBranchY = Math.max(...branchEntryYs)
    const minRailY = Math.min(topBranchY, cy)
    const maxRailY = Math.max(bottomBranchY, cy)

    // Left vertical rail & Right vertical rail
    prims.push({ kind: 'wire', x1: leftRailX, y1: minRailY, x2: leftRailX, y2: maxRailY })
    prims.push({ kind: 'wire', x1: rightRailX, y1: minRailY, x2: rightRailX, y2: maxRailY })

    // Input stub from outer left edge (x) to left rail, and exit stub to outer right edge
    prims.push({ kind: 'wire', x1: x, y1: cy, x2: leftRailX, y2: cy })
    prims.push({ kind: 'wire', x1: rightRailX, y1: cy, x2: x + sz.w, y2: cy })

    // Solder dots at every branch connection point on the rails
    for (const by of branchEntryYs) {
      prims.push({ kind: 'dot', cx: leftRailX, cy: by, r: 2.8 })
      prims.push({ kind: 'dot', cx: rightRailX, cy: by, r: 2.8 })
    }

    // Solder dots at input/output rail intersection if not already on a branch
    if (!branchEntryYs.some((by) => Math.abs(by - cy) < 1e-6)) {
      prims.push({ kind: 'dot', cx: leftRailX, cy, r: 2.8 })
      prims.push({ kind: 'dot', cx: rightRailX, cy, r: 2.8 })
    }

    return sz
  }

  return sz
}

export function countElements(n: Netlist | null | undefined): number {
  if (!n) return 0
  if (isLeaf(n)) return 1
  if (isSeries(n) || isParallel(n)) {
    return n.children.reduce((acc, c) => acc + countElements(c), 0)
  }
  return 0
}

/**
 * Generate full schematic layout with port terminals.
 */
export function layoutSchematic(root: Netlist | null | undefined): SchematicLayout {
  if (!root) {
    return {
      width: 200,
      height: 80,
      viewBox: '0 0 200 80',
      primitives: [],
      elementCount: 0,
    }
  }

  const prims: SchematicPrimitive[] = []
  const counters: CounterState = { R: 0, L: 0, C: 0 }
  const rootSize = measure(root)

  const circuitX = PADDING_X
  const circuitY = PADDING_Y
  layoutNode(root, circuitX, circuitY, prims, counters)

  const portY = circuitY + rootSize.entryY

  // Port 1 (left terminal, excitation/signal)
  const term1X = circuitX - TERMINAL_STUB
  prims.push({ kind: 'wire', x1: term1X, y1: portY, x2: circuitX, y2: portY })
  prims.push({ kind: 'terminal', x: term1X, y: portY, name: '1', align: 'left' })

  // Port 0 (right terminal, reference/ground)
  const rootRightX = circuitX + rootSize.w
  const term0X = rootRightX + TERMINAL_STUB
  prims.push({ kind: 'wire', x1: rootRightX, y1: portY, x2: term0X, y2: portY })
  prims.push({ kind: 'terminal', x: term0X, y: portY, name: '0', align: 'right' })

  const totalW = rootSize.w + PADDING_X * 2
  const totalH = rootSize.h + PADDING_Y * 2

  return {
    width: Math.ceil(totalW),
    height: Math.ceil(totalH),
    viewBox: `0 0 ${Math.ceil(totalW)} ${Math.ceil(totalH)}`,
    primitives: prims,
    elementCount: counters.R + counters.L + counters.C,
  }
}
