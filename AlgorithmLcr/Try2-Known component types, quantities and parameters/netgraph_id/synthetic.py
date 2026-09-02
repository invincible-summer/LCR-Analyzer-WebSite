"""Synthetic DUTs: explicit and random topologies + noisy measurements.

Noise model follows Try1 (A3): complex Gaussian, real/imaginary parts
independent, sigma_k = sigma_rel * |z_k|.  Default band 10 Hz .. 10 MHz,
30 log-spaced points -- identical to Try1's synthetic module so that
difficulty is comparable across the two studies.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .components import Component, ComponentSet
from .graph import (Network, canonical_mult, empty_mult, has_dead_part,
                    is_connected, make_structure, perm_group, permute_mult,
                    slot_index, slot_list)
from .nodal import network_z

F_MIN, F_MAX, N_POINTS = 10.0, 10e6, 30


def default_frequencies(n: int = N_POINTS) -> np.ndarray:
    return np.logspace(np.log10(F_MIN), np.log10(F_MAX), n)


# ---------------------------------------------------------------------------
# building networks from an explicit edge list
# ---------------------------------------------------------------------------

def network_from_edges(compset: ComponentSet,
                       edges: list[tuple[int, int, int]]) -> Network:
    """Build a Network from (u, v, comp_index) triples.

    Terminals are nodes 0 and 1; parallel triples (same u,v) are legal.
    The structure is canonicalized; the assignment follows the canonical
    instance order.
    """
    V = max(max(u, v) for u, v, _ in edges) + 1
    mult = empty_mult(V)
    si = slot_index(V)
    for u, v, _ in edges:
        if u == v:
            raise ValueError("self loops are not allowed")
        mult[si[(min(u, v), max(u, v))]] += 1

    # canonical relabeling: find the group element mapping our labelling to
    # the canonical one, then map each canonical instance back to an edge.
    cm = canonical_mult(V, mult)
    p_inv = None
    for p in perm_group(V):
        if permute_mult(V, mult, p) == cm:
            p_inv = p
            break
    assert p_inv is not None
    canon_edges: list[tuple[int, int]] = []
    for (a, b), m in zip(slot_list(V), cm):
        canon_edges.extend([(a, b)] * m)
    assign: list[int] = []
    used = [False] * len(edges)
    for cp in canon_edges:
        for t, (u, v, ci) in enumerate(edges):
            if used[t]:
                continue
            a, b = p_inv[u], p_inv[v]
            if a > b:
                a, b = b, a
            if (a, b) == cp:
                assign.append(ci)
                used[t] = True
                break
        else:
            raise AssertionError("edge mapping failed")
    structure = make_structure(V, cm, canonicalize=False)
    return Network(structure=structure, assign=tuple(assign))


# ---------------------------------------------------------------------------
# named DUTs
# ---------------------------------------------------------------------------

@dataclass
class DUT:
    name: str
    group: str
    compset: ComponentSet
    network: Network

    def z_exact(self, f: np.ndarray) -> np.ndarray:
        return network_z(self.network, self.compset, f)

    def describe(self) -> str:
        return network_str(self.network, self.compset)


def make_duts() -> list[DUT]:
    out: list[DUT] = []

    def add(name, group, compset, edges):
        out.append(DUT(name, group, compset, network_from_edges(compset, edges)))

    # 1. two-component series (inductor with DCR -> single composite edge)
    cs = ComponentSet.make(n_R=[100.0], n_L=[(10e-3, 5.0)])
    add("dut1_series_RL", "series", cs, [(0, 2, 0), (2, 1, 1)])

    # 2. two-component parallel (multigraph: two edges on one node pair)
    cs = ComponentSet.make(n_R=[1e3], n_C=[100e-9])
    add("dut2_parallel_RC", "parallel", cs, [(0, 1, 0), (0, 1, 1)])

    # 3. three parallel edges (R || L || C tank, pure parallel multi-edge)
    cs = ComponentSet.make(n_R=[1e3], n_C=[10e-9], n_L=[(100e-6, 0.0)])
    add("dut3_tank_RLC", "parallel", cs, [(0, 1, 0), (0, 1, 1), (0, 1, 2)])

    # 4. inductor parasitic model (Rs + L) || Cp
    # sorted: C(50p)=0, L(10uH+d0.5)=1, R(1)=2
    cs = ComponentSet.make(n_R=[1.0], n_C=[50e-12], n_L=[(10e-6, 0.5)])
    add("dut4_lpar", "series_parallel", cs,
        [(0, 2, 2), (2, 1, 1), (0, 1, 0)])

    # 5. capacitor parasitic: R_esr + L_esl + C, all series
    cs = ComponentSet.make(n_R=[0.05], n_C=[10e-6], n_L=[(2e-9, 0.0)])
    add("dut5_cpar", "series", cs, [(0, 2, 0), (2, 3, 1), (3, 1, 2)])

    # 6. reactive Wheatstone bridge -- non-series-parallel, beyond Try1.
    #    (An all-resistor bridge would be unidentifiable: its Z(f) is flat,
    #    so only the effective resistance is observable -- DESIGN.md 2.4.)
    cs = ComponentSet.make(n_R=[100.0, 470.0, 1e3], n_C=[100e-9],
                           n_L=[(1e-3, 5.0)])
    # sorted components: C(100n)=0, L(1mH+d5)=1, R(100)=2, R(470)=3, R(1k)=4
    add("dut6_bridge", "bridge", cs,
        [(0, 2, 2), (0, 3, 1), (2, 1, 0), (3, 1, 3), (2, 3, 4)])

    # 7. double-L branch: C in series with (L1 || L2), parallel multi-edge
    cs = ComponentSet.make(n_C=[1e-6], n_L=[(1e-3, 5.0), (10e-3, 20.0)])
    add("dut7_double_L", "series_parallel", cs,
        [(0, 2, 0), (2, 1, 1), (2, 1, 2)])

    # 8. relaxation ladder R1 + (C1 || (R2 + C2))
    cs = ComponentSet.make(n_R=[100.0, 1e3], n_C=[100e-9, 1e-6])
    add("dut8_ladder", "series_parallel", cs,
        [(0, 2, 0), (2, 1, 2), (2, 3, 1), (3, 1, 3)])

    # 9. identical-value components (interchangeability)
    cs = ComponentSet.make(n_R=[10e3, 10e3], n_C=[100e-9])
    add("dut9_twin_R", "series_parallel", cs,
        [(0, 2, 0), (2, 1, 1), (0, 1, 2)])

    # 10. six-component mixed bridge-ish network with all three kinds
    cs = ComponentSet.make(n_R=[50.0, 200.0], n_C=[10e-9, 1e-6],
                           n_L=[(100e-6, 2.0), (1e-3, 10.0)])
    add("dut10_six_mixed", "mixed", cs,
        [(0, 2, 0), (2, 1, 2), (0, 3, 1), (3, 1, 3), (2, 3, 4), (0, 1, 5)])
    return out


# ---------------------------------------------------------------------------
# random DUTs
# ---------------------------------------------------------------------------

def _random_tree(nV: int, rng: np.random.Generator) -> list[tuple[int, int]]:
    """Uniform random labeled tree on nodes 0..nV-1 via a Prufer sequence."""
    import heapq
    if nV <= 2:
        return [(0, nV - 1)] if nV == 2 else []
    prufer = [int(v) for v in rng.integers(0, nV, size=nV - 2)]
    deg = [1] * nV
    for p in prufer:
        deg[p] += 1
    leaves = [i for i in range(nV) if deg[i] == 1]
    heapq.heapify(leaves)
    edges: list[tuple[int, int]] = []
    for p in prufer:
        leaf = heapq.heappop(leaves)
        edges.append((min(leaf, p), max(leaf, p)))
        deg[leaf] -= 1
        deg[p] -= 1
        if deg[p] == 1:
            heapq.heappush(leaves, p)
    a, b = heapq.heappop(leaves), heapq.heappop(leaves)
    edges.append((min(a, b), max(a, b)))
    return edges


def random_network(compset: ComponentSet, rng: np.random.Generator,
                   *, V: int | None = None,
                   max_tries: int = 1000) -> Network:
    """Random admissible network: uniform node count V in [2, E+1] (or the
    given V), a random connected multigraph on V nodes (retrying the edges,
    not V, so the node-count distribution stays uniform), then a random
    component assignment."""
    E = compset.n
    nV = V if V is not None else int(rng.integers(2, E + 2))
    nV = max(2, min(nV, E + 1))
    for _ in range(max_tries):
        pairs = _random_tree(nV, rng)
        # remaining edges at random node pairs (parallel edges allowed)
        all_pairs = [(i, j) for i in range(nV) for j in range(i + 1, nV)]
        for _ in range(E - (nV - 1)):
            pairs.append(all_pairs[int(rng.integers(0, len(all_pairs)))])
        mult = empty_mult(nV)
        si = slot_index(nV)
        for u, v in pairs:
            mult[si[(min(u, v), max(u, v))]] += 1
        if not is_connected(nV, mult):
            continue
        if has_dead_part(nV, mult):
            continue
        idx = list(range(E))
        rng.shuffle(idx)
        edges = [(u, v, idx[t]) for t, (u, v) in enumerate(pairs)]
        return network_from_edges(compset, edges)
    raise RuntimeError("random_network: no admissible graph found")


def measure(network: Network, compset: ComponentSet,
            f: np.ndarray | None = None, sigma_rel: float = 0.005,
            seed: int = 0) -> tuple[np.ndarray, np.ndarray]:
    """Noisy measurement z = z_exact + eps, eps complex Gaussian with
    sigma_k = sigma_rel * |z_k| (Try1 A3 model)."""
    f = default_frequencies() if f is None else f
    z0 = network_z(network, compset, f)
    rng = np.random.default_rng(seed)
    noise = sigma_rel * np.abs(z0) * (rng.standard_normal(len(f)) +
                                      1j * rng.standard_normal(len(f)))
    return np.asarray(f), z0 + noise


def network_str(network: Network, compset: ComponentSet) -> str:
    """Human-readable wiring string, e.g. '0-1:[R(1k)||C(100n)] 0-2:[L(...)]'."""
    comps = compset.components
    parts: list[str] = []
    t0 = 0
    for (i, j), m in zip(slot_list(network.structure.V), network.structure.mult):
        if m == 0:
            continue
        names = [comps[network.assign[t0 + q]].label() for q in range(m)]
        parts.append(f"{i}-{j}:" + "||".join(names))
        t0 += m
    return " ".join(parts)
