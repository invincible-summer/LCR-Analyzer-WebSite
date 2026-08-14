"""Tests for the multi-start topology fitter."""
from __future__ import annotations
import numpy as np
import pytest

from app.dsp.topology_fit import MODELS, fit_topology, _PHYS_BOUNDS


def _z_model(name, p, f):
    md = MODELS[name]
    return md.func(np.asarray(p, dtype=float), 2 * np.pi * np.asarray(f))


def test_recovers_series_rlc_with_noise():
    f = np.logspace(2, 6, 60)
    rng = np.random.default_rng(3)
    Z = _z_model("series_RLC", [10.0, 1e-4, 1e-7], f)
    Zn = Z + (rng.normal(0, 1e-3, f.size) + 1j * rng.normal(0, 1e-3, f.size)) * np.abs(Z)
    res = fit_topology("series_RLC", f, Zn)
    assert res.params["R"] == pytest.approx(10.0, rel=0.05)
    assert res.params["L"] == pytest.approx(1e-4, rel=0.05)
    assert res.params["C"] == pytest.approx(1e-7, rel=0.05)
    assert res.converged


def test_recovers_parallel_rc_multistart():
    """Single heuristic start failed this topology historically (median-based
    R0 is meaningless); the LHS restarts must find the basin."""
    f = np.logspace(3, 6, 50)
    Z = _z_model("parallel_RC", [330.0, 4.7e-9], f)
    res = fit_topology("parallel_RC", f, Z)
    assert res.params["R"] == pytest.approx(330.0, rel=0.02)
    assert res.params["C"] == pytest.approx(4.7e-9, rel=0.05)


def test_recovers_rs_rp_c():
    f = np.logspace(2, 5, 60)
    Z = _z_model("Rs_Rp_C", [1.2, 8200.0, 1e-6], f)
    res = fit_topology("Rs_Rp_C", f, Z)
    assert res.params["Rs"] == pytest.approx(1.2, rel=0.1)
    assert res.params["Rp"] == pytest.approx(8200.0, rel=0.1)
    assert res.params["C"] == pytest.approx(1e-6, rel=0.1)


def test_confidence_intervals_bracket_truth():
    f = np.logspace(2, 6, 80)
    rng = np.random.default_rng(11)
    truth = [10.0, 1e-4, 1e-7]
    Z = _z_model("series_RLC", truth, f)
    Zn = Z + (rng.normal(0, 2e-3, f.size) + 1j * rng.normal(0, 2e-3, f.size)) * np.abs(Z)
    res = fit_topology("series_RLC", f, Zn)
    for i, k in enumerate(["R", "L", "C"]):
        lo, hi = res.param_ci[k]
        assert lo < truth[i] < hi, f"{k}: CI ({lo:.3g}, {hi:.3g}) misses {truth[i]:.3g}"
        assert lo > 0


def test_aicc_penalises_wrong_topology():
    """series_RLC data fitted by series_RL must lose on AICc (more params
    can't help when the model is wrong, fewer params can't capture the C)."""
    f = np.logspace(2, 6, 60)
    Z = _z_model("series_RLC", [10.0, 1e-4, 1e-7], f)
    right = fit_topology("series_RLC", f, Z)
    wrong = fit_topology("series_RL", f, Z)
    assert right.aicc < wrong.aicc


def test_all_models_recover_their_own_data():
    f = np.logspace(2, 6, 50)
    truths = {
        "series_RLC": [10.0, 1e-4, 1e-7],
        "series_RC": [47.0, 1e-8],
        "series_RL": [47.0, 1e-5],
        "parallel_RLC": [1000.0, 1e-3, 1e-7],
        "parallel_RC": [1000.0, 1e-8],
        "parallel_RL": [100.0, 1e-4],
        "R_LC_parallel": [50.0, 1e-3, 1e-7],   # SRF ~16 kHz, inside the band
        "Rs_Rp_C": [2.0, 5000.0, 1e-7],
    }
    for name, truth in truths.items():
        Z = _z_model(name, truth, f)
        res = fit_topology(name, f, Z, n_starts=12)
        for key, val in zip(MODELS[name].params, truth):
            # near-lossless R+L∥C: only the L*C product (SRF position) is
            # identifiable from 50 pts/decade, individual L and C are not
            tol = 0.5 if name == "R_LC_parallel" and key in ("L", "C") else 0.1
            assert res.params[key] == pytest.approx(val, rel=tol), \
                f"{name}.{key}: {res.params[key]:.4g} vs {val:.4g}"
        if name == "R_LC_parallel":
            lc = res.params["L"] * res.params["C"]
            assert lc == pytest.approx(1e-3 * 1e-7, rel=0.1)


def test_sigma_weighting_reduces_outlier_damage():
    f = np.logspace(2, 6, 50)
    Z = _z_model("series_RLC", [10.0, 1e-4, 1e-7], f)
    sigma = np.abs(Z) * 1e-3
    Z_bad = Z.copy()
    Z_bad[10] *= 5                       # one glitchy point
    sigma_bad = sigma.copy()
    sigma_bad[10] = np.abs(Z_bad[10])    # ...but with a large known sigma
    res = fit_topology("series_RLC", f, Z_bad, sigma=sigma_bad)
    assert res.params["R"] == pytest.approx(10.0, rel=0.05)
    assert res.params["C"] == pytest.approx(1e-7, rel=0.1)
