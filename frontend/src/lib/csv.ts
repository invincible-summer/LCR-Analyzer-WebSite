// csv.ts — measurement-data interchange for the fitting page.
//
// Canonical web format (DESIGN.md): CSV, one point per row,
//     frequency[Hz], Re(Z)[Ω], Im(Z)[Ω]
// comma-separated.  The parser is deliberately tolerant:
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
    if (fields.length !== 3) {
      errors.push(`第 ${points.length + errors.length + 1} 个数据行：需要 3 个字段（f, Re(Z), Im(Z)），实际 ${fields.length} 个`)
      if (errors.length > 8) break
      continue
    }
    if (!fields.every(isNumeric)) {
      if (!sawData) {
        warnings.push(`已跳过表头行：${line.slice(0, 60)}`)
        continue
      }
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
    points.push({ f, re, im })
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

/** Serialise back to the canonical CSV form (for the 导出 button). */
export function toZCsv(points: ZPoint[]): string {
  const rows = points.map((p) => `${p.f.toPrecision(10)},${p.re.toPrecision(10)},${p.im.toPrecision(10)}`)
  return `# f[Hz], Re(Z)[ohm], Im(Z)[ohm]\n${rows.join('\n')}\n`
}
