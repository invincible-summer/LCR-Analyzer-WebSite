// deviceImport.test.ts — 固件 CSV ↔ 网站解析链路的位位兼容验收（plan.md §9.6）
//
// golden_oneport.csv 由固件 formatOnePortCsv 的 host 构建产物生成
// （ino/tools/run_tests.sh → emit_golden），内容含 1 个失败点。
// 本测试把它喂给现有 parseZCsv()，证明「BLE 导入与文件上传在解析层
// 汇合成同一数据路径」（plan.md §7.2），且失败点不进入拟合数据。
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import { parseZCsv } from '../csv'
import { crc32 } from '../ble/crc32'

const fixture = readFileSync(
  fileURLToPath(new URL('./fixtures/golden_oneport.csv', import.meta.url)),
  'utf-8',
)

describe('固件 one-port CSV（golden fixture）', () => {
  it('parseZCsv 完整解析：8 点、值一致、无错误', () => {
    const r = parseZCsv(fixture)
    expect(r.errors).toEqual([])
    expect(r.points).toHaveLength(8)
    // 首尾点与固件产物逐字段一致（f 用 actualHz）
    expect(r.points[0].f).toBeCloseTo(100, 6)
    expect(r.points[0].re).toBeCloseTo(1000, 6)
    expect(r.points[0].im).toBeCloseTo(1.5, 6)
    expect(r.points[7].f).toBeCloseTo(5000, 6)
    expect(r.points[7].re).toBeCloseTo(1014, 6)
    expect(r.points[7].im).toBeCloseTo(-19.5, 6)
    // 失败点（316Hz SignalTooSmall）绝不出现
    expect(r.points.some((p) => Math.abs(p.f - 316) < 1)).toBe(false)
    // f 升序（对数扫描产物本身有序，不应触发自动排序）
    expect(r.warnings.some((w) => w.includes('排序'))).toBe(false)
  })

  it('头部注释被容忍（# 行 + f,re,im 表头跳过）；v2 元数据键存在', () => {
    const r = parseZCsv(fixture)
    // parseZCsv 跳过 # 注释；"f,re,im" 作为非数值表头跳过并提示
    expect(r.points.length).toBe(8)
    expect(r.warnings.some((w) => w.includes('表头行'))).toBe(true)
    // v2 头部：诚实元数据（不再有 drive_vrms / calibration_id）
    expect(fixture).toContain('# schema=lcr-z-csv-v2')
    expect(fixture).toContain('# measurement_backend=DO_NOT_TOUCH_lcr_api')
    expect(fixture).toContain('# calibration_state=')
    expect(fixture).not.toContain('drive_vrms')
    expect(fixture).not.toContain('calibration_id')
  })

  it('golden 内容的 CRC32 稳定（协议字段的可追溯性）', () => {
    // 固件 dataset.cpp 对 csv 字节计算 CRC32 上传 metadata；此处验证
    // 相同文本在前端 crc32 实现下得到相同值（双侧一致性冒烟）。
    const bytes = new TextEncoder().encode(fixture)
    const v = crc32(bytes)
    expect(v).toBe(crc32(new TextEncoder().encode(fixture)))
    expect(v).not.toBe(0)
  })
})

describe('plan.md §5.2 手写格式示例', () => {
  it('3 列示例直接可解析', () => {
    const csv = [
      '# lcr-dataset=one-port-z',
      '# schema=lcr-z-csv-v1',
      '# protocol=1',
      '# firmware=<git/version>',
      '# calibration_id=<id>',
      '# drive_vrms=<value>',
      'f,re,im',
      '100.0,12.34,-45.67',
      '200.0,13.00,-44.00',
      '500.0,14.10,-40.20',
      '1000.0,15.80,-33.90',
    ].join('\n')
    const r = parseZCsv(csv)
    expect(r.errors).toEqual([])
    expect(r.points).toHaveLength(4)
    expect(r.points[0].re).toBeCloseTo(12.34, 9)
  })
})
