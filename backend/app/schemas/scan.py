"""Response schemas for scan results."""
from __future__ import annotations
from datetime import datetime
from pydantic import BaseModel


class MeasurementOut(BaseModel):
    id: int
    scan_id: str
    frequency: float
    dt: float
    n: int
    z_real: float
    z_imag: float
    z_mag: float
    z_phase_deg: float
    z_sigma: float = 0.0                 # 1-sigma on |Z| (Ω)
    z_phase_sigma_deg: float = 0.0       # 1-sigma on ∠Z (°)
    R: float
    X: float
    D: float | None
    Q: float | None
    esr: float
    L_eq: float | None
    C_eq: float | None
    v_amp: float
    v_phase_deg: float
    i_amp: float
    i_phase_deg: float
    resid_rms_v: float
    resid_rms_i: float
    v_dc: float
    i_dc: float


class MeasurementDetail(MeasurementOut):
    """Single measurement with raw waveforms + fitted curves + residuals + FFT."""
    time: list[float]                  # t[k] = k*dt
    voltage: list[float]
    current: list[float]
    fitted_voltage: list[float]
    fitted_current: list[float]
    resid_v: list[float]               # raw - fitted, per sample
    resid_i: list[float]
    fft_freqs: list[float]
    fft_mag_v: list[float]
    fft_mag_i: list[float]


class ScanOut(BaseModel):
    id: str
    device: str
    note: str | None
    freq_list: list[float]
    status: str
    created_at: datetime
    measurement_count: int


class ScanDetail(ScanOut):
    measurements: list[MeasurementOut]
