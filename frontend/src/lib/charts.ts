// ECharts option builders. Each takes the active palette so charts re-render
// correctly on theme switch. Options are plain objects (typed loosely).

import type { Palette } from './palette'

type AnyOpt = Record<string, any>

function axisStyle(p: Palette, log = false): AnyOpt {
  return {
    type: log ? 'log' : 'value',
    axisLine: { lineStyle: { color: p.baseline } },
    axisTick: { lineStyle: { color: p.baseline } },
    axisLabel: { color: p.text3, fontSize: 11, hideOverlap: true },
    splitLine: { show: true, lineStyle: { color: p.grid } },
  }
}

function tooltip(p: Palette, trigger: 'axis' | 'item' = 'axis'): AnyOpt {
  const base: AnyOpt = {
    trigger,
    backgroundColor: p.surface,
    borderColor: p.border,
    borderWidth: 1,
    textStyle: { color: p.text, fontSize: 12 },
  }
  if (trigger === 'axis') {
    base.axisPointer = {
      type: 'cross',
      lineStyle: { color: p.text3, width: 1, type: 'dashed' },
      label: { color: p.text, backgroundColor: p.surface, fontSize: 11 },
    }
  }
  return base
}

function grid(): AnyOpt {
  return { left: 60, right: 18, top: 28, bottom: 38 }
}

export interface LineSeries {
  name: string
  data: [number, number][]     // [x, y]
  color: string
  kind?: 'scatter' | 'line'
  dashed?: boolean
  width?: number
  markLine?: AnyOpt
}

export function waveformOpt(p: Palette, opts: {
  series: LineSeries[]
  xLabel?: string
  yLabel?: string
  xFormatter?: (v: number) => string
  yFormatter?: (v: number) => string
}): AnyOpt {
  const multi = opts.series.length > 1
  return {
    animation: false,
    grid: grid(),
    legend: multi
      ? { show: true, textStyle: { color: p.text2, fontSize: 11 }, top: 0, itemWidth: 14, itemHeight: 3, icon: 'roundRect' }
      : { show: false },
    tooltip: {
      ...tooltip(p, 'axis'),
      valueFormatter: (v: any) => (typeof v === 'number' ? v.toPrecision(4) : v),
    },
    xAxis: {
      ...axisStyle(p),
      name: opts.xLabel, nameLocation: 'middle', nameGap: 26,
      nameTextStyle: { color: p.text3, fontSize: 11 },
      axisLabel: { ...axisStyle(p).axisLabel, formatter: (v: number) => opts.xFormatter ? opts.xFormatter(v) : v },
    },
    yAxis: {
      ...axisStyle(p),
      name: opts.yLabel, nameLocation: 'middle', nameGap: 44,
      nameTextStyle: { color: p.text3, fontSize: 11 },
      scale: true,
      axisLabel: { ...axisStyle(p).axisLabel, formatter: (v: number) => opts.yFormatter ? opts.yFormatter(v) : v },
    },
    series: opts.series.map((s) => {
      const isScatter = s.kind === 'scatter'
      return {
        type: isScatter ? 'scatter' : 'line',
        name: s.name,
        data: s.data,
        color: s.color,
        symbolSize: isScatter ? 2.6 : 0,
        showSymbol: isScatter,
        lineStyle: isScatter ? undefined : { width: s.width ?? 2, type: s.dashed ? 'dashed' : 'solid' },
        itemStyle: isScatter ? { opacity: 0.55 } : undefined,
        smooth: false,
        z: isScatter ? 2 : 3,
        markLine: s.markLine,
      }
    }),
  }
}

