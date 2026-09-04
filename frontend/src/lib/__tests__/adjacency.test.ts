import { describe, expect, it } from 'vitest'
import { graphToNetlist } from '../adjacency'
import { evalNetlistZ } from '../synthData'
import type { Adjacency, CompKind } from '../fitTypes'

/** build an adjacency from raw slots: [u, j, edges] */
function adj(v: number, slots: [number, number, [CompKind, number, number][]][]): Adjacency {
  return {
    v,
    slots: slots.map(([u, j, edges]) => ({
      u,
      j,
      edges: edges.map(([t, p, d]) => ({ t, p, d })),
    })),
  }
}

function zAt(net: ReturnType<typeof graphToNetlist>, w: number): { re: number; im: number } {
  // evalNetlistZ takes non-null netlist
  return evalNetlistZ(net!, w)
}

describe('graphToNetlist · 串并联规约', () => {
  it('single edge → leaf', () => {
    const n = graphToNetlist(adj(2, [[0, 1, [['R', 100, 0]]]]))
    expect(n).toEqual({ type: 'R', R: 100 })
  })

  it('series chain (R-L-C) via degree-2 merge', () => {
    // 0 -R- 2 -L- 3 -C- 1  (merge order may vary → assert structure, not order)
    const n = graphToNetlist(
      adj(4, [
        [0, 2, [['R', 50, 0]]],
        [2, 3, [['L', 1e-3, 2]]],
        [3, 1, [['C', 1e-6, 0]]],
      ]),
    )
    expect(n?.type).toBe('series')
    const kids = n?.type === 'series' ? n.children : []
    expect(kids).toHaveLength(3)
    expect(kids.map((k) => k.type).sort()).toEqual(['C', 'L', 'R'])
    expect(kids.find((k) => k.type === 'L')).toEqual({ type: 'L', L: 1e-3, dcr: 2 })
  })

  it('parallel RC → parallel node', () => {
    const n = graphToNetlist(adj(2, [[0, 1, [['R', 1e4, 0], ['C', 10e-9, 0]]]]))
    expect(n).toMatchObject({ type: 'parallel' })
    expect(n?.type === 'parallel' ? n.children : []).toHaveLength(2)
  })

  it('R + (L ∥ C) splits at articulation node', () => {
    // 0 -R- 2;  2 -L- 1, 2 -C- 1
    const n = graphToNetlist(
      adj(3, [
        [0, 2, [['R', 100, 0]]],
        [2, 1, [['L', 100e-6, 0.5], ['C', 100e-9, 0]]],
      ]),
    )
    expect(n?.type).toBe('series')
    const ser = n?.type === 'series' ? n.children : []
    expect(ser.map((k) => k.type).sort()).toEqual(['R', 'parallel'])
    const par = ser.find((k) => k.type === 'parallel')
    expect(par && par.type === 'parallel' ? par.children.map((c) => c.type).sort() : []).toEqual(['C', 'L'])
    expect(par && par.type === 'parallel' ? par.children.find((c) => c.type === 'L') : null).toEqual({
      type: 'L',
      L: 100e-6,
      dcr: 0.5,
    })
  })

  it('parallel of series: (R∥C)+L via nested articulation', () => {
    // 0 -R- 2, 0 -C- 2, 2 -L- 1
    const n = graphToNetlist(
      adj(3, [
        [0, 2, [['R', 1e3, 0], ['C', 1e-7, 0]]],
        [2, 1, [['L', 1e-3, 1]]],
      ]),
    )
    expect(n).toMatchObject({ type: 'series' })
    const ser = n?.type === 'series' ? n : null
    expect(ser).not.toBeNull()
    // left = parallel(R, C), right = L
    expect(ser!.children[0]).toMatchObject({ type: 'parallel' })
    expect(ser!.children[1]).toEqual({ type: 'L', L: 1e-3, dcr: 1 })
  })

  it('direct edge + series path combine as parallel', () => {
    // 0 -R- 1 and 0 -L- 2 -C- 1
    const n = graphToNetlist(
      adj(3, [
        [0, 1, [['R', 10, 0]]],
        [0, 2, [['L', 1e-3, 0]]],
        [2, 1, [['C', 1e-6, 0]]],
      ]),
    )
    expect(n).toMatchObject({ type: 'parallel' })
    const par = n?.type === 'parallel' ? n.children : []
    expect(par).toHaveLength(2)
    expect(par.some((c) => c.type === 'R')).toBe(true)
    expect(par.some((c) => c.type === 'series')).toBe(true)
  })

  it('Wheatstone bridge → null (not SP)', () => {
    // classic bridge: 0-a,0-b,a-1,b-1,a-b
    const n = graphToNetlist(
      adj(4, [
        [0, 2, [['R', 1, 0]]],
        [0, 3, [['R', 1, 0]]],
        [2, 1, [['R', 1, 0]]],
        [3, 1, [['R', 1, 0]]],
        [2, 3, [['R', 1, 0]]],
      ]),
    )
    expect(n).toBeNull()
  })

  it('electrical equivalence: conversion preserves Z', () => {
    // series RLC chain vs hand-built netlist
    const n = graphToNetlist(
      adj(4, [
        [0, 2, [['R', 50, 0]]],
        [2, 3, [['L', 1e-3, 2]]],
        [3, 1, [['C', 1e-6, 0]]],
      ]),
    )
    const w = 2 * Math.PI * 5e3
    const z = zAt(n, w)
    // analytic: 50 + (2 + jwL) + 1/(jwC)
    const zc = -1 / (w * 1e-6)
    expect(z.re).toBeCloseTo(52, 6)
    expect(z.im).toBeCloseTo(w * 1e-3 + zc, 4)
  })
})

describe('evalNetlistZ', () => {
  it('parallel of identical Rs halves the impedance', () => {
    const net = { type: 'parallel' as const, children: [{ type: 'R' as const, R: 100 }, { type: 'R' as const, R: 100 }] }
    const z = evalNetlistZ(net, 1)
    expect(z.re).toBeCloseTo(50, 9)
    expect(z.im).toBeCloseTo(0, 9)
  })
})
