"""Impedance derivation from V and I sine fits, plus LCR parameter extraction."""
from __future__ import annotations
import math
from dataclasses import dataclass
from typing import Optional
import numpy as np

from .sine_fit import sine_fit, SineFit


@dataclass
class ImpedancePoint:
    frequency: float
    omega: float
    z_mag: float                 # |Z|
    z_phase: float               # angle(Z) (rad)
    z_real: float                # R = Re(Z)
    z_imag: float                # X = Im(Z)
    R: float
    X: float
    D: Optional[float]           # dissipation factor |R/X|
    Q: Optional[float]           # quality factor 1/D
    esr: float                   # equivalent series resistance
    L_eq: Optional[float]        # henry (inductive)
    C_eq: Optional[float]        # farad (capacitive)
    v_fit: SineFit
    i_fit: SineFit


def measure_impedance(voltage, current, dt: float, frequency: float) -> ImpedancePoint:
    """Compute the complex impedance at ``frequency`` from raw V and I traces.

    ``dt`` is the sampling interval (seconds). Time axis is ``t[k] = k*dt``.
    """
    voltage = np.asarray(voltage, dtype=float)
    current = np.asarray(current, dtype=float)
    if voltage.shape != current.shape:
        raise ValueError("voltage and current must have the same shape")

    n = voltage.size
    t = np.arange(n, dtype=float) * float(dt)
    omega = 2.0 * math.pi * float(frequency)
    v_fit = sine_fit(t, voltage, omega)
    i_fit = sine_fit(t, current, omega)

    if i_fit.amp == 0.0:
        raise ValueError("current amplitude is zero; cannot compute impedance")

    z_mag = v_fit.amp / i_fit.amp
    z_phase = v_fit.phase - i_fit.phase
    z_real = z_mag * math.cos(z_phase)
    z_imag = z_mag * math.sin(z_phase)

    R, X = z_real, z_imag
    esr = R
    D = Q = L_eq = C_eq = None
    if abs(X) > 1e-30:
        D = abs(R / X)
        Q = 1.0 / D if D != 0 else None
        if X < 0:                     # capacitive: X = -1/(wC)
            C_eq = -1.0 / (omega * X)
        else:                         # inductive: X = wL
            L_eq = X / omega

    return ImpedancePoint(
        frequency=float(frequency), omega=float(omega),
        z_mag=float(z_mag), z_phase=float(z_phase),
        z_real=float(z_real), z_imag=float(z_imag),
        R=float(R), X=float(X),
        D=(None if D is None else float(D)),
        Q=(None if Q is None else float(Q)),
        esr=float(esr),
        L_eq=(None if L_eq is None else float(L_eq)),
        C_eq=(None if C_eq is None else float(C_eq)),
        v_fit=v_fit, i_fit=i_fit,
    )
