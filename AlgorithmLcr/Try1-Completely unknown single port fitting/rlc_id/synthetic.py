"""Synthetic DUT suite (DESIGN.md section 8.2, v2 model) and measurement
simulation.

14 DUTs covering low/mid/high structural complexity.  Every inductor is a
REAL inductor: ideal L in series with a winding DC resistance (one device,
two parameters).  DUT9/DUT10 exercise the new parallel-multi-L topology
class (two (L + Rd) devices in parallel = a second-order tank that does not
merge into one inductor).  Simulated measurements: 30 log-spaced
frequencies from 10 Hz to 10 MHz, optional complex Gaussian noise with
independent Re/Im parts and sigma_k = sigma_rel * |z_k| (assumption A3),
fixed seeds for reproducibility.
"""

from __future__ import annotations

from dataclasses import dataclass
from itertools import permutations, product

import numpy as np

from .circuits import (SER, PAR, PARAM_KINDS, Leaf, Tree, assemble, canonical,
                       evaluate_f, n_params, to_string)

F_MIN = 10.0
F_MAX = 10e6
N_POINTS = 30
DEFAULT_SIGMA_REL = 0.005
DEFAULT_SEED = 0


@dataclass
class DUT:
    """A synthetic device under test: canonical tree + parameter values."""

    name: str
    group: str
    tree: Tree
    values: np.ndarray  # linear parameter values in canonical order
                        # (an L device contributes [L, Rd])

    @property
    def theta(self) -> np.ndarray:
        return np.log10(self.values)

    def z_exact(self, f: np.ndarray) -> np.ndarray:
        return evaluate_f(self.tree, self.theta, np.asarray(f, dtype=float))

    def describe(self) -> str:
        return to_string(self.tree, self.theta)


def _ser(*children) -> tuple[Tree, list[float]]:
    return assemble(SER, list(children))


def _par(*children) -> tuple[Tree, list[float]]:
    return assemble(PAR, list(children))


def _L(l: float, rd: float) -> tuple[Leaf, list[float]]:
    """A real inductor device: (leaf, [L, Rd])."""
    return (Leaf("L"), [l, rd])


def make_duts() -> list[DUT]:
    """The 14 synthetic DUTs of the v2 suite (real inductors: L + DCR)."""
    specs: list[tuple[str, str, Tree, list[float]]] = []

    # 1. single devices (dut1b is a real 1 mH inductor with 5 ohm winding)
    specs.append(("dut1a_R", "1_single", Leaf("R"), [100.0]))
    t, v = _L(1e-3, 5.0)
    specs.append(("dut1b_L", "1_single", t, v))
    specs.append(("dut1c_C", "1_single", Leaf("C"), [1e-8]))

    # 2. series two-device
    t, v = _ser((Leaf("R"), [1e3]), (Leaf("C"), [1e-10]))
    specs.append(("dut2a_ser_RC", "2_series2", t, v))
    t, v = _ser(_L(1e-5, 5.0), (Leaf("C"), [1e-9]))  # series res ~1.6 MHz, Q~20
    specs.append(("dut2b_ser_LC", "2_series2", t, v))

    # 3. parallel two-device
    t, v = _par((Leaf("R"), [1e3]), (Leaf("C"), [1e-8]))
    specs.append(("dut3a_par_RC", "3_parallel2", t, v))
    t, v = _par((Leaf("R"), [50.0]), _L(1e-4, 2.0))
    specs.append(("dut3b_par_RL", "3_parallel2", t, v))

    # 4. inductor parasitic model: (L with winding Rd) || Cp, Cp = 50 pF
    #    (self-resonance ~7.1 MHz)
    t, v = _par(_L(1e-5, 1.0), (Leaf("C"), [50e-12]))
    specs.append(("dut4_ind_parasitic", "4_ind_parasitic", t, v))

    # 5. capacitor parasitic model: ESL (with its Rd) + C
    #    (series resonance ~1.1 MHz)
    t, v = _ser(_L(2e-9, 0.05), (Leaf("C"), [1e-5]))
    specs.append(("dut5_cap_parasitic", "5_cap_parasitic", t, v))

    # 6. series-parallel mix: R1 + (R2 || C), relaxation pole ~1.6 kHz
    t, v = _ser((Leaf("R"), [100.0]),
                _par((Leaf("R"), [1e3]), (Leaf("C"), [1e-7])))
    specs.append(("dut6_relaxation", "6_mixed3", t, v))

    # 7. second-order tank: R || L || C (resonance ~5 MHz)
    t, v = _par((Leaf("R"), [1e3]), _L(1e-5, 0.5), (Leaf("C"), [1e-10]))
    specs.append(("dut7_tank", "7_tank", t, v))

    # 8. five-device double peak: R1 + (L1||C1) + (L2||C2); moderate tank Q
    #    (Rd sized so the damping is resolvable above 0.5% noise)
    t, v = _ser((Leaf("R"), [10.0]),
                _par(_L(1e-5, 10.0), (Leaf("C"), [1e-10])),
                _par(_L(1e-4, 20.0), (Leaf("C"), [1e-9])))
    specs.append(("dut8_double_peak", "8_double_peak", t, v))

    # 9. NEW v2 class: two real inductors in parallel (second-order, the
    #    parallel (L + Rd) devices do NOT merge); zeros 500 / 5e4 rad/s with
    #    the pole at 5e3 -- a decade from each (well-conditioned)
    t, v = _par(_L(1e-2, 5.0), _L(1e-3, 50.0))
    specs.append(("dut9_par_LL", "9_par_LL", t, v))

    # 10. NEW v2 class: R + (L1 || L2), multi-L PAR node inside a SER chain;
    #     zeros 1e4 / 3e7 with the pole at 3e5 -- the in-band L1 signature
    #     (zero-pole wiggle) sits an order of magnitude above the noise floor
    t, v = _ser((Leaf("R"), [100.0]),
                _par(_L(1e-3, 10.0), _L(1e-5, 300.0)))
    specs.append(("dut10_ser_R_par_LL", "10_ser_R_par_LL", t, v))

    return [DUT(name=n, group=g, tree=t, values=np.asarray(v, dtype=float))
            for n, g, t, v in specs]


