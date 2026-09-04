// adjacency.ts — the unified adjacency matrix (OUTPUT_FORMAT.md) → the
// recursive series-parallel Netlist tree consumed by lib/schematic.ts.
//
// The engines emit an upper-triangle multigraph (nodes 0/1 = port); the
// journal-style schematic renders SP trees.  This module performs the
// classic two-terminal SP reduction:
//   S1  series merge — an internal node of degree 2 chains its two edges;
//   S2  parallel split — several live components hanging between the same
//       terminal pair become parallel children;
//   S3  series split — an internal articulation point separating the
//       terminal pair becomes a series chain link;
//   anything that survives all three (a Wheatstone bridge, say) is NOT
//   series-parallel → null, and the caller falls back to the generic graph
//   renderer (GraphSchematic).
// Dangling sub-graphs attached at a single terminal carry no current and are
// dropped (electrically exact; the engines pre-filter them anyway).

import type { Netlist } from '../api'
import type { Adjacency, AdjEdge } from './fitTypes'

interface WorkEdge {
  a: number
  b: number
  net: Netlist
}

interface Part {
  nodes: number[]
  edges: WorkEdge[]
}

function leaf(e: AdjEdge): Netlist {
  if (e.t === 'R') return { type: 'R', R: e.p }
  if (e.t === 'C') return { type: 'C', C: e.p }
  return { type: 'L', L: e.p, ...(e.d > 0 ? { dcr: e.d } : {}) }
}

function wrapParallel(children: Netlist[]): Netlist | null {
  if (children.length === 0) return null
  if (children.length === 1) return children[0]
  return { type: 'parallel', children }
}

/**
 * Connected components of the graph when `exclude` nodes (and any edge with
 * no non-excluded endpoint) are removed.  An edge joins the part of its
 * non-excluded endpoint(s); edges between two excluded nodes belong to no
 * part (the caller collects direct terminal edges before calling).
 */
function componentsOf(edges: WorkEdge[], exclude: Set<number>): Part[] {
  const nodes = new Set<number>()
  for (const e of edges) {
    if (!exclude.has(e.a)) nodes.add(e.a)
    if (!exclude.has(e.b)) nodes.add(e.b)
  }
  const parts: Part[] = []
  const visited = new Set<number>()
  for (const start of nodes) {
    if (visited.has(start)) continue
    const part: Part = { nodes: [], edges: [] }
    const queue = [start]
    visited.add(start)
    while (queue.length) {
      const n = queue.shift()!
      part.nodes.push(n)
      for (const e of edges) {
        const other = e.a === n ? e.b : e.b === n ? e.a : null
        if (other === null) continue
        if (!part.edges.includes(e)) part.edges.push(e)
        if (!visited.has(other) && !exclude.has(other)) {
          visited.add(other)
          queue.push(other)
        }
      }
    }
    parts.push(part)
  }
  return parts
}

function reducePair(u: number, v: number, edges: WorkEdge[], depth: number): Netlist | null {
  if (edges.length === 0 || depth > 128) return null

  const parChildren: Netlist[] = []
  const work = [...edges]

  const pullDirect = () => {
    for (let i = work.length - 1; i >= 0; i--) {
      const e = work[i]
      if ((e.a === u && e.b === v) || (e.a === v && e.b === u)) {
        parChildren.push(e.net)
        work.splice(i, 1)
      }
    }
  }
  pullDirect()

  // S1: chain internal degree-2 nodes into series edges (fixpoint)
  let progress = true
  while (progress) {
    progress = false
    const deg = new Map<number, number>()
    for (const e of work) {
      deg.set(e.a, (deg.get(e.a) ?? 0) + 1)
      deg.set(e.b, (deg.get(e.b) ?? 0) + 1)
    }
    for (const [w, d] of deg) {
      if (w === u || w === v || d !== 2) continue
      const i1 = work.findIndex((e) => e.a === w || e.b === w)
      const i2 = work.findIndex((e, i) => i !== i1 && (e.a === w || e.b === w))
      if (i1 < 0 || i2 < 0) continue
      const e1 = work[Math.min(i1, i2)]
      const e2 = work[Math.max(i1, i2)]
      const a = e1.a === w ? e1.b : e1.a
      const b = e2.a === w ? e2.b : e2.a
      work.splice(Math.max(i1, i2), 1)
      work.splice(Math.min(i1, i2), 1)
      if (a !== b) {
        work.push({ a, b, net: { type: 'series', children: [e1.net, e2.net] } })
      }
      progress = true
      break
    }
  }
  pullDirect()

  if (work.length === 0) return wrapParallel(parChildren)

  // S2: several live components between the same u–v pair → parallel split
  const parts = componentsOf(work, new Set([u, v]))
  const live = parts.filter(
    (p) =>
      p.edges.some((e) => e.a === u || e.b === u) &&
      p.edges.some((e) => e.a === v || e.b === v),
  )
  if (live.length >= 2 || (live.length === 1 && live[0].edges.length < work.length)) {
    const children = [...parChildren]
    for (const p of live) {
      const sub = reducePair(u, v, p.edges, depth + 1)
      if (!sub) return null
      children.push(sub)
    }
    const consumed = live.reduce((s, p) => s + p.edges.length, 0)
    // leftover edges belong to dangling parts (single terminal) — dropped
    if (consumed === work.length || live.length >= 2) {
      const dangling = work.length - consumed
      if (children.length > 0 && dangling >= 0) return wrapParallel(children)
    }
  }

  // S3: internal articulation point separating u from v → series split
  const internal = new Set<number>()
  for (const e of work) {
    if (e.a !== u && e.a !== v) internal.add(e.a)
    if (e.b !== u && e.b !== v) internal.add(e.b)
  }
  for (const w of internal) {
    const partsW = componentsOf(work, new Set([w]))
    const pu = partsW.find((p) => p.nodes.includes(u))
    const pv = partsW.find((p) => p.nodes.includes(v))
    if (!pu || !pv || pu === pv) continue
    const left = reducePair(u, w, pu.edges, depth + 1)
    if (!left) continue
    const right = reducePair(w, v, pv.edges, depth + 1)
    if (!right) continue
    const chain: Netlist = { type: 'series', children: [left, right] }
    return parChildren.length > 0 ? { type: 'parallel', children: [chain, ...parChildren] } : chain
  }

  return null // e.g. a Wheatstone bridge — not series-parallel
}

/** flatten nested series(series(a,b),c) → series(a,b,c) for clean schematics. */
function flatten(n: Netlist): Netlist {
  if (n.type === 'series' || n.type === 'parallel') {
    const out: Netlist[] = []
    for (const c of n.children) {
      const f = flatten(c)
      if (f.type === n.type) out.push(...f.children)
      else out.push(f)
    }
    return { type: n.type, children: out }
  }
  return n
}

/** Adjacency (upper-triangle multigraph, ports 0/1) → SP Netlist tree, or null if not SP. */
export function graphToNetlist(adj: Adjacency): Netlist | null {
  const edges: WorkEdge[] = []
  for (const slot of adj.slots) {
    for (const e of slot.edges) {
      edges.push({ a: slot.u, b: slot.j, net: leaf(e) })
    }
  }
  if (edges.length === 0) return null
  const n = reducePair(0, 1, edges, 0)
  return n ? flatten(n) : null
}
