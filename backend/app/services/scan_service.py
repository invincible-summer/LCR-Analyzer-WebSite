"""Scan lifecycle: ingest frequency points, run DSP, persist, fit."""
from __future__ import annotations
import uuid
import numpy as np
from sqlalchemy.orm import Session
from sqlalchemy import func

from ..models.db import Scan, Measurement, RawWave, FitResult, CalibSet
from ..schemas.upload import PointUpload, ScanStart
from ..schemas.scan import MeasurementOut, MeasurementDetail, ScanOut, ScanDetail
from ..dsp.impedance import measure_impedance
from ..dsp.spectrum import fft_spectrum
from ..dsp.topology_fit import MODELS as TOPOLOGIES, fit_topology
from ..dsp.fit_auto import fit_auto
from ..dsp.calibration import apply_calibration


def start_scan(db: Session, payload: ScanStart) -> Scan:
    scan = Scan(
        id=uuid.uuid4().hex[:12],
        device=payload.device,
        note=payload.note,
        freq_list=list(payload.freq_list),
        status="open",
    )
    db.add(scan)
    db.commit()
    db.refresh(scan)
    return scan


def add_point(db: Session, scan_id: str, p: PointUpload) -> Measurement:
    imp = measure_impedance(p.voltage, p.current, p.dt, p.frequency)
    spec_v = fft_spectrum(p.voltage, p.dt)
    spec_i = fft_spectrum(p.current, p.dt)

    m = Measurement(
        scan_id=scan_id, frequency=p.frequency, dt=p.dt, n=p.n,
        z_real=imp.z_real, z_imag=imp.z_imag, z_mag=imp.z_mag,
        z_phase_deg=float(np.degrees(imp.z_phase)),
        z_sigma=imp.z_sigma,
        z_phase_sigma_deg=float(np.degrees(imp.z_phase_sigma)),
        R=imp.R, X=imp.X, D=imp.D, Q=imp.Q, esr=imp.esr,
        L_eq=imp.L_eq, C_eq=imp.C_eq,
        v_amp=imp.v_fit.amp, v_phase_deg=float(np.degrees(imp.v_fit.phase)),
        i_amp=imp.i_fit.amp, i_phase_deg=float(np.degrees(imp.i_fit.phase)),
        resid_rms_v=imp.v_fit.resid_rms, resid_rms_i=imp.i_fit.resid_rms,
        v_dc=float(imp.v_fit.dc), i_dc=float(imp.i_fit.dc),
    )
    m.raw = RawWave(
        voltage=[float(x) for x in p.voltage],
        current=[float(x) for x in p.current],
        fitted_voltage=[float(x) for x in imp.v_fit.fitted],
        fitted_current=[float(x) for x in imp.i_fit.fitted],
        resid_v=[float(x) for x in imp.v_fit.resid],
        resid_i=[float(x) for x in imp.i_fit.resid],
        fft_freqs=[float(x) for x in spec_v.freqs],
        fft_mag_v=[float(x) for x in spec_v.mag],
        fft_mag_i=[float(x) for x in spec_i.mag],
    )
    db.add(m)
    db.commit()
    db.refresh(m)
    return m


def measurement_to_out(m: Measurement) -> MeasurementOut:
    return MeasurementOut(
        id=m.id, scan_id=m.scan_id, frequency=m.frequency, dt=m.dt, n=m.n,
        z_real=m.z_real, z_imag=m.z_imag, z_mag=m.z_mag, z_phase_deg=m.z_phase_deg,
        z_sigma=m.z_sigma or 0.0, z_phase_sigma_deg=m.z_phase_sigma_deg or 0.0,
        R=m.R, X=m.X, D=m.D, Q=m.Q, esr=m.esr, L_eq=m.L_eq, C_eq=m.C_eq,
        v_amp=m.v_amp, v_phase_deg=m.v_phase_deg,
        i_amp=m.i_amp, i_phase_deg=m.i_phase_deg,
        resid_rms_v=m.resid_rms_v, resid_rms_i=m.resid_rms_i,
        v_dc=m.v_dc, i_dc=m.i_dc,
    )


def measurement_to_detail(m: Measurement) -> MeasurementDetail:
    t = [m.dt * k for k in range(m.n + 1)]
    raw = m.raw
    return MeasurementDetail(
        **measurement_to_out(m).model_dump(),
        time=t,
        voltage=raw.voltage, current=raw.current,
        fitted_voltage=raw.fitted_voltage, fitted_current=raw.fitted_current,
        resid_v=raw.resid_v, resid_i=raw.resid_i,
        fft_freqs=raw.fft_freqs, fft_mag_v=raw.fft_mag_v, fft_mag_i=raw.fft_mag_i,
    )