export function spectrumOpt(p: Palette, opts: {
  series: { name: string; freqs: number[]; mag: number[]; color: string; area?: boolean }[]
  excitation?: number
  xLabel?: string
  yLabel?: string
}): AnyOpt {
  const minF = Math.min(...opts.series.flatMap((s) => s.freqs).filter((f) => f > 0), 1)
  return {
    animation: false,
    grid: grid(),
    legend: opts.series.length > 1
      ? { show: true, textStyle: { color: p.text2, fontSize: 11 }, top: 0, itemWidth: 14, itemHeight: 3, icon: 'roundRect' }
      : { show: false },
    tooltip: tooltip(p, 'axis'),
    xAxis: {
      ...axisStyle(p, true),
      min: minF,
      name: opts.xLabel || '频率 (Hz)', nameLocation: 'middle', nameGap: 26,
      nameTextStyle: { color: p.text3, fontSize: 11 },
      axisLabel: { ...axisStyle(p).axisLabel, formatter: (v: number) => fmtEng(v) + 'Hz' },
    },
    yAxis: {
      ...axisStyle(p),
      name: opts.yLabel || '幅值', nameLocation: 'middle', nameGap: 44,
      nameTextStyle: { color: p.text3, fontSize: 11 },
      axisLabel: { ...axisStyle(p).axisLabel, formatter: (v: number) => fmtEng(v) },
    },
    series: opts.series.map((s) => {
      const data = s.freqs.map((f, i) => [f, s.mag[i]]) as [number, number][]
      const ml = opts.excitation
        ? {
            markLine: {
              silent: true,
              symbol: 'none',
              lineStyle: { color: p.text3, type: 'dashed', width: 1 },
              label: { color: p.text3, fontSize: 10, formatter: 'f₀' },
              data: [{ xAxis: opts.excitation }],
            },
          }
        : {}
      return {
        type: 'line', name: s.name, data, color: s.color,
        showSymbol: false, smooth: true,
        lineStyle: { width: 1.8 },
        areaStyle: s.area ? { opacity: 0.12 } : undefined,
        ...ml,
      }
    }),
  }
}

export function bodeOpt(p: Palette, opts: {
  mode: 'mag' | 'phase'
  measured: { f: number; v: number }[]
  theory?: { f: number[]; v: number[] }
  yLabel: string
}): AnyOpt {
  const measData = opts.measured.map((m) => [m.f, m.v]) as [number, number][]
  const series: AnyOpt[] = [
    {
      type: 'scatter', name: '测量', data: measData,
      color: p.series[0], symbolSize: 7,
      itemStyle: { color: p.series[0], borderColor: 'transparent' },
      z: 4,
    },
  ]
  if (opts.theory) {
    series.push({
      type: 'line', name: '拟合模型', data: opts.theory.f.map((f, i) => [f, opts.theory!.v[i]]),
      color: p.series[1], showSymbol: false, smooth: true,
      lineStyle: { width: 2 }, z: 3,
    })
  }
  const minF = Math.min(...measData.map((d) => d[0]).filter((f) => f > 0), 1)
  return {
    animation: false,
    grid: grid(),
    legend: { show: true, textStyle: { color: p.text2, fontSize: 11 }, top: 0, itemWidth: 14, itemHeight: 3, icon: 'roundRect' },
    tooltip: tooltip(p, 'axis'),
    xAxis: {
      ...axisStyle(p, true), min: minF,
      name: '频率 (Hz)', nameLocation: 'middle', nameGap: 26,
      nameTextStyle: { color: p.text3, fontSize: 11 },
      axisLabel: { ...axisStyle(p).axisLabel, formatter: (v: number) => fmtEng(v) + 'Hz' },
    },
    yAxis: {
      ...axisStyle(p), name: opts.yLabel, nameLocation: 'middle', nameGap: 48,
      nameTextStyle: { color: p.text3, fontSize: 11 },
      scale: true,
      axisLabel: opts.mode === 'phase'
        ? { ...axisStyle(p).axisLabel, formatter: (v: number) => v.toFixed(0) + '°' }
        : { ...axisStyle(p).axisLabel, formatter: (v: number) => fmtEng(v) + 'Ω' },
    },
    series,
  }
}

