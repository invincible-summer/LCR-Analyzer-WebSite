import numpy as np
from app.dsp.sine_fit import sine_fit


def test_recovers_amplitude_phase_dc():
    n, dt, f = 1024, 1e-4, 100.0
    omega = 2 * np.pi * f
    t = np.arange(n) * dt
    amp, phi, dc = 1.7, 0.6, 0.3
    x = amp * np.sin(omega * t + phi) + dc
    fit = sine_fit(t, x, omega)
    assert abs(fit.amp - amp) < 1e-6
    assert abs(fit.phase - phi) < 1e-6
    assert abs(fit.dc - dc) < 1e-9
    assert fit.resid_rms < 1e-9


def test_robust_to_noise():
    rng = np.random.default_rng(0)
    n, dt, f = 2048, 5e-5, 1000.0
    omega = 2 * np.pi * f
    t = np.arange(n) * dt
    amp, phi = 2.0, -1.2
    x = amp * np.sin(omega * t + phi) + rng.normal(0, 0.05, n)
    fit = sine_fit(t, x, omega)
    assert abs(fit.amp - amp) < 0.02
    wrapped = (fit.phase - phi + np.pi) % (2 * np.pi) - np.pi
    assert abs(wrapped) < 0.02


def test_handles_cosine_input():
    n, dt, f = 1024, 1e-4, 100.0
    omega = 2 * np.pi * f
    t = np.arange(n) * dt
    x = np.cos(omega * t)            # phi should be ~ +pi/2 (sin + pi/2 == cos)
    fit = sine_fit(t, x, omega)
    assert abs(fit.amp - 1.0) < 1e-6
    assert abs(fit.phase - np.pi / 2) < 1e-6
