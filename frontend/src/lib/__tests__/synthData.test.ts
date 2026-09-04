import { describe, expect, it } from 'vitest'
import { synthPoints, evalNetlistZ, DEMO_CASES } from '../synthData'

describe('synthPoints', () => {
  it('generates the requested count on a log grid', () => {
    const pts = synthPoints(DEMO_CASES[0].net, { fMin: 100, fMax: 1e6, n: 37, noise: 0 })
    expect(pts).toHaveLength(37)
    expect(pts[0].f).toBeCloseTo(100, 6)
    expect(pts[36].f).toBeCloseTo(1e6, 3)
    for (let i = 1; i < pts.length; i++) expect(pts[i].f).toBeGreaterThan(pts[i - 1].f)
  })

  it('noise=0 reproduces the exact netlist impedance', () => {
    const net = DEMO_CASES[0].net // series RLC
    const pts = synthPoints(net, { noise: 0 })
    for (const p of pts) {
      const z = evalNetlistZ(net, 2 * Math.PI * p.f)
      expect(p.re).toBeCloseTo(z.re, 10)
      expect(p.im).toBeCloseTo(z.im, 10)
    }
  })

  it('noise perturbs within the requested bound and is deterministic', () => {
    const a = synthPoints(DEMO_CASES[1].net, { noise: 0.01, seed: 42 })
    const b = synthPoints(DEMO_CASES[1].net, { noise: 0.01, seed: 42 })
    expect(a).toEqual(b)
    const clean = synthPoints(DEMO_CASES[1].net, { noise: 0 })
    const rel = a.map((p, i) => Math.abs(p.re - clean[i].re) / Math.abs(clean[i].re || 1))
    expect(Math.max(...rel)).toBeLessThanOrEqual(0.0101)
  })

  it('series RLC demo hits the DCR floor at DC-ish frequencies', () => {
    const z = evalNetlistZ(DEMO_CASES[0].net, 1e-3) // ~DC
    expect(z.re).toBeCloseTo(52, 3) // 50 + DCR 2
  })
})
