"""Goodness-of-fit metrics, formula-identical to Try1 (DESIGN.md section 6).

Try1 reference: rlc_id/fit_engine_a.py (Try1 DESIGN.md sections 5.1, 5.5,
8.1).  Try2 has no free parameters, so all candidates share the same model
dimension K = n_params + 1 and the AICc ordering degenerates to the RSS
ordering; the AICc value is still reported for continuity.
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
    """Corrected AIC (Try1 section 5.5); n_obs = 2M, K = p + 1."""
    k = p + 1
    rss = max(rss, 1e-300)
    denom = max(n_obs - k - 1, 1)
    return n_obs * np.log(rss / n_obs) + 2 * k + 2 * k * (k + 1) / denom


def fit_metrics(z: np.ndarray, zfit: np.ndarray) -> tuple[float, float]:
    """Relative RMSE and max relative error (Try1 section 8.1)."""
    rel = np.abs((z - zfit) / z)
    return float(np.sqrt(np.mean(rel**2))), float(np.max(rel))


def weighted_rss(z: np.ndarray, z_model: np.ndarray,
                 w: np.ndarray) -> float:
    """RSS of the weighted residual, vectorized over candidates.

    z: (M,), z_model: (N, M), w: (M,) -> (N,) float."""
    r = w[None, :] * np.abs(z[None, :] - z_model)
    return np.sum(r * r, axis=1)
