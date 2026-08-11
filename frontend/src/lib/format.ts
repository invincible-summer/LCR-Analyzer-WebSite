// Number / unit formatting helpers for instrument-style readouts.

export function fmt(x: number | null | undefined, digits = 4): string {
  if (x === null || x === undefined || Number.isNaN(x)) return '—'
  if (!Number.isFinite(x)) return '∞'
  const ax = Math.abs(x)
  if (ax !== 0 && (ax < 1e-3 || ax >= 1e6)) return x.toExponential(digits - 1)
  return x.toPrecision(digits).replace(/\.?0+$/, '')
}

const ENG_UNITS: [number, string][] = [
  [1e9, 'G'], [1e6, 'M'], [1e3, 'k'],
  [1, ''],
  [1e-3, 'm'], [1e-6, 'µ'], [1e-9, 'n'], [1e-12, 'p'], [1e-15, 'f'],
]

export function eng(x: number | null | undefined, unit = '', digits = 3): string {
  if (x === null || x === undefined || Number.isNaN(x)) return '—'
  if (!Number.isFinite(x)) return '∞'
  if (x === 0) return `0 ${unit}`.trim()
  const sign = x < 0 ? '-' : ''
  const ax = Math.abs(x)
  for (const [factor, prefix] of ENG_UNITS) {
    if (ax >= factor) {
      const v = (x / factor).toPrecision(digits).replace(/\.?0+$/, '')
      return `${sign}${Math.abs(parseFloat(v))}${prefix ? ' ' + prefix : ''} ${unit}`.trim().replace(/^-\s*/, sign)
    }
  }
  return `${x.toExponential(digits)} ${unit}`.trim()
}

export function deg(rad: number | null | undefined, digits = 2): string {
  if (rad === null || rad === undefined || Number.isNaN(rad)) return '—'
  return `${(rad * 180 / Math.PI).toFixed(digits)}°`
}

export function degFromDeg(d: number | null | undefined, digits = 2): string {
  if (d === null || d === undefined || Number.isNaN(d)) return '—'
  return `${d.toFixed(digits)}°`
}

export function pct(x: number | null | undefined, digits = 2): string {
  if (x === null || x === undefined || Number.isNaN(x)) return '—'
  return `${(x * 100).toFixed(digits)}%`
}

export function fmtHz(f: number): string {
  return eng(f, 'Hz', 4)
}

export function fmtTime(t: number): string {
  // seconds
  return eng(t, 's', 3)
}
