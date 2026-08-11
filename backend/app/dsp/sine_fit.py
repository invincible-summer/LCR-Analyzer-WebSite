"""Three-parameter sine fit (IEEE 1057) for a *known* excitation frequency.

We fit  x[k] ~= a*sin(w*t_k) + b*cos(w*t_k) + c   (ordinary least squares).

Because the ESP32 generates the excitation, w = 2*pi*f is known exactly. This
makes the sine fit the optimal (ML under AWGN) amplitude/phase estimator:
no spectral leakage, no windowing, and the DC offset `c` is removed for free.

Phase convention used everywhere in this codebase:

    fitted(t) = amp * sin(w*t + phi)
              = (amp*cos phi)*sin(w*t) + (amp*sin phi)*cos(w*t)

so  a = amp*cos(phi), b = amp*sin(phi)  ->  amp = hypot(a,b), phi = atan2(b,a).

Both V and I channels use the same convention, so the impedance phase
angle(Z) = phi_v - phi_i is correct by construction.
"""
from __future__ import annotations
from dataclasses import dataclass
import numpy as np


@dataclass
class SineFit:
    amp: float            # amplitude sqrt(a^2+b^2)
    phase: float          # phase phi (rad)
    dc: float             # fitted offset c
    a: float
    b: float
    fitted: np.ndarray    # full fitted curve (AC + DC), len == len(x)
    resid: np.ndarray     # residuals x - fitted
    resid_rms: float      # RMS of residuals (noise + harmonic distortion)


def sine_fit(t: np.ndarray, x, omega: float) -> SineFit:
    """Fit a single sinusoid at known angular frequency ``omega``."""
    t = np.asarray(t, dtype=float)
    x = np.asarray(x, dtype=float)
    if x.size < 3:
        raise ValueError("need at least 3 samples for a 3-parameter fit")

    s = np.sin(omega * t)
    c = np.cos(omega * t)
    design = np.column_stack([s, c, np.ones_like(t)])
    (a, b, dc), _res, _rank, _sv = np.linalg.lstsq(design, x, rcond=None)

    fitted = a * s + b * c + dc
    resid = x - fitted
    amp = float(np.hypot(a, b))
    phase = float(np.arctan2(b, a))
    return SineFit(
        amp=amp, phase=phase, dc=float(dc),
        a=float(a), b=float(b),
        fitted=fitted, resid=resid,
        resid_rms=float(np.sqrt(np.mean(resid ** 2))),
    )
