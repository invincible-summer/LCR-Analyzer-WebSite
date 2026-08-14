"""Fixed-topology equivalent-circuit fitting over a frequency sweep.

Given measured complex Z(f), fit one of a library of named RLC topologies in
**log10 parameter space** (R/L/C span many orders of magnitude). This is the
interpretable counterpart to the automatic vector-fitting engine
(``rational_fit.py``): every model here is a hand-written physical circuit.

Improvements over the previous single-start implementation:

* **Latin-hypercube multi-start** — log-space LHS samples plus a physical
  heuristic guess; the best converged solution wins. Single starts routinely
  landed in wrong local minima for parallel topologies.
* **sigma-weighted residual** — each point weighted by its measured noise
  (from the time-domain sine fits) instead of |Z| normalisation, so chi2 is
  statistically meaningful and comparable across models (and against the
  vector-fitting candidates).
* **robust loss** (soft_l1 at the median sigma) so a single glitchy
  frequency point cannot drag the whole fit.
* **parameter covariance** from the Jacobian -> 95% confidence intervals,
  reported per parameter.
* convergence status is reported, not silently dropped.

Weighting convention matches ``rational_fit.vector_fit``: sigma defaults to a
relative |Z| * 1e-3 when per-point noise is unavailable, so AICc values are
comparable between the two engines.
"""
from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Callable

import numpy as np
from scipy.optimize import least_squares


# --- model transfer functions: Z(params, omega) -> complex array ------------

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


def _z_r_plus_lc_parallel(p, w):
    """R + L∥C : inductor with parallel self-capacitance (SRF model)."""
    R, L, C = p
    Y = 1.0 / (w * L * 1j) + w * C * 1j
    return R + 1.0 / Y


def _z_rs_rp_c(p, w):
    """Rs + Rp∥C : electrolytic capacitor / electrochemical double layer."""
    Rs, Rp, C = p
    return Rs + 1.0 / (1.0 / Rp + 1j * w * C)


@dataclass
class ModelDef:
    name: str
    params: list[str]
    func: Callable
    label: str
    tex: str          # LaTeX impedance expression for the frontend


MODELS: dict[str, ModelDef] = {
    "series_RLC": ModelDef("series_RLC", ["R", "L", "C"], _z_series_rlc, "串联 RLC",
                           r"Z = R + j\omega L + \tfrac{1}{j\omega C}"),
    "series_RC": ModelDef("series_RC", ["R", "C"], _z_series_rc, "串联 RC",
                          r"Z = R + \tfrac{1}{j\omega C}"),
    "series_RL": ModelDef("series_RL", ["R", "L"], _z_series_rl, "串联 RL",
                          r"Z = R + j\omega L"),
    "parallel_RLC": ModelDef("parallel_RLC", ["R", "L", "C"], _z_parallel_rlc, "并联 RLC",
                             r"Z = \bigl(\tfrac{1}{R} + \tfrac{1}{j\omega L} + j\omega C\bigr)^{-1}"),
    "parallel_RC": ModelDef("parallel_RC", ["R", "C"], _z_parallel_rc, "并联 RC",
                            r"Z = \bigl(\tfrac{1}{R} + j\omega C\bigr)^{-1}"),
    "parallel_RL": ModelDef("parallel_RL", ["R", "L"], _z_parallel_rl, "并联 RL",
                            r"Z = \bigl(\tfrac{1}{R} + \tfrac{1}{j\omega L}\bigr)^{-1}"),
    "R_LC_parallel": ModelDef("R_LC_parallel", ["R", "L", "C"], _z_r_plus_lc_parallel,
                              "R + L∥C（电感 SRF）",
                              r"Z = R + \bigl(\tfrac{1}{j\omega L} + j\omega C\bigr)^{-1}"),
    "Rs_Rp_C": ModelDef("Rs_Rp_C", ["Rs", "Rp", "C"], _z_rs_rp_c,
                        "Rs + Rp∥C（电解电容）",
                        r"Z = R_s + \bigl(\tfrac{1}{R_p} + j\omega C\bigr)^{-1}"),
}

# physical bounds for log-space optimisation
_PHYS_BOUNDS = {"R": (1e-3, 1e9), "Rs": (1e-3, 1e9), "Rp": (1e-3, 1e12),
                "L": (1e-9, 1e3), "C": (1e-15, 1.0)}
_PARAM_UNITS = {"R": "Ω", "Rs": "Ω", "Rp": "Ω", "L": "H", "C": "F"}


def param_unit(name: str) -> str:
    return _PARAM_UNITS.get(name, "")


@dataclass
class TopologyFitResult:
    model: str
    params: dict[str, float]
    param_ci: dict[str, tuple[float, float]]   # 95% CI, linear space
    rmse: float                                 # unweighted complex RMS (Ω)
    chi2_red: float
    aicc: float
    rss: float
    converged: bool
    theory: dict                                # dense log-grid curve
    residuals: dict                             # per-point complex residual (Ω)


def _theory_grid(md: ModelDef, p_opt, f_lo: float, f_hi: float, n: int = 200) -> dict:
    f_grid = np.logspace(np.log10(max(f_lo, 1e-3)), np.log10(max(f_hi, f_lo * 10)), n)
    Zt = md.func(p_opt, 2.0 * np.pi * f_grid)
    return {
        "frequency": f_grid.tolist(),
        "z_mag": np.abs(Zt).tolist(),
        "z_phase_deg": np.degrees(np.angle(Zt)).tolist(),
        "z_real": Zt.real.tolist(),
        "z_imag": Zt.imag.tolist(),
    }


