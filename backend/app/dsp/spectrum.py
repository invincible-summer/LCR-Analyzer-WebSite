"""Single-sided FFT amplitude spectrum, for diagnostics only.

The impedance value itself comes from the sine fit (see ``impedance.py``).
The spectrum is shown so the user can spot harmonic distortion, out-of-band
noise, or a wrong excitation frequency.
"""
from __future__ import annotations
from dataclasses import dataclass
import numpy as np
from scipy import signal


@dataclass
class Spectrum:
    freqs: np.ndarray
    mag: np.ndarray       # single-sided amplitude spectrum (approx)


def fft_spectrum(x, dt: float, window: str = "hann") -> Spectrum:
    x = np.asarray(x, dtype=float)
    x = x - x.mean()
    n = x.size
    if n < 2:
        raise ValueError("need at least 2 samples")

    if window:
        w = signal.get_window(window, n)
        gain = n / np.sum(w)          # coherent-gain compensation
    else:
        w = np.ones(n)
        gain = 1.0

    spec = np.fft.rfft(x * w)
    freqs = np.fft.rfftfreq(n, d=dt)
    mag = (np.abs(spec) * 2.0 / n) * gain
    return Spectrum(freqs=freqs, mag=mag)
