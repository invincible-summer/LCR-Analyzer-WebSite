// In-browser synthetic data generator — lets the platform be demoed without
// running the CLI simulator or having real ESP32 hardware. Mirrors the same
// physics: drive i(t)=I0·sin(wt); measure v(t)=I0·|Z|·sin(wt+∠Z) (+ noise/dc/harmonic).

import { http } from '../api/client'

export interface GenPreset {
  R: number
  L: number
  C: number
  fStart: number
  fStop: number
  fPoints: number
  i0?: number
  noise?: number
  harmonic?: number
  dc?: number
  device?: string
  note?: string
}

export const PRESETS: Record<string, GenPreset> = {
  rlc: { R: 50, L: 1e-3, C: 1e-6, fStart: 100, fStop: 1e5, fPoints: 30, noise: 0.005, note: '串联 RLC · R=50Ω L=1mH C=1µF' },
  rc: { R: 330, L: 0, C: 1e-6, fStart: 100, fStop: 1e6, fPoints: 30, noise: 0.005, note: '串联 RC · R=330Ω C=1µF' },
  rl: { R: 22, L: 1e-3, C: 1e15, fStart: 100, fStop: 1e5, fPoints: 30, noise: 0.005, note: '串联 RL · R=22Ω L=1mH' },
  rlc_harm: { R: 50, L: 1e-3, C: 1e-6, fStart: 100, fStop: 1e5, fPoints: 24, noise: 0.005, harmonic: 0.15, note: '串联 RLC + 15% 二次谐波（u 非纯正弦）' },
}

function logspace(a: number, b: number, n: number): number[] {
  if (n <= 1) return [a]
  const la = Math.log10(a)
  const lb = Math.log10(b)
  const out: number[] = []
  for (let k = 0; k < n; k++) out.push(10 ** (la + ((lb - la) * k) / (n - 1)))
  return out
}

function genPoint(o: GenPreset, f: number) {
  const omega = 2 * Math.PI * f
  const spc = 64
  const cycles = 16
  const N = spc * cycles
  const dt = 1 / (f * spc)
  const i0 = o.i0 ?? 0.01
  const zim = omega * o.L - 1 / (omega * o.C)
  const zre = o.R
  const mag = Math.hypot(zre, zim)
  const ph = Math.atan2(zim, zre)
  const v: number[] = []
  const i: number[] = []
  for (let k = 0; k < N; k++) {
    const t = k * dt
    const ic = i0 * Math.sin(omega * t)
    let vc = i0 * mag * Math.sin(omega * t + ph)
    if (o.harmonic) vc += o.harmonic * i0 * mag * Math.sin(2 * omega * t + ph)
    if (o.dc) vc += o.dc
    if (o.noise) vc += (Math.random() * 2 - 1) * o.noise * i0 * mag
    i.push(ic)
    v.push(vc)
  }
  return { v, i, dt, n: N - 1 }
}

export async function postSyntheticScan(o: GenPreset): Promise<string> {
  const freqs = logspace(o.fStart, o.fStop, o.fPoints)
  const device = o.device || 'ESP32_LCR_WEB'
  const { data } = await http.post('/scan/start', { device, freq_list: freqs, note: o.note })
  const scanId: string = data.id
  for (const f of freqs) {
    const { v, i, dt, n } = genPoint(o, f)
    await http.post(`/scan/${scanId}/point`, {
      device, frequency: f, dt, n, voltage: v, current: i,
    })
  }
  return scanId
}
