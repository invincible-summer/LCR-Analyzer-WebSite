import { http } from './client'

export interface ScanSummary {
  id: string
  device: string
  note: string | null
  freq_list: number[]
  status: string
  created_at: string
  measurement_count: number
}

export interface Measurement {
  id: number
  scan_id: string
  frequency: number
  dt: number
  n: number
  z_real: number
  z_imag: number
  z_mag: number
  z_phase_deg: number
  z_sigma: number
  z_phase_sigma_deg: number
  R: number
  X: number
  D: number | null
  Q: number | null
  esr: number
  L_eq: number | null
  C_eq: number | null
  v_amp: number
  v_phase_deg: number
  i_amp: number
  i_phase_deg: number
  resid_rms_v: number
  resid_rms_i: number
  v_dc: number
  i_dc: number
}

export interface MeasurementDetail extends Measurement {
  time: number[]
  voltage: number[]
  current: number[]
  fitted_voltage: number[]
  fitted_current: number[]
  resid_v: number[]
  resid_i: number[]
  fft_freqs: number[]
  fft_mag_v: number[]
  fft_mag_i: number[]
}

export interface ScanDetail extends ScanSummary {
  measurements: Measurement[]
}

// Recursive series-parallel RLC netlist tree (rendered by lib/schematic.ts).
// `dcr` on an L leaf is the inductor's series DC resistance — ONE device with
// TWO parameters (the Try engines' real-inductor model).
export type Netlist =
  | { type: 'R'; R: number }
  | { type: 'L'; L: number; dcr?: number }
  | { type: 'C'; C: number }
  | { type: 'series'; children: Netlist[] }
  | { type: 'parallel'; children: Netlist[] }

export const listScans = () => http.get<ScanSummary[]>('/scans').then((r) => r.data)
export const getScan = (id: string) => http.get<ScanDetail>(`/scan/${id}`).then((r) => r.data)
export const getMeasurement = (scanId: string, measurementId: number) =>
  http.get<MeasurementDetail>(`/scan/${scanId}/measurement/${measurementId}`).then((r) => r.data)
export const deleteScan = (id: string) => http.delete(`/scan/${id}`).then((r) => r.data)
export const exportScanUrl = (id: string, format: 'csv' | 'json') =>
  `/api/scan/${id}/export?format=${format}`

export const wsLiveUrl = (): string => {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws'
  return `${proto}://${location.host}/ws/live`
}
