"""Equivalent-circuit model fitting over a frequency sweep.

Given measured complex Z(f) across a sweep, fit one of several RLC/RC/RL
topologies. We optimise over log10 of the parameters (R/L/C span many orders
of magnitude) with a joint real+imaginary residual, weighted by |Z|.

This is the *frequency-domain* fit (yields R/L/C and the smooth theory curve
for Bode/Nyquist). It is distinct from the *time-domain* sine fit in
``sine_fit.py`` (which yields Z at a single frequency and the u-t overlay).
"""
from __future__ import annotations
import math
from dataclasses import dataclass, field
from typing import Callable
import numpy as np
from scipy.optimize import least_squares


# --- model transfer functions: Z(params, omega) -> complex array -------------

def _z_series_rlc(p, w):
    R, L, C = p
    return R + 1j * (w * L - 1.0 / (w * C))


def _z_series_rc(p, w):
    R, C = p
    return R - 1j / (w * C)


def _z_series_rl(p, w):
    R, L = p
    return R + 1j * w * L


def _z_parallel_rlc(p, w):
    R, L, C = p
    Y = 1.0 / R + 1j * (w * C - 1.0 / (w * L))
    return 1.0 / Y


def _z_parallel_rc(p, w):
    R, C = p
    Y = 1.0 / R + 1j * w * C
    return 1.0 / Y


def _z_parallel_rl(p, w):
    R, L = p
    Y = 1.0 / R - 1j / (w * L)        # 1/(jwL) = -j/(wL)
    return 1.0 / Y


@dataclass
class ModelDef:
    name: str
    params: list[str]
    func: Callable
    label: str


MODELS: dict[str, ModelDef] = {
    "series_RLC":   ModelDef("series_RLC",   ["R", "L", "C"], _z_series_rlc,   "串联 RLC"),
    "series_RC":    ModelDef("series_RC",    ["R", "C"],      _z_series_rc,    "串联 RC"),
    "series_RL":    ModelDef("series_RL",    ["R", "L"],      _z_series_rl,    "串联 RL"),
    "parallel_RLC": ModelDef("parallel_RLC", ["R", "L", "C"], _z_parallel_rlc, "并联 RLC"),
    "parallel_RC":  ModelDef("parallel_RC",  ["R", "C"],      _z_parallel_rc,  "并联 RC"),
    "parallel_RL":  ModelDef("parallel_RL",  ["R", "L"],      _z_parallel_rl,  "并联 RL"),
}

# physical bounds for log-space optimisation
_PHYS_BOUNDS = {"R": (1e-3, 1e9), "L": (1e-9, 1e3), "C": (1e-15, 1.0)}
_PARAM_UNITS = {"R": "Ω", "L": "H", "C": "F"}


def param_unit(name: str) -> str:
    return _PARAM_UNITS.get(name, "")


@dataclass
class FitResult:
    model: str
    params: dict[str, float]
    rmse: float
    accuracy: float            # 1 - rmse/mean(|Z|), clamped >= 0
    cost: float
    theory: dict               # dense log-grid curve for plotting


def _initial_guess(name: str, w: np.ndarray, Z: np.ndarray) -> np.ndarray:
    md = MODELS[name]
    R0 = float(np.clip(np.median(Z.real), 1e-3, 1e8))
    X = Z.imag
    C0 = L0 = None
    neg = np.where(X < 0)[0]
    if neg.size:
        i = neg[int(np.argmin(X[neg]))]            # most negative reactance
        val = -1.0 / (w[i] * X[i])                  # X = -1/(wC)
        if np.isfinite(val) and val > 0:
            C0 = val
    pos = np.where(X > 0)[0]
    if pos.size:
        i = pos[int(np.argmax(X[pos]))]             # most positive reactance
        val = X[i] / w[i]                           # X = wL
        if np.isfinite(val) and val > 0:
            L0 = val
    table = {"R": R0, "L": L0 if (L0 and L0 > 0) else 1e-3, "C": C0 if (C0 and C0 > 0) else 1e-6}
    return np.array([table[k] for k in md.params], dtype=float)


def fit_circuit(model_name: str, freqs, Z, plot_points: int = 200) -> FitResult:
    """Fit ``model_name`` to measured complex impedance ``Z`` over ``freqs``."""
    if model_name not in MODELS:
        raise ValueError(f"unknown model {model_name!r}; choose from {list(MODELS)}")
    freqs = np.asarray(freqs, dtype=float)
    Z = np.asarray(Z, dtype=complex)
    if freqs.size != Z.size:
        raise ValueError("freqs and Z must have the same length")

    md = MODELS[model_name]
    w = 2.0 * np.pi * freqs

    p0 = _initial_guess(model_name, w, Z)
    lo = np.array([math.log10(_PHYS_BOUNDS[k][0]) for k in md.params])
    hi = np.array([math.log10(_PHYS_BOUNDS[k][1]) for k in md.params])
    log0 = np.clip(np.log10(p0), lo + 1e-9, hi - 1e-9)
    z_scale = np.maximum(np.abs(Z), 1e-12)

    def residual(logp):
        Zm = md.func(10.0 ** logp, w)
        return np.concatenate([
            (Zm.real - Z.real) / z_scale,
            (Zm.imag - Z.imag) / z_scale,
        ])

    sol = least_squares(residual, log0, bounds=(lo, hi), method="trf", max_nfev=4000)
    p_opt = 10.0 ** sol.x
    params = {k: float(v) for k, v in zip(md.params, p_opt)}

    Zm = md.func(p_opt, w)
    rmse = float(np.sqrt(np.mean(np.abs(Z - Zm) ** 2)))
    meanz = float(np.mean(np.abs(Z))) or 1e-12
    accuracy = max(0.0, 1.0 - rmse / meanz)

    f_lo = max(float(freqs.min()), 1e-3)
    f_hi = max(float(freqs.max()), f_lo * 10)
    f_grid = np.logspace(np.log10(f_lo), np.log10(f_hi), plot_points)
    Zt = md.func(p_opt, 2.0 * np.pi * f_grid)
    theory = {
        "frequency": f_grid.tolist(),
        "z_mag": np.abs(Zt).tolist(),
        "z_phase_deg": np.degrees(np.angle(Zt)).tolist(),
        "z_real": Zt.real.tolist(),
        "z_imag": Zt.imag.tolist(),
    }
    return FitResult(model=model_name, params=params, rmse=rmse,
                     accuracy=accuracy, cost=float(sol.cost), theory=theory)
