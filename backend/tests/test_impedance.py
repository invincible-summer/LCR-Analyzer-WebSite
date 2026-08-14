import math
import numpy as np
from app.dsp.impedance import measure_impedance


def _gen(R, L, C, f, n=2048, spc=64, cycles=16, i0=0.01):
    """Drive i(t)=I0 sin(wt); measure v(t)=I0|Z|sin(wt+angle Z)."""
    omega = 2 * math.pi * f
    dt = 1.0 / (f * spc)
    N = spc * cycles
    t = np.arange(N) * dt
    Z = R + 1j * (omega * L - 1.0 / (omega * C))
    i = i0 * np.sin(omega * t)
    v = i0 * abs(Z) * np.sin(omega * t + math.atan2(Z.imag, Z.real))
    return v, i, dt


def test_pure_resistor():
    R = 100.0
    v, i, dt = _gen(R, 0.0, 1e12, 1000.0)     # C huge -> X negligible -> pure R
    pt = measure_impedance(v, i, dt, 1000.0)
    assert abs(pt.z_mag - R) < R * 1e-3
    assert abs(pt.z_phase) < 1e-2
    assert abs(pt.R - R) < R * 1e-3


def test_capacitor_near_minus_90():
    R, C, f = 1.0, 1e-6, 1000.0
    v, i, dt = _gen(R, 0.0, C, f)
    pt = measure_impedance(v, i, dt, f)
    assert pt.z_phase < 0
    assert abs(pt.z_phase + math.pi / 2) < 0.05           # within ~3 deg
    assert abs(pt.C_eq - C) / C < 0.05


def test_inductor_near_plus_90():
    R, L, f = 1.0, 1e-3, 1000.0
    v, i, dt = _gen(R, L, 1e12, f)
    pt = measure_impedance(v, i, dt, f)
    omega = 2 * math.pi * f
    true_phase = math.atan2(omega * L, R)        # = atan2(X, R), ~80.96 deg here
    assert pt.z_phase > 0
    assert abs(pt.z_phase - true_phase) < 0.01
    assert abs(pt.L_eq - L) / L < 0.02


def test_sigma_scales_with_noise():
    """z_sigma must grow with injected noise and match its magnitude."""
    rng = np.random.default_rng(5)
    R, L, f = 100.0, 1e-4, 1000.0

    def run(noise):
        v, i, dt = _gen(R, L, 1e12, f)
        v = v + rng.normal(0, noise, v.size)
        return measure_impedance(v, i, dt, f)

    clean = run(0.0)
    assert clean.z_sigma < 1e-12                # only float rounding remains
    small = run(1e-3)
    big = run(1e-2)
    assert 0 < small.z_sigma < big.z_sigma
    assert big.z_sigma / big.z_mag < 0.01       # far below the signal
    assert big.z_phase_sigma > small.z_phase_sigma


def test_sigma_brackets_repeat_measurements():
    """Empirical spread of repeated measurements must be ~ z_sigma."""
    rng = np.random.default_rng(9)
    R, L, f = 50.0, 1e-3, 500.0
    noise = 0.02
    mags = []
    sig = None
    for _ in range(24):
        v, i, dt = _gen(R, L, 1e12, f)
        v = v + rng.normal(0, noise, v.size)
        pt = measure_impedance(v, i, dt, f)
        mags.append(pt.z_mag)
        sig = pt.z_sigma
    emp = np.std(mags)
    assert 0.5 * sig < emp < 2.0 * sig, f"empirical {emp:.4g} vs reported {sig:.4g}"
