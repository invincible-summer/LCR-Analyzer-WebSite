"""Synthetic DUTs: named multigraphs + random cases + noisy measurements.

Noise model follows Try1/Try2 (A3): complex Gaussian, Re/Im independent,
sigma_k = sigma_rel * |z_k|.  Default band 10 Hz .. 10 MHz, 30 log-spaced
points -- identical to Try1/Try2 so difficulty is comparable.

Value conventions per original edge:
    R -> ("R", r);  C -> ("C", c);  L -> ("L", l, rd)
flat linear value vectors follow the fit.py/nodal.py parameter order
(per edge in order; L edges carry l then rd).
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .graph import reduce_graph
from .nodal import NodalModel, model_from_reduced

F_MIN, F_MAX, N_POINTS = 10.0, 10e6, 30
DEFAULT_SIGMA_REL = 0.005

# true-value log-uniform ranges for random cases (inside PHYS_BOUNDS)
RANDOM_RANGES = {"R": (0.0, 6.0), "C": (-12.0, -6.0),
                 "L": (-8.0, 0.0), "Rd": (-3.0, 3.0)}


def default_frequencies(n: int = N_POINTS) -> np.ndarray:
    return np.logspace(np.log10(F_MIN), np.log10(F_MAX), n)


@dataclass
class DUT:
    name: str
    edges: list                    # [(u, v, kind), ...]
    values: dict                   # orig edge idx -> value tuple

    def flat_values(self) -> np.ndarray:
        out = []
        for i, (u, v, k) in enumerate(self.edges):
            out.extend(self.values[i][1:])
        return np.asarray(out, dtype=float)

    def z_exact(self, f) -> np.ndarray:
        model = NodalModel.from_edges(self.edges)
        return model.z_linear(self.flat_values(),
                              1j * 2.0 * np.pi * np.asarray(f, dtype=float))

    def z_exact_reduced(self, f) -> np.ndarray:
        """Sanity path: evaluate through the reduced graph."""
        red = reduce_graph(self.edges)
        model = model_from_reduced(red)
        from .graph import eval_group
        vals = []
        for g in red.edges:
            vals.extend(eval_group(g.expr, self.values)[1:])
        return model.z_linear(np.asarray(vals, dtype=float),
                              1j * 2.0 * np.pi * np.asarray(f, dtype=float))


def measure(dut: DUT, f=None, sigma_rel: float = DEFAULT_SIGMA_REL,
            seed: int = 0):
    """Simulate a measurement (Try1 noise model A3).  Returns (f, z)."""
    if f is None:
        f = default_frequencies()
    f = np.asarray(f, dtype=float)
    z = dut.z_exact(f)
    if sigma_rel > 0:
        rng = np.random.default_rng(seed)
        z = z + sigma_rel * np.abs(z) * (rng.standard_normal(len(z))
                                         + 1j * rng.standard_normal(len(z)))
    return f, z


# ---------------------------------------------------------------------------
# named DUTs (edge kinds fixed; values are the ground truth)
# ---------------------------------------------------------------------------

def make_duts() -> list:
    specs = []

    def add(name, edges, raw):
        vals = {}
        for i, (u, v, k) in enumerate(edges):
            vals[i] = (k, raw[i][0], raw[i][1]) if k == "L" else (k, raw[i][0])
        specs.append(DUT(name, list(edges), vals))

    # 1. series R-C (3 nodes, classic)
    add("ser_rc", [(0, 2, "R"), (2, 1, "C")],
        [(1e3,), (100e-9,)])
    # 2. all-parallel multiedge R || C || L(dcr) -- parallel different kinds
    add("par_rlc", [(0, 1, "R"), (0, 1, "C"), (0, 1, "L")],
        [(1e3,), (10e-9,), (1e-3, 1.0)])
    # 3. inductor parasitic (Rs + L) || Cp  -- Try1 dut4 equivalent as graph
    add("ind_parasitic", [(0, 2, "R"), (2, 1, "L"), (0, 1, "C")],
        [(1.0,), (10e-6, 0.5), (50e-12,)])
    # 4. ladder: series L then (C || R) shunt
    add("ladder", [(0, 2, "L"), (2, 1, "C"), (2, 1, "R")],
        [(1e-3, 2.0), (100e-9,), (10e3,)])
    # 5. Wheatstone-style bridge with a capacitor cross arm: NOT series-parallel
    add("bridge", [(0, 2, "R"), (0, 3, "R"), (2, 1, "R"), (3, 1, "R"),
                   (2, 3, "C")],
        [(1e3,), (2e3,), (3e3,), (4e3,), (10e-9,)])
    # 6. reducible: two series R plus a dangling C branch
    add("reducible", [(0, 2, "R"), (2, 1, "R"), (2, 3, "C")],
        [(100.0,), (220.0,), (1e-9,)])
    # 7. parallel different kinds R || C (Try1 canonical forbids same-kind
    #    leaves only; here multigraph R || C across the port directly)
    add("par_rc", [(0, 1, "R"), (0, 1, "C")],
        [(1e3,), (10e-9,)])
    # 8. two parallel inductors (same kind, NOT parallel-mergeable)
    add("par_ll", [(0, 1, "L"), (0, 1, "L")],
        [(1e-3, 1.0), (100e-6, 10.0)])
    # 9. series R absorbed into L (F4): (R + L + Rd)
    add("ser_rl_absorb", [(0, 2, "R"), (2, 1, "L")],
        [(50.0,), (1e-3, 2.0)])
    # 10. capacitor parasitic: Resr + Lesl + C in series
    add("cap_parasitic", [(0, 2, "R"), (2, 3, "L"), (3, 1, "C")],
        [(0.05,), (2e-9, 0.01), (10e-6,)])
    # 11. double tank ladder: L1 + (L2 || C) + C2 -- 4 storage elements
    add("double_tank", [(0, 2, "L"), (2, 3, "L"), (2, 3, "C"), (3, 1, "C")],
        [(1e-5, 0.2), (1e-4, 0.5), (1e-9,), (10e-9,)])
    # 12. nested reduction: (R1 || R2) + R3 with dangling C on the mid node
    add("nested_red", [(4, 1, "R"), (4, 1, "R"), (0, 4, "R"), (4, 5, "C")],
        [(1e3,), (2e3,), (300.0,), (1e-9,)])
    return specs


# ---------------------------------------------------------------------------
# random cases
# ---------------------------------------------------------------------------

def _pruefer_tree(rng: np.random.Generator, V: int) -> list:
    """Random labelled tree on nodes 0..V-1 from a uniform Pruefer code."""
    if V == 1:
        return []
    code = rng.integers(0, V, size=V - 2)
    degree = np.ones(V, dtype=int)
    for c in code:
        degree[c] += 1
    edges = []
    leaves = [n for n in range(V) if degree[n] == 1]
    import heapq
    heapq.heapify(leaves)
    code = list(code)
    for c in code:
        leaf = heapq.heappop(leaves)
        edges.append((leaf, int(c)))
        degree[leaf] -= 1
        degree[c] -= 1
        if degree[c] == 1:
            heapq.heappush(leaves, int(c))
    a = heapq.heappop(leaves)
    b = heapq.heappop(leaves)
    edges.append((a, b))
    return edges


def random_case(rng: np.random.Generator, name: str = "rand") -> DUT:
    """Random connected multigraph DUT: random tree + extra parallel/cross
    edges, random kinds, log-uniform values in RANDOM_RANGES."""
    V = int(rng.integers(2, 7))
    tree = _pruefer_tree(rng, V)
    edges = [ (min(u, v), max(u, v)) for (u, v) in tree ]
    n_extra = int(rng.integers(0, 4))
    for _ in range(n_extra):
        u, v = rng.integers(0, V, size=2)
        if u == v:
            continue
        edges.append((min(u, v), max(u, v)))
    kinds = []
    values = {}
    raw = []
    for i, (u, v) in enumerate(edges):
        k = ("R", "C", "L")[int(rng.integers(0, 3))]
        kinds.append(k)
        if k == "R":
            raw.append((10.0 ** rng.uniform(*RANDOM_RANGES["R"]),))
        elif k == "C":
            raw.append((10.0 ** rng.uniform(*RANDOM_RANGES["C"]),))
        else:
            raw.append((10.0 ** rng.uniform(*RANDOM_RANGES["L"]),
                        10.0 ** rng.uniform(*RANDOM_RANGES["Rd"])))
    edge_list = [(u, v, k) for (u, v), k in zip(edges, kinds)]
    for i, (u, v, k) in enumerate(edge_list):
        values[i] = (k, raw[i][0], raw[i][1]) if k == "L" else (k, raw[i][0])
    return DUT(name, edge_list, values)
