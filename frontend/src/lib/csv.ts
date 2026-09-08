// csv.ts — measurement-data interchange for the fitting page.
//
// Canonical web format (DESIGN.md): CSV, one point per row,
//     frequency[Hz], Re(Z)[Ω], Im(Z)[Ω]
// optionally extended with a per-point Cartesian covariance:
//     f, re, im, cov_rr, cov_ri, cov_ii
// The whole file must be uniformly 3 or 6 columns; 6-column rows are
// checked for finiteness and positive definiteness. The parser is
// deliberately tolerant:
//   * `#` comments and blank lines are skipped;
//   * comma / semicolon / whitespace separated fields all parse;
//   * a leading single-integer line (AlgorithmLcr `measurements.txt`) is
//     detected and skipped so lab files drop in unmodified;
//   * a header row whose fields are non-numeric is skipped with a warning.

import type { ZPoint } from './fitTypes'

export interface ParseResult {
  points: ZPoint[]
  warnings: string[]
  errors: string[]
}

function splitFields(line: string): string[] {
  const trimmed = line.trim()
  if (trimmed.includes(',')) return trimmed.split(/[,;]+/)
  if (trimmed.includes(';')) return trimmed.split(';')
  return trimmed.split(/\s+/)
}

const isNumeric = (s: string) => s !== '' && Number.isFinite(Number(s))

export function parseZCsv(text: string): ParseResult {
  const warnings: string[] = []
  const errors: string[] = []
  const points: ZPoint[] = []
  let columns = 0 // 3 or 6, fixed by the first data row

  const lines = text.split(/\r?\n/)
  // measurements.txt compatibility: first meaningful line is a lone integer
  // equal to the announced point count — skip it.
  let sawData = false
  let skipCountHeader = false
  for (const raw of lines) {
    const line = raw.trim()
    if (!line || line.startsWith('#')) continue
    if (!sawData && !skipCountHeader) {
      const fields = splitFields(line)
      if (fields.length === 1 && /^\d+$/.test(fields[0])) {
        skipCountHeader = true
        warnings.push('检测到 measurements.txt 风格的点数行，已忽略')
        continue
      }
    }
    const fields = splitFields(line)
    if (!sawData && !fields.every(isNumeric)) {
      warnings.push(`已跳过表头行：${line.slice(0, 60)}`)
      continue
    }
    if (columns === 0 && (fields.length === 3 || fields.length === 6)) {
      columns = fields.length
      if (columns === 6) warnings.push('检测到 6 列协方差格式，将按 GLS 逐点白化使用')
    }
    if (fields.length !== columns) {
      errors.push(
        `第 ${points.length + errors.length + 1} 个数据行：整文件须统一 ${columns || 3} 或 6 列，实际 ${fields.length}`,
      )
      if (errors.length > 8) break
      continue
    }
    if (!fields.every(isNumeric)) {
      errors.push(`数据行无法解析：${line.slice(0, 60)}`)
      if (errors.length > 8) break
      continue
    }
    const f = Number(fields[0])
    const re = Number(fields[1])
    const im = Number(fields[2])
    if (!(f > 0)) {
      errors.push(`第 ${points.length + 1} 点：频率必须 > 0（得到 ${f}）`)
      continue
    }
    if (columns === 6) {
      const rr = Number(fields[3])
      const ri = Number(fields[4])
      const ii = Number(fields[5])
      if (!(rr > 0) || !(ii > 0) || !(rr * ii - ri * ri > 0)) {
        errors.push(`第 ${points.length + 1} 点：协方差矩阵不是正定矩阵`)
        continue
      }
      points.push({ f, re, im, cov: { rr, ri, ii, source: 'csv' } })
    } else points.push({ f, re, im })
    sawData = true
  }

  if (points.length < 4 && errors.length === 0) {
    errors.push(`有效数据点不足：${points.length} 个（至少 4 个）`)
  }

  const freqs = points.map((p) => p.f)
  const sorted = [...freqs].sort((a, b) => a - b)
  if (freqs.some((v, i) => v !== sorted[i])) {
    warnings.push('频率未按升序排列，已自动排序')
    points.sort((a, b) => a.f - b.f)
  }

  return { points, warnings, errors }
}

/** Serialise back to CSV; 6 columns only when every point carries covariance. */
export function toZCsv(points: ZPoint[]): string {
  const withCov = points.length > 0 && points.every((p) => p.cov)
  const n = (x: number) => x.toPrecision(10)
  const rows = withCov
    ? points.map((p) => `${n(p.f)},${n(p.re)},${n(p.im)},${n(p.cov!.rr)},${n(p.cov!.ri)},${n(p.cov!.ii)}`)
    : points.map((p) => `${n(p.f)},${n(p.re)},${n(p.im)}`)
  const header = withCov
    ? '# f[Hz], Re(Z)[ohm], Im(Z)[ohm], cov_rr, cov_ri, cov_ii'
    : '# f[Hz], Re(Z)[ohm], Im(Z)[ohm]'
  return `${header}\n${rows.join('\n')}\n`
}
