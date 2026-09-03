"""Goodness-of-fit metrics, formula-identical to Try1 (DESIGN.md sec.6).

Try1 reference: rlc_id/fit_engine_a.py, Try1 DESIGN.md sec.5.1/5.5/8.1
(Try2 metric.py carries the same formulas).  Everything is relative-error
based: w_k = 1/|z_k|, residual r_k = w_k (z_k - Z_k), interleaved real and
imaginary parts; wRMSE and max relative error exactly as Try1 sec.8.1;
AICc with n_obs = 2M and K = p + 1 for cross-graph ranking.
"""

from __future__ import annotations

import numpy as np


def default_weights(z: np.ndarray) -> np.ndarray:
    """Default relative-error weights w_k = 1/|z_hat_k| (Try1 model A3)."""
    return 1.0 / np.abs(z)


def residual_vector(z: np.ndarray, z_model: np.ndarray,
                    w: np.ndarray) -> np.ndarray:
    """Interleaved [Re r_1, Im r_1, Re r_2, ...] weighted residual."""
    r = w * (z - z_model)
    out = np.empty(2 * len(z))
    out[0::2] = r.real
    out[1::2] = r.imag
    return out


def rss_of(residual: np.ndarray) -> float:
    return float(residual @ residual)


def aicc(rss: float, n_obs: int, p: int) -> float:
    """Corrected AIC (Try1 sec.5.5); n_obs = 2M, K = p + 1."""
    k = p + 1
    rss = max(rss, 1e-300)
    denom = max(n_obs - k - 1, 1)
    return n_obs * np.log(rss / n_obs) + 2 * k + 2 * k * (k + 1) / denom


def fit_metrics(z: np.ndarray, zfit: np.ndarray):
    """Relative RMSE and max relative error (Try1 sec.8.1)."""
    rel = np.abs((z - zfit) / z)
    return float(np.sqrt(np.mean(rel**2))), float(np.max(rel))


def curve_max_rel(z_true: np.ndarray, z_fit: np.ndarray) -> float:
    """Dense-grid max relative curve error (Try1 equivalence check)."""
    return float(np.max(np.abs((z_true - z_fit) / z_true)))


def curve_max_rel_floored(z_true: np.ndarray, z_fit: np.ndarray,
                          floor_frac: float = 0.1) -> float:
    """Max relative curve error with the denominator floored at
    floor_frac * median|z_true|.

    Plain relative error explodes at deep resonance notches where |Z| is
    tiny: there the *absolute* error is what the data can constrain, so the
    denominator is floored (dynamic-range-aware Bode comparison)."""
    z_true = np.asarray(z_true)
    z_fit = np.asarray(z_fit)
    den = np.maximum(np.abs(z_true), floor_frac * np.median(np.abs(z_true)))
    return float(np.max(np.abs(z_true - z_fit) / den))


def matched_group_errors(fit_vals: list, true_vals: list, reduced_edges) -> list:
    """Per-parameter relative errors between fitted and true group values,
    resolving interchangeable-group permutations.

    Two reduced groups occupying the same node pair with the same kind
    (e.g. two parallel L edges, which cannot be merged) are electrically
    interchangeable: the fit may return their values in either order.
    Sorting within each (u, v, kind) class aligns them before comparing."""
    classes = {}
    for g, fv, tv in zip(reduced_edges, fit_vals, true_vals):
        key = (min(g.u, g.v), max(g.u, g.v), g.kind)
        classes.setdefault(key, []).append((fv, tv))
    errors = []
    labels = []
    for key, lst in classes.items():
        fits = sorted(v[0] for v in lst)
        trues = sorted(v[1] for v in lst)
        for fv, tv in zip(fits, trues):
            for i in range(1, len(tv)):
                errors.append(abs(fv[i] - tv[i]) / tv[i])
                nm = "Rd" if (key[2] == "L" and i == 2) else "v"
                labels.append((key, nm))
    return errors, labels
