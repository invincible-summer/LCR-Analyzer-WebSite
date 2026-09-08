// uncertainty.ts — explicit opt-in bridge from backend scan polar
// uncertainty (z_sigma [Ω], z_phase_sigma_deg) to per-point Cartesian
// covariance for GLS fitting.
//
// Σ_RI = J · diag(σρ², σφ²) · Jᵀ,  J = [[cosφ, -ρ sinφ], [sinφ, ρ cosφ]]
//
// The backend sigmas are approximate error propagation, not a full
// waveform least-squares covariance; converted points are tagged
// source='scan_polar_approx' and must never be presented as instrument
// covariance. Any point violating ρ>0, σρ>0, σφ>0 or SPD invalidates the
// whole group — the caller then keeps the relative fallback.

import type { ZPoint } from './fitTypes'

export interface PolarUncertainty {
  /** 1-sigma on |Z| per point [Ω] */
  rho: number[]
  /** 1-sigma on arg Z per point [deg] */
  phi: number[]
}

/** true when every scan point carries usable polar uncertainty */
export function usablePolarUncertainty(u: PolarUncertainty): boolean {
  return (
    u.rho.length === u.phi.length &&
    u.rho.length > 0 &&
    u.rho.every(s => Number.isFinite(s) && s > 0) &&
    u.phi.every(s => Number.isFinite(s) && s > 0)
  )
}

/**
 * Convert all-or-nothing. Returns null when any point cannot be converted;
 * callers must then stay on the relative fallback and say so.
 */
export function polarToCartesianCov(points: ZPoint[], u: PolarUncertainty): ZPoint[] | null {
  if (points.length !== u.rho.length || points.length !== u.phi.length) return null
  const out: ZPoint[] = []
  for (let i = 0; i < points.length; i++) {
    const p = points[i]
    const rho = Math.hypot(p.re, p.im)
    const sr = u.rho[i]
    const sf = (u.phi[i] * Math.PI) / 180
    if (!(rho > 0) || !(sr > 0) || !(sf > 0)) return null
    const c = Math.cos(Math.atan2(p.im, p.re))
    const s = Math.sin(Math.atan2(p.im, p.re))
    const rr = c * c * sr * sr + rho * rho * s * s * sf * sf
    const ri = c * s * (sr * sr - rho * rho * sf * sf)
    const ii = s * s * sr * sr + rho * rho * c * c * sf * sf
    if (!(rr > 0) || !(ii > 0) || !(rr * ii - ri * ri > 0)) return null
    out.push({ ...p, cov: { rr, ri, ii, source: 'scan_polar_approx' } })
  }
  return out
}
