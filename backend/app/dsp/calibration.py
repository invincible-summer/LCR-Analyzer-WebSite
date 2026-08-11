"""OSL (open / short / load) calibration helpers.

STATUS: functional scaffolding. The exact correction depends on the analog
front-end topology of the real hardware, which is not finalised yet. The
functions below implement a defensible, commonly-used sequence:

  1. subtract the short standard  -> removes series lead impedance
  2. subtract the open standard   -> removes parallel (shunt) admittance
  3. scale by the load standard    -> removes gain error and residual phase

A real instrument's error model may reorder or reweight these; revisit once
the V/I front-end and calibration fixture are fixed. The API layer only calls
into here when a calibration set is explicitly selected.
"""
from __future__ import annotations
from typing import Iterable
import numpy as np


def _interp_standard(standard: dict, freqs: np.ndarray) -> np.ndarray:
    """A standard is stored as {freq: [re, im]}; linearly interpolate in freq."""
    if not standard:
        return np.zeros_like(freqs, dtype=complex)
    sf = np.array([float(k) for k in standard.keys()], dtype=float)
    sv = np.array([[complex(*v)] if np.ndim(v) == 1 else [complex(v)]
                   for v in standard.values()], dtype=complex).ravel()
    order = np.argsort(sf)
    sf, sv = sf[order], sv[order]
    re = np.interp(freqs, sf, sv.real)
    im = np.interp(freqs, sf, sv.imag)
    return re + 1j * im


def apply_calibration(freqs: Iterable[float], Z_meas: Iterable[complex],
                      cal: dict | None) -> np.ndarray:
    """Apply an OSL calibration set to measured impedances.

    ``cal`` keys: ``short``, ``open``, ``load`` (each {freq: [re, im]}), and
    optionally ``load_true`` (the known complex value of the load standard).
    If ``cal`` is None or empty, returns Z_meas unchanged.
    """
    Z = np.asarray(Z_meas, dtype=complex)
    f = np.asarray(freqs, dtype=float)
    if not cal:
        return Z

    z_short = _interp_standard(cal.get("short", {}), f)
    z_open = _interp_standard(cal.get("open", {}), f)
    z_load = _interp_standard(cal.get("load", {}), f)
    load_true = complex(cal.get("load_true", 0.0))

    # 1. remove series lead impedance
    Z1 = Z - z_short

    # 2. remove shunt admittance of the open fixture
    with np.errstate(divide="ignore", invalid="ignore"):
        Y1 = np.where(Z1 != 0, 1.0 / Z1, 0.0)
        Yo = np.where(z_open != 0, 1.0 / z_open, 0.0)
        Z2 = np.where((Y1 - Yo) != 0, 1.0 / (Y1 - Yo), Z1)

    # 3. gain/phase correction from the load standard
    if load_true != 0 and np.any(z_load):
        # factor k makes corrected load == load_true
        with np.errstate(divide="ignore", invalid="ignore"):
            k = np.where(z_load != 0, load_true / z_load, 1.0)
        # use the median factor across freq as a single global correction
        k_global = np.median(k[np.isfinite(k) & (np.abs(k) > 0)]) if np.any(np.isfinite(k)) else 1.0
        Z2 = Z2 * k_global
    return Z2
