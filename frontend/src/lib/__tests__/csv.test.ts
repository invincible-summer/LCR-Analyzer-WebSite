import { describe, expect, it } from 'vitest'
import { parseZCsv, toZCsv } from '../csv'

describe('parseZCsv', () => {
  it('parses canonical comma CSV', () => {
    const r = parseZCsv('100,52.3,-0.62\n200,52.4,-1.25\n500,52.9,-3.11\n1000,54.8,-6.24\n')
    expect(r.errors).toEqual([])
    expect(r.points).toHaveLength(4)
    expect(r.points[0]).toEqual({ f: 100, re: 52.3, im: -0.62 })
  })

  it('accepts whitespace / semicolon separated fields', () => {
    const r = parseZCsv('1 2 3\n4;5;6\n7\t8\t9\n10 11 12\n')
    expect(r.errors).toEqual([])
    expect(r.points).toHaveLength(4)
  })

  it('skips # comments and blank lines, and a non-numeric header row', () => {
    const r = parseZCsv('# comment\n\nfreq,re,im\n100,1,2\n200,3,4\n300,5,6\n400,7,8\n')
    expect(r.errors).toEqual([])
    expect(r.warnings.some((w) => w.includes('表头'))).toBe(true)
    expect(r.points).toHaveLength(4)
  })

  it('detects and skips the measurements.txt count line', () => {
    const r = parseZCsv('4\n100,1,1\n200,2,2\n300,3,3\n400,4,4\n')
    expect(r.errors).toEqual([])
    expect(r.points).toHaveLength(4)
    expect(r.warnings.some((w) => w.includes('measurements.txt'))).toBe(true)
  })

  it('rejects f <= 0 and reports the point index', () => {
    const r = parseZCsv('100,1,1\n-5,2,2\n300,3,3\n400,4,4\n')
    expect(r.errors.join()).toContain('频率')
  })

  it('errors when fewer than 4 points', () => {
    const r = parseZCsv('100,1,1\n200,2,2\n300,3,3\n')
    expect(r.errors.length).toBeGreaterThan(0)
  })

  it('sorts unsorted frequencies and warns', () => {
    const r = parseZCsv('400,4,4\n100,1,1\n300,3,3\n200,2,2\n')
    expect(r.points.map((p) => p.f)).toEqual([100, 200, 300, 400])
    expect(r.warnings.some((w) => w.includes('排序'))).toBe(true)
  })

  it('round-trips through toZCsv', () => {
    const pts = [
      { f: 100, re: 1.5, im: -2.5 },
      { f: 200, re: 3, im: 4 },
      { f: 300, re: 5, im: 6 },
      { f: 400, re: 7, im: 8 },
    ]
    const r = parseZCsv(toZCsv(pts))
    expect(r.errors).toEqual([])
    expect(r.points).toHaveLength(4)
    expect(Math.abs(r.points[0].re - 1.5)).toBeLessThan(1e-6)
  })
})
