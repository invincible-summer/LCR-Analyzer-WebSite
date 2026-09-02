"""Synthetic DUT suite (DESIGN.md section 8.2) and measurement simulation.

12 DUTs in 8 classes covering low/mid/high structural complexity.  Simulated
measurements: 30 log-spaced frequencies from 10 Hz to 10 MHz, optional
complex Gaussian noise with independent Re/Im parts and sigma_k =
sigma_rel * |z_k| (assumption A3), fixed seeds for reproducibility.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .circuits import SER, PAR, Leaf, Tree, assemble, evaluate_f, to_string

F_MIN = 10.0
F_MAX = 10e6
N_POINTS = 30
DEFAULT_SIGMA_REL = 0.005
DEFAULT_SEED = 0


@dataclass
class DUT:
    """A synthetic device under test: canonical tree + element values."""

    name: str
    group: str
    tree: Tree
    values: np.ndarray  # linear element values in canonical leaf order

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


def make_duts() -> list[DUT]:
    """The 12 synthetic DUTs of section 8.2 (DUT4 uses Cp = 50 pF)."""
    specs: list[tuple[str, str, Tree, list[float]]] = []

    # 1. single elements
    specs.append(("dut1a_R", "1_single", Leaf("R"), [100.0]))
    specs.append(("dut1b_L", "1_single", Leaf("L"), [1e-3]))
    specs.append(("dut1c_C", "1_single", Leaf("C"), [1e-8]))

    # 2. series two-element
    t, v = _ser((Leaf("R"), [50.0]), (Leaf("L"), [1e-3]))
    specs.append(("dut2a_ser_RL", "2_series2", t, v))
    t, v = _ser((Leaf("R"), [1e3]), (Leaf("C"), [1e-10]))
    specs.append(("dut2b_ser_RC", "2_series2", t, v))

    # 3. parallel two-element
    t, v = _par((Leaf("R"), [1e3]), (Leaf("C"), [1e-8]))
    specs.append(("dut3a_par_RC", "3_parallel2", t, v))
    t, v = _par((Leaf("R"), [50.0]), (Leaf("L"), [1e-4]))
    specs.append(("dut3b_par_RL", "3_parallel2", t, v))

    # 4. inductor parasitic model: (Rs + L) || Cp,  Cp = 50 pF (self-res ~7.1 MHz)
    t, v = _par(_ser((Leaf("R"), [1.0]), (Leaf("L"), [1e-5])),
                (Leaf("C"), [50e-12]))
    specs.append(("dut4_ind_parasitic", "4_ind_parasitic", t, v))

    # 5. capacitor parasitic model: Resr + Lesl + C (series resonance ~1.1 MHz)
    t, v = _ser((Leaf("R"), [0.05]), (Leaf("L"), [2e-9]), (Leaf("C"), [1e-5]))
    specs.append(("dut5_cap_parasitic", "5_cap_parasitic", t, v))

    # 6. series-parallel mix: R1 + (R2 || C), relaxation pole ~1.6 kHz
    t, v = _ser((Leaf("R"), [100.0]),
                _par((Leaf("R"), [1e3]), (Leaf("C"), [1e-7])))
    specs.append(("dut6_relaxation", "6_mixed3", t, v))

    # 7. second-order tank: R || L || C (resonance ~5 MHz)
    t, v = _par((Leaf("R"), [1e3]), (Leaf("L"), [1e-5]), (Leaf("C"), [1e-10]))
    specs.append(("dut7_tank", "7_tank", t, v))

    # 8. four-element double peak: R1 + (L1||C1) + (L2||C2)
    t, v = _ser((Leaf("R"), [10.0]),
                _par((Leaf("L"), [1e-5]), (Leaf("C"), [1e-10])),
                _par((Leaf("L"), [1e-4]), (Leaf("C"), [1e-9])))
    specs.append(("dut8_double_peak", "8_double_peak", t, v))

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


def max_param_error(theta_fit: np.ndarray, dut: DUT) -> float:
    """Max relative element-value error; requires the fitted topology to be
    structurally identical to the DUT (same canonical string)."""
    fit_vals = np.power(10.0, theta_fit)
    return float(np.max(np.abs(fit_vals - dut.values) / dut.values))
