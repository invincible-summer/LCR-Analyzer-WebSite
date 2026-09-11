// twoPortCsv.ts — 双端口 lcr-h-csv-v1 解析（H = Vout/Vin 复数真源）
//
// 设备到网站的真源是复数传递函数：
//   # lcr-dataset=two-port-h
//   # schema=lcr-h-csv-v1
//   ...
//   f,re_h,im_h
// gainDb = 20·log10|H|、phaseDeg = arg(H) 在 parser 内从复 H 推导
// （Bode/Nyquist 全部由复数 H 派生，不传可能符号不一致的派生量）。

export interface HPoint {
  f: number
  re: number
  im: number
  gainDb: number
  phaseDeg: number
}

export interface TwoPortParseResult {
  points: HPoint[]
  warnings: string[]
  errors: string[]
  /** 头部注释键值（calibration_id / drive_vrms / firmware 等） */
  headers: Record<string, string>
}

const isNumeric = (s: string) => s !== '' && Number.isFinite(Number(s))

function parseHeaderKV(line: string, out: Record<string, string>): void {
  const m = line.match(/^#\s*([A-Za-z0-9_]+)\s*=\s*(.*)$/)
  if (m) out[m[1]] = m[2].trim()
}

export function parseHCsv(text: string): TwoPortParseResult {
  const warnings: string[] = []
  const errors: string[] = []
  const points: HPoint[] = []
  const headers: Record<string, string> = {}
  let sawData = false

  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim()
    if (!line) continue
    if (line.startsWith('#')) {
      parseHeaderKV(line, headers)
      continue
    }
    const fields = line.includes(',') ? line.split(',') : line.split(/\s+/)
    if (!sawData && !fields.every(isNumeric)) {
      // 表头行（f,re_h,im_h）
      warnings.push(`已跳过表头行：${line.slice(0, 60)}`)
      continue
    }
    if (fields.length !== 3) {
      errors.push(`第 ${points.length + errors.length + 1} 行：需要 3 列 (f,re_h,im_h)，实际 ${fields.length}`)
      if (errors.length > 8) break
      continue
    }
    const [fs, res, ims] = fields.map(Number)
    if (!(fs > 0) || !Number.isFinite(fs) || !Number.isFinite(res) || !Number.isFinite(ims)) {
      errors.push(`第 ${points.length + 1} 点：数值非法（f 必须 > 0）`)
      continue
    }
    const gainDb = 20 * Math.log10(Math.hypot(res, ims))
    let phaseDeg = (Math.atan2(ims, res) * 180) / Math.PI
    if (phaseDeg <= -180) phaseDeg += 360
    if (phaseDeg > 180) phaseDeg -= 360
    points.push({ f: fs, re: res, im: ims, gainDb, phaseDeg })
    sawData = true
  }

  if (points.length < 2 && errors.length === 0)
    errors.push(`有效数据点不足：${points.length} 个（双端口曲线至少 2 个）`)

  const freqs = points.map((p) => p.f)
  const sorted = [...freqs].sort((a, b) => a - b)
  if (freqs.some((v, i) => v !== sorted[i])) {
    warnings.push('频率未按升序排列，已自动排序')
    points.sort((a, b) => a.f - b.f)
  }

  if (headers['schema'] && headers['schema'] !== 'lcr-h-csv-v1')
    errors.unshift(`schema 不受支持：${headers['schema']}（本站支持 lcr-h-csv-v1）`)

  return { points, warnings, errors, headers }
}