def _heuristic_guess(md: ModelDef, w: np.ndarray, Z: np.ndarray) -> np.ndarray:
    """Physical first guess. L is taken from the *low-frequency* inductive
    end (X ~ wL) and C from the *high-frequency* capacitive end (X ~ -1/wC):
    using the extreme-reactance points breaks down for self-resonant parts,
    where X peaks hugely at the resonance instead of growing linearly."""
    R0 = float(np.clip(np.median(Z.real), 1e-3, 1e8))
    X = Z.imag
    table = {"R": R0, "Rs": float(np.clip(np.min(Z.real), 1e-3, 1e8)),
             "Rp": float(np.clip(np.max(Z.real) * 10, 1e3, 1e11))}
    pos = np.where(X > 0)[0]
    if pos.size:
        i = pos[0]                                   # lowest inductive frequency
        val = X[i] / w[i]
        table["L"] = val if (np.isfinite(val) and val > 0) else 1e-3
    else:
        table["L"] = 1e-3
    neg = np.where(X < 0)[0]
    if neg.size:
        i = neg[-1]                                  # highest capacitive frequency
        val = -1.0 / (w[i] * X[i])
        table["C"] = val if (np.isfinite(val) and val > 0) else 1e-6
    else:
        table["C"] = 1e-6
    return np.array([table[k] for k in md.params], dtype=float)


def _lhs_log(bounds_lo, bounds_hi, n: int, rng: np.random.Generator) -> np.ndarray:
    """Latin hypercube samples in log10 space, shape (n, len(bounds))."""
    m = len(bounds_lo)
    u = (np.arange(n)[:, None] + rng.random((n, m))) / n
    return bounds_lo + u * (bounds_hi - bounds_lo)


def fit_topology(model_name: str, freqs, Z, sigma=None,
                 n_starts: int = 24, seed: int = 7,
                 plot_points: int = 200) -> TopologyFitResult:
    """Fit ``model_name`` to measured complex impedance ``Z`` over ``freqs``."""
    if model_name not in MODELS:
        raise ValueError(f"unknown model {model_name!r}; choose from {list(MODELS)}")
    freqs = np.asarray(freqs, dtype=float)
    Z = np.asarray(Z, dtype=complex)
    if freqs.size != Z.size:
        raise ValueError("freqs and Z must have the same length")

    md = MODELS[model_name]
    w = 2.0 * np.pi * freqs
    if sigma is None:
        sigma = np.abs(Z) * 1e-3
    else:
        sigma = np.asarray(sigma, dtype=float)
        # same noise floor as rational_fit: ~100 ppm relative
        sigma = np.maximum(sigma, np.abs(Z) * 1e-5)
    sigma = np.maximum(sigma, 1e-30)

    lo = np.array([math.log10(_PHYS_BOUNDS[k][0]) for k in md.params])
    hi = np.array([math.log10(_PHYS_BOUNDS[k][1]) for k in md.params])
    rng = np.random.default_rng(seed)
    starts = [_heuristic_guess(md, w, Z)]
    for logp in _lhs_log(lo, hi, n_starts, rng):
        starts.append(10.0 ** np.clip(logp, lo + 1e-9, hi - 1e-9))

    def residual(logp):
        Zm = md.func(10.0 ** logp, w)
        return np.concatenate([
            (Zm.real - Z.real) / sigma,
            (Zm.imag - Z.imag) / sigma,
        ])

    best = None
    for p0 in starts:
        log0 = np.clip(np.log10(np.maximum(p0, 1e-300)), lo + 1e-9, hi - 1e-9)
        sol = least_squares(residual, log0, bounds=(lo, hi), method="trf",
                            loss="soft_l1", f_scale=float(np.median(sigma)),
                            max_nfev=2000, x_scale="jac")
        if best is None or sol.cost < best.cost:
            best = sol

    p_opt = 10.0 ** best.x
    params = {k: float(v) for k, v in zip(md.params, p_opt)}

    Zm = md.func(p_opt, w)
    r = residual(best.x)
    rss = float(np.sum(r ** 2))
    n = 2 * Z.size
    k = len(md.params)
    chi2_red = rss / max(n - k, 1)
    aicc = n * math.log(max(rss, 1e-300) / n) + 2 * k + 2 * k * (k + 1) / max(n - k - 1, 1)
    rmse = float(np.sqrt(np.mean(np.abs(Z - Zm) ** 2)))

    # covariance in log space -> 95% CI back in linear space
    param_ci: dict[str, tuple[float, float]] = {}
    try:
        JTJ = best.jac.T @ best.jac
        cov = np.linalg.inv(JTJ) * chi2_red
        for i, name in enumerate(md.params):
            s = math.sqrt(max(cov[i, i], 0.0))
            # clamp to the physical bounds to avoid overflow on flat directions
            lo_i, hi_i = lo[i], hi[i]
            x_lo = 10.0 ** float(np.clip(best.x[i] - 1.96 * s, lo_i, hi_i))
            x_hi = 10.0 ** float(np.clip(best.x[i] + 1.96 * s, lo_i, hi_i))
            param_ci[name] = (float(x_lo), float(x_hi))
    except np.linalg.LinAlgError:
        pass

    return TopologyFitResult(
        model=model_name,
        params=params,
        param_ci=param_ci,
        rmse=rmse,
        chi2_red=float(chi2_red),
        aicc=float(aicc),
        rss=rss,
        converged=bool(best.success),
        theory=_theory_grid(md, p_opt, float(freqs.min()), float(freqs.max()),
                            plot_points),
        residuals={
            "frequency": freqs.tolist(),
            "w_re": (Zm.real - Z.real).tolist(),
            "w_im": (Zm.imag - Z.imag).tolist(),
        },
    )
