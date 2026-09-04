// synthData.ts — local demo measurement generation (no backend): evaluate a
// known RLC netlist on a log frequency grid, optionally with multiplicative
// relative noise.  Also hosts the small Netlist-tree Z evaluator reused by
// tests.

import type { Netlist } from '../api'
import type { ZPoint } from './fitTypes'

type Cx = { re: number; im: number }
const cx = (re: number, im: number): Cx => ({ re, im })
const add = (a: Cx, b: Cx): Cx => cx(a.re + b.re, a.im + b.im)
const mul = (a: Cx, b: Cx): Cx => cx(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re)
const div = (a: Cx, b: Cx): Cx => {
  const d = b.re * b.re + b.im * b.im
  return cx((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d)
}

/** Z(netlist, jω) — exact complex impedance of an SP tree. */
export function evalNetlistZ(net: Netlist, w: number): Cx {
  const jw = cx(0, w)
  switch (net.type) {
    case 'R':
      return cx(net.R, 0)
    case 'C':
      return div(cx(1, 0), mul(jw, cx(net.C, 0)))
    case 'L':
      return add(cx(net.dcr ?? 0, 0), mul(jw, cx(net.L, 0)))
    case 'series':
      return net.children.reduce((acc, c) => add(acc, evalNetlistZ(c, w)), cx(0, 0))
    case 'parallel': {
      // admittance sum: Z = 1 / Σ(1/Zᵢ)
      let yRe = 0
      let yIm = 0
      for (const c of net.children) {
        const z = evalNetlistZ(c, w)
        const d = z.re * z.re + z.im * z.im
        yRe += z.re / d
        yIm += -z.im / d
      }
      const d = yRe * yRe + yIm * yIm
      return cx(yRe / d, -yIm / d)
    }
  }
}

export interface DemoCase {
  key: string
  label: string
  net: Netlist
}

/** Built-in demo circuits (values chosen to sit inside all engines' boxes). */
export const DEMO_CASES: DemoCase[] = [
  {
    key: 'series_rlc',
    label: '串联 RLC（R 50Ω · L 1mH+DCR 2Ω · C 1µF）',
    net: {
      type: 'series',
      children: [
        { type: 'R', R: 50 },
        { type: 'L', L: 1e-3, dcr: 2 },
        { type: 'C', C: 1e-6 },
      ],
    },
  },
  {
    key: 'r_lpar_c',
    label: 'R + (L ∥ C) 并联谐振（R 100Ω · L 100µH · C 100nF）',
    net: {
      type: 'series',
      children: [
        { type: 'R', R: 100 },
        {
          type: 'parallel',
          children: [
            { type: 'L', L: 100e-6, dcr: 0.5 },
            { type: 'C', C: 100e-9 },
          ],
        },
      ],
    },
  },
  {
    key: 'ladder',
    label: '梯形：L — (C ∥ R)（L 1mH+DCR 1Ω · C 100nF · R 1kΩ）',
    net: {
      type: 'series',
      children: [
        { type: 'L', L: 1e-3, dcr: 1 },
        {
          type: 'parallel',
          children: [
            { type: 'C', C: 100e-9 },
            { type: 'R', R: 1e3 },
          ],
        },
      ],
    },
  },
  {
    key: 'parallel_rc',
    label: '并联 RC（R 10kΩ ∥ C 10nF）',
    net: {
      type: 'parallel',
      children: [
        { type: 'R', R: 1e4 },
        { type: 'C', C: 10e-9 },
      ],
    },
  },
]

/** Evaluate a demo netlist on a log grid with optional relative noise. */
export function synthPoints(net: Netlist, opts?: { fMin?: number; fMax?: number; n?: number; noise?: number; seed?: number }): ZPoint[] {
  const fMin = opts?.fMin ?? 100
  const fMax = opts?.fMax ?? 1e6
  const n = opts?.n ?? 60
  const noise = opts?.noise ?? 0
  let state = BigInt(opts?.seed ?? 20260904)
  const rand = () => {
    // xorshift64* — deterministic, no dependencies
    state ^= state << 13n
    state ^= state >> 7n
    state ^= state << 17n
    return Number((state * 0x2545f4914f6cdd1dn >> 11n) & 0xffffffn) / 0xffffff
  }
  const points: ZPoint[] = []
  for (let i = 0; i < n; i++) {
    const f = 10 ** (Math.log10(fMin) + (Math.log10(fMax) - Math.log10(fMin)) * (i / (n - 1)))
    const z = evalNetlistZ(net, 2 * Math.PI * f)
    const nz = noise > 0 ? 1 + noise * (rand() * 2 - 1) : 1
    points.push({ f, re: z.re * nz, im: z.im * nz })
  }
  return points
}
