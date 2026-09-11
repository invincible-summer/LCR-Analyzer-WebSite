// twoPortCsv.test.ts — lcr-h-csv-v1 解析与复 H 派生量
import { describe, expect, it } from 'vitest'
import { parseHCsv } from '../twoPortCsv'

describe('parseHCsv（H = Vout/Vin 真源）', () => {
  it('plan.md §5.3 示例格式', () => {
    const csv = [
      '# lcr-dataset=two-port-h',
      '# schema=lcr-h-csv-v1',
      '# protocol=1',
      '# firmware=4.1.0',
      '# calibration_id=factory-none',
      '# drive_vrms=1.05',
      'f,re_h,im_h',
      '100.0,0.923,-0.146',
      '1591.5,0.5,-0.5',
      '15915.0,0.01,-0.1',
    ].join('\n')
    const r = parseHCsv(csv)
    expect(r.errors).toEqual([])
    expect(r.points).toHaveLength(3)
    expect(r.headers['schema']).toBe('lcr-h-csv-v1')
    expect(r.headers['drive_vrms']).toBe('1.05')

    const p0 = r.points[0]
    expect(p0.re).toBeCloseTo(0.923, 12)
    expect(p0.im).toBeCloseTo(-0.146, 12)
    expect(p0.gainDb).toBeCloseTo(20 * Math.log10(Math.hypot(0.923, -0.146)), 9)
    expect(p0.phaseDeg).toBeCloseTo((Math.atan2(-0.146, 0.923) * 180) / Math.PI, 9)

    // 拐点：|H| = 1/√2 → -3.01 dB，phase = -45°
    const fc = r.points[1]
    expect(fc.gainDb).toBeCloseTo(-3.0103, 3)
    expect(fc.phaseDeg).toBeCloseTo(-45, 6)
    // 高频 phase 趋近 -90° 且为负（H=Vout/Vin 约定）
    expect(r.points[2].phaseDeg).toBeLessThan(-80)
    expect(r.points[2].phaseDeg).toBeGreaterThan(-95)
  })

  it('RC 低通合成：低频 0dB、高频 -20dB/dec、phase ∈ (-90,0)', () => {
    const rows: string[] = ['# schema=lcr-h-csv-v1', 'f,re_h,im_h']
    const fc = 1000
    for (let d = 0; d <= 2; d += 0.25) {
      const f = 100 * 10 ** d
      const x = f / fc
      const re = 1 / (1 + x * x)
      const im = -x / (1 + x * x)
      rows.push(`${f.toFixed(4)},${re.toFixed(8)},${im.toFixed(8)}`)
    }
    const r = parseHCsv(rows.join('\n'))
    expect(r.errors).toEqual([])
    expect(r.points[0].gainDb).toBeGreaterThan(-0.05)   // fc/10：-0.043 dB
    expect(r.points[0].gainDb).toBeLessThanOrEqual(0)
    const last = r.points[r.points.length - 1]
    expect(last.gainDb).toBeLessThan(-15)     // 100×fc → ~-40dB
    for (const p of r.points) {
      expect(p.phaseDeg).toBeLessThanOrEqual(0)
      expect(p.phaseDeg).toBeGreaterThan(-90)
    }
  })

  it('schema 不受支持时明确报错', () => {
    const r = parseHCsv('# schema=lcr-h-csv-v9\nf,re_h,im_h\n100,1,0\n200,0.5,-0.5\n')
    expect(r.errors[0]).toMatch(/schema 不受支持/)
  })

  it('非法行 / 点数不足 / 频率排序', () => {
    const r = parseHCsv('f,re_h,im_h\n100,1\n-5,1,0\n200,0.5,0\n')
    expect(r.errors.length).toBeGreaterThan(0)
    const few = parseHCsv('f,re_h,im_h\n100,1,0\n')
    expect(few.errors[0]).toMatch(/有效数据点不足/)
    const unsorted = parseHCsv('f,re_h,im_h\n200,0.5,0\n100,1,0\n')
    expect(unsorted.warnings.some((w) => w.includes('排序'))).toBe(true)
    expect(unsorted.points[0].f).toBe(100)
  })
})

// ---- v2 golden fixture（固件 formatTwoPortCsv host 产物，run_tests.sh 生成）----
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'

const twoPortFixture = readFileSync(
  fileURLToPath(new URL('./fixtures/golden_twoport.csv', import.meta.url)),
  'utf-8',
)

describe('固件 two-port CSV v2（golden fixture）', () => {
  it('parseHCsv 完整解析 v2 头部 + RC 低通物理趋势', () => {
    const r = parseHCsv(twoPortFixture)
    expect(r.errors).toEqual([])
    expect(r.points).toHaveLength(7)
    expect(r.headers['schema']).toBe('lcr-h-csv-v2')
    expect(r.headers['measurement_backend']).toBe('DO_NOT_TOUCH_lcr_api')
    expect(r.headers['calibration_state']).toBe('raw_w_path')
    // 一阶 RC 低通（fc≈1591.5Hz）：低频 |H|≈1，高频单调下降
    expect(r.points[0].gainDb).toBeGreaterThan(-0.5)
    const last = r.points[r.points.length - 1]
    expect(last.gainDb).toBeLessThan(-10)
    expect(r.points.every((p) => p.phaseDeg < 0 && p.phaseDeg > -90)).toBe(true)
  })
  it('v1 历史文件（calibration_id 头）仍可读', () => {
    const v1 = [
      '# lcr-dataset=two-port-h',
      '# schema=lcr-h-csv-v1',
      '# calibration_id=factory-none',
      'f,re_h,im_h',
      '100,0.9,0.1',
      '200,0.8,0.1',
    ].join(String.fromCharCode(10))
    const r = parseHCsv(v1)
    expect(r.errors).toEqual([])
    expect(r.points).toHaveLength(2)
    expect(r.headers['calibration_id']).toBe('factory-none')
  })
})
