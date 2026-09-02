"""Batched nodal analysis of multigraph networks (DESIGN.md section 5).

Stamping rule: every edge (u, v) with admittance y contributes
Y[u,u] += y, Y[v,v] += y, Y[u,v] -= y, Y[v,u] -= y.  Parallel edges land in
the same matrix entries, so their admittances add directly -- for V = 2 the
single entry is exactly the closed-form parallel expression

    Z(f) = 1 / (sum_i 1/R_i + j*2*pi*f*sum_i C_i + sum_i 1/(R_Li + j*2*pi*f*L_i)).

Node 0 is grounded; with unit current injected at terminal node 1 the
driving-point impedance is Z = (Y_red^-1)[0, 0] where Y_red is the
(V-1)x(V-1) reduced matrix over nodes 1..V-1.

All heavy loops are vectorized over a batch of candidate assignments (the
same structure, different component-to-edge assignments); the frequency
loop stays in Python (M is small, <= ~200).
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .components import Component, ComponentSet
from .graph import Network, Structure

_KIND_R, _KIND_C, _KIND_L = 0, 1, 2


def comp_admittance(kind_codes: np.ndarray, vals: np.ndarray, dcrs: np.ndarray,
                    s: complex) -> np.ndarray:
    """Vectorized admittance of many components at one complex frequency."""
    y = np.empty(len(kind_codes), dtype=complex)
    mR = kind_codes == _KIND_R
    mC = kind_codes == _KIND_C
    mL = kind_codes == _KIND_L
    y[mR] = 1.0 / vals[mR]
    y[mC] = s * vals[mC]
    y[mL] = 1.0 / (dcrs[mL] + s * vals[mL])
    return y


@dataclass
class StructureStamps:
    """Precomputed stamping indices for one structure + component set.

    Instances are grouped by occupied slot (Structure.slot_of_instances is
    contiguous per slot), so pair admittances come from np.add.reduceat.
    """

    structure: Structure
    compset: ComponentSet
    kind_codes: np.ndarray          # (n_comp,)
    vals: np.ndarray                # (n_comp,)
    dcrs: np.ndarray                # (n_comp,)
    pair_nodes: list[tuple[int, int]]   # occupied slots, in slot order
    group_starts: np.ndarray        # reduceat boundaries, len = n_pairs
    diag_targets: list[tuple[int, int]]  # (pair_idx, reduced node) for each endpoint != 0
    offdiag: list[tuple[int, int, int]]  # (pair_idx, ri, rj) both endpoints != 0

    @classmethod
    def build(cls, structure: Structure, compset: ComponentSet) -> "StructureStamps":
        comps = compset.components
        kinds = np.array([{"R": _KIND_R, "C": _KIND_C, "L": _KIND_L}[c.kind] for c in comps])
        vals = np.array([c.value for c in comps])
        dcrs = np.array([c.dcr for c in comps])

        soi = structure.slot_of_instances()
        occupied = [k for k, m in enumerate(structure.mult) if m > 0]
        from .graph import slot_list
        slots = slot_list(structure.V)
        pair_nodes = [slots[k] for k in occupied]
        starts = np.array([soi.index(k) for k in occupied], dtype=np.intp)

        diag_targets: list[tuple[int, int]] = []
        offdiag: list[tuple[int, int, int]] = []
        for p, (i, j) in enumerate(pair_nodes):
            if i > 0:
                diag_targets.append((p, i - 1))
            if j > 0:
                diag_targets.append((p, j - 1))
            if i > 0 and j > 0:
                offdiag.append((p, i - 1, j - 1))
        return cls(structure, compset, kinds, vals, dcrs,
                   pair_nodes, starts, diag_targets, offdiag)

    # -- core batched evaluation -------------------------------------------
    def z_batch(self, assigns: np.ndarray, s: complex) -> np.ndarray:
        """Z at one frequency for assigns (N, E) of component indices."""
        N = assigns.shape[0]
        k = self.structure.V - 1
        y_comp = comp_admittance(self.kind_codes, self.vals, self.dcrs, s)
        y_inst = y_comp[assigns]                       # (N, E)
        y_pair = np.add.reduceat(y_inst, self.group_starts, axis=1)  # (N, P)

        diag = np.zeros((N, k), dtype=complex)
        for p, r in self.diag_targets:
            diag[:, r] += y_pair[:, p]
        Y = np.zeros((N, k, k), dtype=complex)
        for r in range(k):
            Y[:, r, r] = diag[:, r]
        for p, ri, rj in self.offdiag:
            Y[:, ri, rj] = -y_pair[:, p]
            Y[:, rj, ri] = -y_pair[:, p]

        e0 = np.zeros((N, k, 1), dtype=complex)
        e0[:, 0, 0] = 1.0
        try:
            v = np.linalg.solve(Y, e0)
            return v[:, 0, 0]
        except np.linalg.LinAlgError:
            return np.array([self._solve_one(Y[n], e0[n]) for n in range(N)])

    @staticmethod
    def _solve_one(Y: np.ndarray, e0: np.ndarray) -> complex:
        try:
            return complex(np.linalg.solve(Y, e0)[0, 0])
        except np.linalg.LinAlgError:
            return complex(np.inf)

    def z_full(self, assigns: np.ndarray, s_array: np.ndarray) -> np.ndarray:
        """Z at all frequencies; returns (N, M) complex."""
        out = np.empty((assigns.shape[0], len(s_array)), dtype=complex)
        for m, s in enumerate(s_array):
            out[:, m] = self.z_batch(assigns, s)
        return out


def network_z(network: Network, compset: ComponentSet,
              f: np.ndarray) -> np.ndarray:
    """Exact Z(f) of a single network (used for reports and synthetic data)."""
    stamps = StructureStamps.build(network.structure, compset)
    assign = np.array(network.assign, dtype=np.intp).reshape(1, -1)
    return stamps.z_full(assign, 1j * 2.0 * np.pi * np.asarray(f))[0]


# ---------------------------------------------------------------------------
# DC / HF asymptotic invariants (pure graph computations, DESIGN.md 5.3)
# ---------------------------------------------------------------------------

def _effective_resistance(nodes_edges: list[tuple[int, int, float]],
                          terminals: tuple[int, int]) -> float:
    """Driving-point resistance of a small resistor multigraph.

    nodes_edges: (u, v, conductance-or-resistance) with resistance values;
    terminals must appear in the edge list, else the network is open (inf).
    """
    touched = {n for e in nodes_edges for n in e[:2]}
    if any(t not in touched for t in terminals):
        return np.inf
    labels = {n: i for i, n in enumerate(sorted(touched | set(terminals)))}
    n = len(labels)
    Y = np.zeros((n, n), dtype=float)
    for u, v, r in nodes_edges:
        g = 1.0 / r
        i, j = labels[u], labels[v]
        Y[i, i] += g
        Y[j, j] += g
        Y[i, j] -= g
        Y[j, i] -= g
    t0, t1 = labels[terminals[0]], labels[terminals[1]]
    if t0 == t1:
        return 0.0
    k = n - 1
    Yr = np.zeros((k, k))
    keep = [i for i in range(n) if i != t0]
    for a, ia in enumerate(keep):
        for b, ib in enumerate(keep):
            Yr[a, b] = Y[ia, ib]
    e0 = np.zeros(k)
    e0[keep.index(t1)] = 1.0
    try:
        return float(np.linalg.solve(Yr, e0)[keep.index(t1)])
    except np.linalg.LinAlgError:
        return np.inf


def asymptote_impedance(network: Network, compset: ComponentSet,
                        mode: str) -> float:
    """Z(0) ('dc') or Z(inf) ('hf') of a network (DESIGN.md 5.3).

    dc: ideal inductors (dcr = 0) are shorts (node merge), capacitors open,
    resistors are R and lossy inductors are their dcr.
    hf: capacitors are shorts, inductors open, resistors stay R.
    """
    if mode not in ("dc", "hf"):
        raise ValueError("mode must be 'dc' or 'hf'")
    comps = compset.components
    V = network.structure.V
    from .graph import slot_list
    slots = slot_list(V)
    soi = network.structure.slot_of_instances()

    parent = list(range(V))

    def find(a: int) -> int:
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a: int, b: int) -> None:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    resistive: list[tuple[int, int, float]] = []
    for t, slot in enumerate(soi):
        c: Component = comps[network.assign[t]]
        u, v = slots[slot]
        if mode == "dc":
            if c.kind == "C":
                continue                      # open
            if c.kind == "L" and c.dcr == 0.0:
                union(u, v)                   # short
                continue
            r = c.value if c.kind == "R" else c.dcr
        else:
            if c.kind == "L":
                continue                      # open
            if c.kind == "C":
                union(u, v)                   # short
                continue
            r = c.value
        resistive.append((find(u), find(v), r))
    if find(0) == find(1):
        return 0.0
    resistive = [(find(u), find(v), r) for (u, v, r) in resistive]
    return _effective_resistance(resistive, (find(0), find(1)))
