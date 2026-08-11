import numpy as np
from app.dsp.circuit_fit import MODELS, fit_circuit


def test_recover_series_rlc():
    R, L, C = 50.0, 1e-3, 1e-6
    md = MODELS["series_RLC"]
    freqs = np.logspace(2, 5, 40)
    Z = md.func([R, L, C], 2 * np.pi * freqs)
    res = fit_circuit("series_RLC", freqs, Z)
    assert abs(res.params["R"] - R) / R < 0.01
    assert abs(res.params["L"] - L) / L < 0.05
    assert abs(res.params["C"] - C) / C < 0.05
    assert res.accuracy > 0.999


def test_recover_series_rc():
    R, C = 330.0, 100e-9
    md = MODELS["series_RC"]
    freqs = np.logspace(2, 6, 40)
    Z = md.func([R, C], 2 * np.pi * freqs)
    res = fit_circuit("series_RC", freqs, Z)
    assert abs(res.params["R"] - R) / R < 0.02
    assert abs(res.params["C"] - C) / C < 0.05
    assert res.accuracy > 0.999


def test_recover_parallel_rc():
    R, C = 1000.0, 1e-7
    md = MODELS["parallel_RC"]
    freqs = np.logspace(2, 6, 40)
    Z = md.func([R, C], 2 * np.pi * freqs)
    res = fit_circuit("parallel_RC", freqs, Z)
    assert abs(res.params["R"] - R) / R < 0.02
    assert abs(res.params["C"] - C) / C < 0.05
    assert res.accuracy > 0.999


def test_theory_curve_has_smooth_grid():
    md = MODELS["series_RL"]
    freqs = np.logspace(2, 5, 10)
    Z = md.func([22.0, 1e-3], 2 * np.pi * freqs)
    res = fit_circuit("series_RL", freqs, Z)
    assert len(res.theory["frequency"]) == 200
    assert len(res.theory["z_mag"]) == 200