def default_frequencies() -> np.ndarray:
    """30 log-spaced frequencies from 10 Hz to 10 MHz (section 8.2)."""
    return np.geomspace(F_MIN, F_MAX, N_POINTS)


def measure(dut: DUT, f: np.ndarray | None = None,
            sigma_rel: float = DEFAULT_SIGMA_REL,
            seed: int = DEFAULT_SEED) -> tuple[np.ndarray, np.ndarray]:
    """Simulate a measurement: exact Z plus complex Gaussian noise.

    Noise model (A3): Re/Im independent, sigma_k = sigma_rel * |z_k|.
    Returns (f, z_noisy).
    """
    if f is None:
        f = default_frequencies()
    f = np.asarray(f, dtype=float)
    z = dut.z_exact(f)
    if sigma_rel > 0:
        rng = np.random.default_rng(seed)
        noise = sigma_rel * np.abs(z) * (rng.standard_normal(len(z))
                                         + 1j * rng.standard_normal(len(z)))
        z = z + noise
    return f, z


def _interchange_groups(tree: Tree) -> list[list[tuple[int, int]]]:
    """Parameter blocks of interchangeable siblings.

    Children with IDENTICAL canonical strings (two L leaves under one PAR
    node, or two structurally identical subtrees) are electrically
    interchangeable: the canonical order cannot distinguish them, so a fit
    may assign their parameter blocks in either order.  Returns, per node,
    the list of (start, width) parameter blocks of each such sibling group.
    """
    groups: list[list[tuple[int, int]]] = []

    def rec(t: Tree, offset: int) -> int:
        if isinstance(t, Leaf):
            return offset + len(PARAM_KINDS[t.kind])
        blocks: list[tuple[str, int, int]] = []
        off = offset
        for c in t.children:
            w = n_params(c)
            blocks.append((canonical(c), off, w))
            off = rec(c, off)
        i = 0
        while i < len(blocks):
            j = i
            while j + 1 < len(blocks) and blocks[j + 1][0] == blocks[i][0]:
                j += 1
            if j > i:
                groups.append([(blocks[k][1], blocks[k][2])
                               for k in range(i, j + 1)])
            i = j + 1
        return off

    rec(tree, 0)
    return groups


def max_param_error(theta_fit: np.ndarray, dut: DUT) -> float:
    """Max relative parameter error, minimized over interchangeable-sibling
    permutations (two L leaves under one PAR node -- or two identical
    subtrees -- may legitimately have their parameter blocks swapped by the
    fitter).  Requires the fitted topology to be structurally identical to
    the DUT (same canonical string)."""
    fit = np.power(10.0, np.asarray(theta_fit, dtype=float))
    truth = np.asarray(dut.values, dtype=float)
    groups = _interchange_groups(dut.tree)
    base = list(range(len(truth)))
    choices = [list(permutations(range(len(g)))) for g in groups]
    best = np.inf
    for combo in product(*choices) if choices else [()]:
        idx = base.copy()
        for g, perm in zip(groups, combo):
            for slot, src in enumerate(perm):
                start_src, width = g[src]
                for k in range(width):
                    idx[g[slot][0] + k] = start_src + k
        rel = np.abs(fit[idx] - truth) / truth
        best = min(best, float(rel.max()))
    return best
