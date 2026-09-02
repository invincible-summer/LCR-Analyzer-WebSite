"""Metric formulas must reproduce Try1 numbers exactly."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import pytest

from netgraph_id.metric import (aicc, default_weights, fit_metrics,
                                residual_vector, rss_of, weighted_rss)

_TRY1_DIR = Path(__file__).resolve().parents[2] / "Try1-Completely unknown single port fitting"
try:
    sys.path.insert(0, str(_TRY1_DIR))
    from rlc_id.fit_engine_a import aicc as try1_aicc       # noqa: E402
    from rlc_id.fit_engine_a import fit_metrics as try1_fit  # noqa: E402
    HAS_TRY1 = True
except Exception:  # pragma: no cover
    HAS_TRY1 = False


def test_residual_interleaved_layout():
    z = np.array([1 + 2j, 3 - 1j])
    zm = np.array([0.9 + 2.1j, 3.2 - 1.2j])
    w = 1.0 / np.abs(z)
    r = residual_vector(z, zm, w)
    r0 = w[0] * (z[0] - zm[0])
    r1 = w[1] * (z[1] - zm[1])
    assert r[0] == pytest.approx(r0.real)
    assert r[1] == pytest.approx(r0.imag)
    assert r[2] == pytest.approx(r1.real)
    assert r[3] == pytest.approx(r1.imag)


def test_weighted_rss_matches_loop():
    rng = np.random.default_rng(3)
    z = rng.standard_normal(20) + 1j * rng.standard_normal(20)
    zm = rng.standard_normal((6, 20)) + 1j * rng.standard_normal((6, 20))
    w = default_weights(z)
    fast = weighted_rss(z, zm, w)
    for row in range(6):
        slow = rss_of(residual_vector(z, zm[row], w))
        assert fast[row] == pytest.approx(slow)


def test_fit_metrics_definition():
    z = np.array([1 + 1j, 2 - 1j])
    zf = np.array([1.1 + 0.9j, 2.0 - 1.1j])
    wrmse, mre = fit_metrics(z, zf)
    rel = np.abs((z - zf) / z)
    assert wrmse == pytest.approx(float(np.sqrt(np.mean(rel**2))))
    assert mre == pytest.approx(float(np.max(rel)))


def test_aicc_constant_K_ordering():
    """All candidates share K in Try2, so AICc ordering == RSS ordering."""
    rss = [1e-3, 2e-3, 5e-2]
    a = [aicc(r, n_obs=60, p=4) for r in rss]
    assert np.argsort(a).tolist() == np.argsort(rss).tolist()


@pytest.mark.skipif(not HAS_TRY1, reason="Try1 package not found")
def test_formulas_match_try1():
    rng = np.random.default_rng(11)
    z = 100 * (rng.standard_normal(15) + 1j * rng.standard_normal(15))
    zf = 100 * (rng.standard_normal(15) + 1j * rng.standard_normal(15))
    for rss in (1e-4, 3.7, 1e9):
        assert aicc(rss, 30, 5) == pytest.approx(try1_aicc(rss, 30, 5))
    assert fit_metrics(z, zf) == try1_fit(z, zf)
