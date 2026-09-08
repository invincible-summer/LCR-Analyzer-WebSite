import { describe, expect, it } from 'vitest'
import { polarToCartesianCov, usablePolarUncertainty } from '../uncertainty'

const pts = [
  { f: 10, re: 1000, im: 0 },
  { f: 100, re: 0, im: 2000 },
  { f: 1000, re: -500, im: 500 },
  { f: 10000, re: 3, im: 4 },
]

describe('polar → Cartesian covariance bridge', () => {
  it('marks only complete positive sigmas as usable', () => {
    expect(usablePolarUncertainty({ rho: [1, 1, 1, 1], phi: [1, 1, 1, 1] })).toBe(true)
    expect(usablePolarUncertainty({ rho: [1, 0, 1, 1], phi: [1, 1, 1, 1] })).toBe(false)
    expect(usablePolarUncertainty({ rho: [1, 1], phi: [1, 1, 1, 1] })).toBe(false)
    expect(usablePolarUncertainty({ rho: [], phi: [] })).toBe(false)
  })

  it('matches the J·diag·Jᵀ propagation for pure magnitude uncertainty', () => {
    const out = polarToCartesianCov(pts, { rho: [10, 10, 10, 10], phi: [1, 1, 1, 1] })!
    expect(out).not.toBeNull()
    // point 1 lies on the positive real axis: rr = σρ², ii = ρ²σφ²
    expect(out[0].cov!.rr).toBeCloseTo(100, 6)
    expect(out[0].cov!.ii).toBeCloseTo(1e6 * (Math.PI / 180) ** 2, 0)
    expect(out.every(p => p.cov!.source === 'scan_polar_approx')).toBe(true)
    // every converted matrix stays SPD
    for (const p of out) {
      const c = p.cov!
      expect(c.rr * c.ii - c.ri * c.ri).toBeGreaterThan(0)
    }
  })

  it('keeps pure phase uncertainty consistent with the manual formula', () => {
    const out = polarToCartesianCov([{ f: 5, re: 0, im: 100 }], { rho: [0.5], phi: [2] })!
    const sfp = (2 * Math.PI) / 180
    // φ = 90°: rr = ρ²σφ², ii = σρ²
    expect(out[0].cov!.rr).toBeCloseTo(1e4 * sfp * sfp, 6)
    expect(out[0].cov!.ii).toBeCloseTo(0.25, 6)
  })

  it('returns null for any invalid point (all-or-nothing)', () => {
    expect(polarToCartesianCov(pts, { rho: [1, 1, 1, 0], phi: [1, 1, 1, 1] })).toBeNull()
    expect(polarToCartesianCov(pts, { rho: [1, 1, 1, 1], phi: [1, -1, 1, 1] })).toBeNull()
    expect(polarToCartesianCov([{ f: 1, re: 0, im: 0 }], { rho: [1], phi: [1] })).toBeNull()
    expect(polarToCartesianCov(pts, { rho: [1, 1], phi: [1, 1] })).toBeNull()
  })
})