export function nyquistOpt(p: Palette, opts: {
  measured: { re: number; im: number }[]
  theory?: { re: number[]; im: number[] }
}): AnyOpt {
  // plot -Im(Z) on y (electrochemistry convention) so capacitive (Im<0) is upper half
  const measData = opts.measured.map((m) => [m.re, -m.im]) as [number, number][]
  const series: AnyOpt[] = [
    {
      type: 'scatter', name: '测量', data: measData, color: p.series[0],
      symbolSize: 7, itemStyle: { color: p.series[0] }, z: 4,
    },
  ]
  if (opts.theory) {
    series.push({
      type: 'line', name: '拟合模型',
      data: opts.theory.re.map((r, i) => [r, -opts.theory!.im[i]]),
      color: p.series[1], showSymbol: false, smooth: true,
      lineStyle: { width: 2 }, z: 3,
    })
  }
  return {
    animation: false,
    grid: { left: 60, right: 18, top: 28, bottom: 38 },
    legend: { show: true, textStyle: { color: p.text2, fontSize: 11 }, top: 0, itemWidth: 14, itemHeight: 3, icon: 'roundRect' },
    tooltip: tooltip(p, 'axis'),
    xAxis: {
      ...axisStyle(p), name: 'Re(Z) (Ω)', nameLocation: 'middle', nameGap: 28,
      nameTextStyle: { color: p.text3, fontSize: 11 },
      axisLabel: { ...axisStyle(p).axisLabel, formatter: (v: number) => fmtEng(v) },
      scale: true,
    },
    yAxis: {
      ...axisStyle(p), name: '−Im(Z) (Ω)', nameLocation: 'middle', nameGap: 44,
      nameTextStyle: { color: p.text3, fontSize: 11 },
      axisLabel: { ...axisStyle(p).axisLabel, formatter: (v: number) => fmtEng(v) },
      scale: true,
    },
    series,
  }
}

export function complexPointOpt(p: Palette, z: { re: number; im: number }): AnyOpt {
  return {
    animation: false,
    grid: { left: 50, right: 18, top: 16, bottom: 30 },
    tooltip: { ...tooltip(p, 'item'), formatter: () => `Re=${z.re.toPrecision(4)} Ω<br/>Im=${z.im.toPrecision(4)} Ω` },
    xAxis: {
      ...axisStyle(p), name: 'Re (Ω)', nameLocation: 'middle', nameGap: 24,
      nameTextStyle: { color: p.text3, fontSize: 11 },
      axisLabel: { ...axisStyle(p).axisLabel, formatter: (v: number) => fmtEng(v) },
      scale: true,
    },
    yAxis: {
      ...axisStyle(p), name: 'Im (Ω)', nameLocation: 'middle', nameGap: 36,
      nameTextStyle: { color: p.text3, fontSize: 11 },
      axisLabel: { ...axisStyle(p).axisLabel, formatter: (v: number) => fmtEng(v) },
      scale: true,
    },
    series: [
      {
        type: 'scatter', data: [[z.re, z.im]], color: p.series[0],
        symbolSize: 12, itemStyle: { color: p.series[0] },
        markLine: {
          silent: true, symbol: 'none',
          lineStyle: { color: p.baseline, type: 'dashed', width: 1 },
          data: [{ yAxis: 0 }, { xAxis: 0 }],
        },
      },
    ],
  }
}

// ---- small local formatter (avoids importing format.ts into option hot path) ----
const ENG: [number, string][] = [
  [1e9, 'G'], [1e6, 'M'], [1e3, 'k'], [1, ''],
  [1e-3, 'm'], [1e-6, 'µ'], [1e-9, 'n'], [1e-12, 'p'],
]
function fmtEng(x: number): string {
  if (!Number.isFinite(x)) return '∞'
  if (x === 0) return '0'
  const sign = x < 0 ? '-' : ''
  const a = Math.abs(x)
  for (const [f, pfx] of ENG) {
    if (a >= f) return sign + (x / f).toPrecision(2).replace(/\.?0+$/, '') + (pfx ? pfx : '')
  }
  return x.toExponential(1)
}