def list_scans(db: Session) -> list[ScanOut]:
    rows = db.query(Scan).order_by(Scan.created_at.desc()).all()
    out: list[ScanOut] = []
    for s in rows:
        cnt = db.query(func.count(Measurement.id)).filter(
            Measurement.scan_id == s.id).scalar() or 0
        out.append(ScanOut(
            id=s.id, device=s.device, note=s.note, freq_list=s.freq_list,
            status=s.status, created_at=s.created_at, measurement_count=cnt,
        ))
    return out


def get_scan_detail(db: Session, scan_id: str) -> ScanDetail | None:
    s = db.get(Scan, scan_id)
    if not s:
        return None
    ms = db.query(Measurement).filter(Measurement.scan_id == scan_id)\
        .order_by(Measurement.frequency).all()
    return ScanDetail(
        id=s.id, device=s.device, note=s.note, freq_list=s.freq_list,
        status=s.status, created_at=s.created_at, measurement_count=len(ms),
        measurements=[measurement_to_out(m) for m in ms],
    )


def get_measurement_detail(db: Session, scan_id: str, measurement_id: int) -> MeasurementDetail | None:
    m = db.get(Measurement, measurement_id)
    if not m or m.scan_id != scan_id or not m.raw:
        return None
    return measurement_to_detail(m)


def _cx_list(zs) -> list[list[float]]:
    return [[float(z.real), float(z.imag)] for z in zs]


def fit_scan(db: Session, scan_id: str, model: str = "auto",
             calib_id: int | None = None) -> FitResult:
    """Fit a scan. ``model`` is "auto" (VF + topology ranking) or a named
    topology key from ``dsp.topology_fit.MODELS``."""
    ms = db.query(Measurement).filter(Measurement.scan_id == scan_id)\
        .order_by(Measurement.frequency).all()
    if not ms:
        raise ValueError("scan has no measurements")

    freqs = np.array([m.frequency for m in ms], dtype=float)
    Z = np.array([complex(m.z_real, m.z_imag) for m in ms], dtype=complex)
    sigma = np.array([m.z_sigma or 0.0 for m in ms], dtype=float)
    sigma = sigma if np.any(sigma > 0) else None

    if calib_id:
        cal = db.get(CalibSet, calib_id)
        if cal:
            Z = apply_calibration(freqs, Z, {
                "short": cal.short, "open": cal.open_,
                "load": cal.load, "load_true": cal.load_true,
            })

    if model == "auto":
        auto = fit_auto(freqs, Z, sigma=sigma)
        best = auto.best
        if best.kind == "vf":
            rfit, syn = best.vf, best.synthesis
            fr = FitResult(
                scan_id=scan_id, model="auto", kind="vf",
                params={"d": rfit.d, "e": rfit.e},
                rmse=best.rmse, chi2_red=best.chi2_red, aicc=best.aicc,
                converged=rfit.converged, passive=syn.passive,
                theory=auto.theory,
                residuals={"frequency": freqs.tolist(),
                           "re": (rfit.z_fit - Z).real.tolist(),
                           "im": (rfit.z_fit - Z).imag.tolist()},
                netlist=syn.netlist,
                poles=_cx_list(rfit.poles), zeros=_cx_list(rfit.zeros()),
                warnings=syn.warnings or None,
                ranking=auto.to_summary(),
            )
        else:
            topo = best.topo
            fr = FitResult(
                scan_id=scan_id, model="auto", kind="topology",
                params=topo.params, param_ci=topo.param_ci,
                rmse=topo.rmse, chi2_red=topo.chi2_red, aicc=topo.aicc,
                converged=topo.converged, passive=None,
                theory=topo.theory, residuals=topo.residuals,
                ranking=auto.to_summary(),
            )
    elif model in TOPOLOGIES:
        topo = fit_topology(model, freqs, Z, sigma=sigma)
        fr = FitResult(
            scan_id=scan_id, model=model, kind="topology",
            params=topo.params, param_ci=topo.param_ci,
            rmse=topo.rmse, chi2_red=topo.chi2_red, aicc=topo.aicc,
            converged=topo.converged, passive=None,
            theory=topo.theory, residuals=topo.residuals,
        )
    else:
        raise ValueError(f"unknown model {model!r}; choose 'auto' or {list(TOPOLOGIES)}")

    db.add(fr)
    db.commit()
    db.refresh(fr)
    return fr


def delete_scan(db: Session, scan_id: str) -> bool:
    s = db.get(Scan, scan_id)
    if not s:
        return False
    db.delete(s)
    db.commit()
    return True
