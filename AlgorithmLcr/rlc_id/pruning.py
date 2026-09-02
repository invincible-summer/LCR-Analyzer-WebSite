"""Pruning filters (DESIGN.md §5.4, §6.1, §7 table):

F2: asymptotic slope / termination filter.
    Estimate low- and high-frequency |Z| log-log slopes and endpoint phases.
    A topology whose high-frequency termination cannot produce the observed
    +1 (inductive) or -1 (capacitive) slope is pruned.

F3: pole-structure lower bound on reactive elements.
    Engine-B poles provide a lower bound on reactive element count: each real
    pole needs >= 1 reactive element, each conjugate pair needs >= 2.
    Topologies with fewer reactive elements than this bound are pruned.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .circuits import Leaf, Node, Tree, leaf_kinds
from .fit_engine_a import StartHints
from .fit_engine_b import RationalModel


@dataclass
class AsymptoticFeatures:
    """Low- and high-frequency asymptotic features extracted from Z(jw)."""

    slope_low: float      # log10|Z| / log10(w) slope at low-frequency end
    slope_high: float     # slope at high-frequency end
    phase_low_deg: float  # phase at lowest frequency [deg]
    phase_high_deg: float # phase at highest frequency [deg]
    r_level: float        # flat-region resistance magnitude estimate
    r_peak: float         # max|Z| (parallel-resonance peak level)
    r_floor: float        # min|Z| (series-resonance floor level)
    l_est: float          # inductance estimate from the band ends
    c_est: float          # capacitance estimate from the band ends
    w_res: float | None   # interior extremum angular frequency, if any


def extract_asymptotics(w: np.ndarray, z: np.ndarray) -> AsymptoticFeatures:
    """Estimate log-log slopes, endpoint phases and element magnitudes."""
    w = np.asarray(w, dtype=float)
    z = np.asarray(z, dtype=complex)
    mag = np.abs(z)
    phase_deg = np.angle(z, deg=True)
    lw = np.log10(w)
    lmag = np.log10(np.maximum(mag, 1e-300))

    k = min(4, max(2, len(w) // 5))
    slope_low = float(np.polyfit(lw[:k], lmag[:k], 1)[0])
    slope_high = float(np.polyfit(lw[-k:], lmag[-k:], 1)[0])

    r_level = float(np.median(mag))
    w_min, w_max = float(w[0]), float(w[-1])

    # L estimate (H): from whichever band end looks inductive (|Z| ~ w L).
    if slope_low > 0.5 or phase_deg[0] > 60.0:
        l_est = float(np.exp(np.mean(np.log(mag[:k] / w[:k]))))
    elif slope_high > 0.5 or phase_deg[-1] > 60.0:
        l_est = float(np.exp(np.mean(np.log(mag[-k:] / w[-k:]))))
    else:
        l_est = float(r_level / w_max)

    # C estimate (F): from whichever band end looks capacitive
    # (|Z| ~ 1/(w C)).
    if slope_high < -0.5 or phase_deg[-1] < -60.0:
        c_est = float(np.exp(np.mean(np.log(1.0 / (w[-k:] * mag[-k:])))))
    elif slope_low < -0.5 or phase_deg[0] < -60.0:
        c_est = float(np.exp(np.mean(np.log(1.0 / (w[:k] * mag[:k])))))
    else:
        c_est = float(1.0 / (w_max * r_level))

    # Detect interior resonance / anti-resonance peak/dip in |Z|
    w_res = None
    if len(mag) >= 7:
        mid_mag = mag[2:-1]  # allow the penultimate point to be the peak
        imax = int(np.argmax(mid_mag)) + 2
        imin = int(np.argmin(mid_mag)) + 2
        # prominence check: peak or valley >= 1.5x of endpoints
        if mag[imax] > 1.5 * mag[0] and mag[imax] > 1.5 * mag[-1]:
            w_res = float(w[imax])
        elif mag[imin] < 0.67 * mag[0] and mag[imin] < 0.67 * mag[-1]:
            w_res = float(w[imin])
    if w_res is None:
        # Resonance may sit too close to a band edge for the prominence
        # check.  When the band ends show a pure +1 then -1 slope (or the
        # reverse), the two asymptotes w*L and 1/(w*C) cross at the
        # resonance: w0 = 1/sqrt(L*C).
        signs = (slope_low > 0.5 and slope_high < -0.5) or \
                (slope_low < -0.5 and slope_high > 0.5)
        if signs and l_est > 0 and c_est > 0:
            w0 = 1.0 / np.sqrt(l_est * c_est)
            if w_min <= w0 <= w_max:
                w_res = float(w0)

    return AsymptoticFeatures(
        slope_low=slope_low,
        slope_high=slope_high,
        phase_low_deg=float(phase_deg[0]),
        phase_high_deg=float(phase_deg[-1]),
        r_level=r_level,
        r_peak=float(np.max(mag)),
        r_floor=float(np.min(mag)),
        l_est=l_est,
        c_est=c_est,
        w_res=w_res,
    )


def hints_from_features(feat: AsymptoticFeatures) -> StartHints:
    return StartHints(
        r_level=feat.r_level,
        l_est=feat.l_est,
        c_est=feat.c_est,
        w_res=feat.w_res,
        r_peak=feat.r_peak,
    )


# ---------------------------------------------------------------------------
# High-frequency termination query
# ---------------------------------------------------------------------------

def high_freq_slope_range(tree: Tree) -> tuple[int, int]:
    """Asymptotic log-log slope bounds for s -> inf.

    Returns (min_slope, max_slope) in {-1, 0, +1}, where
      +1 = series-L dominance,
      -1 = parallel-C dominance,
       0 = resistive / mixed.
    """
    if isinstance(tree, Leaf):
        if tree.kind == "L":
            return (+1, +1)
        if tree.kind == "C":
            return (-1, -1)
        return (0, 0)
    ranges = [high_freq_slope_range(c) for c in tree.children]
    if tree.kind == "SER":
        # in series, the child with the highest slope dominates at s -> inf
        return (max(r[0] for r in ranges), max(r[1] for r in ranges))
    # in parallel, the child with the lowest slope dominates at s -> inf
    return (min(r[0] for r in ranges), min(r[1] for r in ranges))


# ---------------------------------------------------------------------------
# Pruning rules
# ---------------------------------------------------------------------------

def prune_f2(tree: Tree, feat: AsymptoticFeatures) -> bool:
    """True if tree should be KEPT, False if pruned by F2."""
    s_min, s_max = high_freq_slope_range(tree)
    # strong inductive trend at high frequency: reject topologies that can
    # only be capacitive at s -> inf
    if feat.slope_high > 0.65 and feat.phase_high_deg > 60.0:
        if s_max < 0:
            return False
    # strong capacitive trend at high frequency: reject topologies that can
    # only be inductive at s -> inf
    if feat.slope_high < -0.65 and feat.phase_high_deg < -60.0:
        if s_min > 0:
            return False
    return True


def prune_f3(tree: Tree, min_energy: int) -> bool:
    """True if tree should be KEPT, False if pruned by F3 (energy elements)."""
    kinds = leaf_kinds(tree)
    n_energy = kinds.count("L") + kinds.count("C")
    return n_energy >= min_energy


def prune(trees: list[Tree], feat: AsymptoticFeatures,
          min_energy: int = 0,
          enable_f2: bool = True, enable_f3: bool = True) -> list[Tree]:
    """Apply F2 and F3 filters; fallback to full list if over-pruned."""
    out: list[Tree] = []
    for t in trees:
        if enable_f2 and not prune_f2(t, feat):
            continue
        if enable_f3 and not prune_f3(t, min_energy):
            continue
        out.append(t)
    return out if out else list(trees)
