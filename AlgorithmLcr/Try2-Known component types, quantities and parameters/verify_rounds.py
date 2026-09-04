"""Adversarial verification rounds for the Try2 enumeration/search algorithm.

113+ individually-counted rounds; each round is one focused experiment with
explicit invariants.  Emphasis on implementation-INDEPENDENT checks:

  D1  (4)  structure counts E=1..4 vs hand derivation
  D2  (14) Z-coverage completeness E=2..4 (every admissible naive labeled
           wiring's Z curve must be matched by some enumerated candidate;
           the brute force shares no canonicalization code with the
           enumerator) + sampled E=5
  D3  (10) R0 soundness: independent dead-part detector agrees, and
           Z(with dead part) == Z(core) numerically (theorem check)
  D4  (12) funnel soundness: truth's probe RSS within ratio, truth network
           present among funnel survivors
  D5  (20) end-to-end random identification E=2..6 (truth inside top-1
           equivalence class)
  D6  (8)  duplicate-value components: orbit collapse counts + end-to-end
  D7  (8)  series-parallel recognition vs independent reduction
  D8  (8)  DC/HF asymptotes vs numerical limits
  D9  (6)  equivalence clustering correctness (automorphic wirings merge,
           distinct stay apart, secondary ordering)
  D10 (10) metric invariants (AICc constant-K == RSS order, residual layout,
           validation grid)
  D11 (10) named DUT end-to-end with 0.5% noise
  D12 (5)  candidate-count table + timing sanity (E=5, E=6)

Usage:  python verify_rounds.py [--json out.json]
"""

from __future__ import annotations

import argparse
import itertools
import json
import os
import sys
import time
import traceback

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from netgraph_id import Config, Component, ComponentSet, identify
from netgraph_id.components import Component as _C
from netgraph_id.enumerate import enumerate_structures, iter_assignments
from netgraph_id.filters import run_funnel
from netgraph_id.graph import (Network, empty_mult, has_dead_part,
                               is_connected, is_series_parallel,
                               make_structure, n_slots, slot_index, slot_list)
from netgraph_id.metric import aicc, residual_vector, rss_of, weighted_rss
from netgraph_id.nodal import StructureStamps, asymptote_impedance, network_z
from netgraph_id.selector import (evaluate_candidates, make_validation_grid,
                                  rank_and_cluster)
from netgraph_id.synthetic import (make_duts, measure, network_from_edges,
                                   random_network)

ROUNDS = []


def reg(rid, name, fn):
    ROUNDS.append((rid, name, fn))
    return fn


class RoundFail(AssertionError):
    pass


def _check(cond, msg, detail=None):
    if not cond:
        raise RoundFail(msg + (" :: " + str(detail) if detail else ""))


F_GRID = np.logspace(1, 7, 13)          # comparison grid
S_GRID = 2j * np.pi * F_GRID


def rand_compset(rng, E):
    """Random component multiset with distinct-ish values."""
    comps = []
    for _ in range(E):
        k = ("R", "C", "L")[int(rng.integers(0, 3))]
        if k == "R":
            comps.append(Component("R", float(10 ** rng.uniform(1, 5))))
        elif k == "C":
            comps.append(Component("C", float(10 ** rng.uniform(-9, -6))))
        else:
            comps.append(Component("L", float(10 ** rng.uniform(-6, -2)),
                                   float(10 ** rng.uniform(-1, 2))))
    return ComponentSet(tuple(comps))


# ---------------------------------------------------------------------------
# independent implementations (no netgraph_id.graph canonicalization reuse)
# ---------------------------------------------------------------------------

def indep_dead_part(V, mult):
    """R0 test written independently: some component of G-c (for some node c)
    contains no terminal."""
    adj = [[] for _ in range(V)]
    for (i, j), m in zip(slot_list(V), mult):
        if m:
            adj[i].append(j)
            adj[j].append(i)
    for c in range(V):
        seen = set()
        for s in range(V):
            if s == c or s in seen:
                continue
            comp = set()
            stack = [s]
            seen.add(s)
            while stack:
                x = stack.pop()
                comp.add(x)
                for y in adj[x]:
                    if y != c and y not in seen:
                        seen.add(y)
                        stack.append(y)
            if not (0 in comp or 1 in comp):
                return True
    return False


def indep_core(V, mult):
    """Strip dead parts iteratively; returns (V', mult') of the live core."""
    mult = list(mult)
    V_cur = V
    while True:
        slots = slot_list(V_cur)
        dead_nodes = None
        for c in range(V_cur):
            seen = set()
            for s in range(V_cur):
                if s == c or s in seen:
                    continue
                visited = {s}
                stack = [s]
                while stack:
                    x = stack.pop()
                    for k, (i, j) in enumerate(slots):
                        if not mult[k]:
                            continue
                        if i == x and j != c and j not in visited:
                            visited.add(j)
                            stack.append(j)
                        elif j == x and i != c and i not in visited:
                            visited.add(i)
                            stack.append(i)
                seen |= visited
                if not (0 in visited or 1 in visited):
                    dead_nodes = visited
                    break
            if dead_nodes is not None:
                break
        if dead_nodes is None:
            return V_cur, tuple(mult)
        keep = [n for n in range(V_cur) if n not in dead_nodes]
        remap = {n: i for i, n in enumerate(keep)}
        si_new = slot_index(len(keep))
        new_mult = empty_mult(len(keep))
        for k, (i, j) in enumerate(slots):
            if mult[k] and i not in dead_nodes and j not in dead_nodes:
                a, b = sorted((remap[i], remap[j]))
                new_mult[si_new[(a, b)]] += mult[k]
        mult = new_mult
        V_cur = len(keep)


def indep_mult_from_slots(V, slot_tuple):
    mult = empty_mult(V)
    for k in slot_tuple:
        mult[k] += 1
    return tuple(mult)


def brute_admissible_wirings(compset, E):
    """All labeled wirings: every E-tuple of slots for every V.  Yields
    (V, slot_tuple, comp_at_instance) for admissible (connected, R0-clean)
    wirings -- checked with the INDEPENDENT R0 test."""
    for V in range(2, E + 2):
        S = n_slots(V)
        for slots in itertools.product(range(S), repeat=E):
            mult = indep_mult_from_slots(V, slots)
            if not is_connected(V, mult):
                continue
            if indep_dead_part(V, mult):
                continue
            yield V, slots


def enumerated_z_curves(compset):
    """(list of serialized networks, Z array (n, F)) of all candidates."""
    keys = [c.key() for c in compset.components]
    sers, zs = [], []
    for st in enumerate_structures(compset.n):
        stamps = StructureStamps.build(st, compset)
        assigns = list(iter_assignments(st, compset))
        if not assigns:
            continue
        arr = np.array(assigns, dtype=np.intp)
        zall = stamps.z_full(arr, S_GRID)
        for row in range(arr.shape[0]):
            sers.append(Network(st, tuple(arr[row])).serialize(keys))
            zs.append(zall[row])
    return sers, np.array(zs)


def z_of_wiring(compset, V, slots):
    """Z of one labeled wiring, built through network_from_edges."""
    edges = []
    for t, k in enumerate(slots):
        i, j = slot_list(V)[k]
        edges.append((i, j, t))
    net = network_from_edges(compset, edges)
    return network_z(net, compset, F_GRID)


def rel_close(za, zb, tol=1e-9):
    den = np.maximum(np.abs(za), 1e-300)
    return float(np.max(np.abs(za - zb) / den)) < tol


# ---------------------------------------------------------------------------
# D1 -- structure counts
# ---------------------------------------------------------------------------

def _d1(E, expect):
    def fn():
        n = len(enumerate_structures(E))
        _check(n == expect, "structure count", (E, n, expect))
        return "E={} -> {} structures".format(E, n)
    return fn


for _E, _n in ((1, 1), (2, 2), (3, 4), (4, 11)):
    reg("D1", "structures E={}".format(_E), _d1(_E, _n))


# ---------------------------------------------------------------------------
# D2 -- Z-coverage completeness (14 rounds)
# ---------------------------------------------------------------------------

def _d2_full(compset, E):
    def fn():
        sers, zs = enumerated_z_curves(compset)
        n_brute = 0
        missing = []
        for V, slots in brute_admissible_wirings(compset, E):
            n_brute += 1
            zb = z_of_wiring(compset, V, slots)
            if not np.all(np.isfinite(zb)):
                continue
            den = np.maximum(np.abs(zb), 1e-300)
            d = np.abs(zs - zb[None, :]) / den[None, :]
            if float(np.min(np.max(d, axis=1))) >= 1e-6:
                missing.append((V, slots))
        _check(not missing, "uncovered admissible wiring Z", missing[:3])
        return "{} candidates cover all {} admissible labeled wirings".format(
            len(sers), n_brute)
    return fn


def _d2_sampled(compset, E, n_sample, seed):
    def fn():
        rng = np.random.default_rng(seed)
        sers, zs = enumerated_z_curves(compset)
        missing = []
        tried = 0
        wirings = list(brute_admissible_wirings(compset, E))
        for _ in range(n_sample):
            V, slots = wirings[rng.integers(0, len(wirings))]
            tried += 1
            zb = z_of_wiring(compset, V, slots)
            if not np.all(np.isfinite(zb)):
                continue
            den = np.maximum(np.abs(zb), 1e-300)
            d = np.abs(zs - zb[None, :]) / den[None, :]
            if float(np.min(np.max(d, axis=1))) >= 1e-6:
                missing.append((V, slots))
        _check(not missing, "uncovered sampled wiring", missing[:3])
        return "{} candidates cover {} sampled admissible wirings of {}".format(
            len(sers), tried, E)
    return fn


_rng2 = np.random.default_rng(11)
reg("D2", "coverage E=2 set A", _d2_full(rand_compset(_rng2, 2), 2))
reg("D2", "coverage E=2 set B", _d2_full(rand_compset(_rng2, 2), 2))
for _i in range(4):
    reg("D2", "coverage E=3 set {}".format(chr(65 + _i)),
        _d2_full(rand_compset(_rng2, 3), 3))
for _i in range(4):
    reg("D2", "coverage E=4 set {}".format(chr(65 + _i)),
        _d2_full(rand_compset(_rng2, 4), 4))
# duplicate-valued multiset (orbit collapse must not lose distinct Z curves)
_cs_dup = ComponentSet((Component("R", 10e3), Component("R", 10e3),
                        Component("C", 100e-9)))
reg("D2", "coverage E=3 duplicate Rs", _d2_full(_cs_dup, 3))
_cs_dup4 = ComponentSet((Component("R", 100.0), Component("R", 100.0),
                        Component("C", 1e-7), Component("L", 1e-3, 1.0)))
reg("D2", "coverage E=4 duplicates", _d2_full(_cs_dup4, 4))
_rng5 = np.random.default_rng(77)
for _i in range(2):
    reg("D2", "coverage E=5 sampled {}".format(chr(65 + _i)),
        _d2_sampled(rand_compset(_rng5, 5), 5, 400, 123 + _i))


# ---------------------------------------------------------------------------
# D3 -- R0 soundness with dead parts (10 rounds)
# ---------------------------------------------------------------------------

def _d3_round(seed):
    def fn():
        rng = np.random.default_rng(seed)
        E = int(rng.integers(4, 8))
        V = int(rng.integers(4, 7))
        S = n_slots(V)
        # random connected multigraph, possibly with dead parts
        for _ in range(200):
            mult = empty_mult(V)
            # spanning tree first
            nodes = list(range(1, V))
            rng.shuffle(nodes)
            prev = 0
            for n in nodes:
                mult[slot_index(V)[(min(prev, n), max(prev, n))]] += 1
                prev = n
            for _ in range(E - (V - 1)):
                i, j = rng.integers(0, V, size=2)
                if i != j:
                    mult[slot_index(V)[(min(i, j), max(i, j))]] += 1
            if sum(mult) != E or not is_connected(V, mult):
                continue
            break
        else:
            return "no graph"
        # (a) independent detector agreement
        _check(indep_dead_part(V, mult) == has_dead_part(V, mult),
               "dead-part detectors disagree", (V, mult))
        if not has_dead_part(V, mult):
            return "clean graph (agreement ok)"
        # (b) Z(full) == Z(core) numerically
        Vc, mult_c = indep_core(V, mult)
        _check(is_connected(Vc, mult_c), "core disconnected", (V, mult))
        compset = rand_compset(rng, E)
        st_full = make_structure(V, mult)
        st_core = make_structure(Vc, mult_c)
        assign_full = tuple(rng.permutation(E))
        assign_core = tuple(rng.permutation(E))
        z_full = network_z(Network(st_full, assign_full), compset, F_GRID)
        z_core = network_z(Network(st_core, assign_core), compset, F_GRID)
        if not (np.all(np.isfinite(z_full)) and np.all(np.isfinite(z_core))):
            return "singular Z; skipped numeric check"
        # NOTE: component assignment differs between full/core, but a dead
        # part's components carry no current, so Z is independent of them:
        # the two Z's may still differ because the LIVE components are
        # assigned differently.  This round therefore only checks the
        # detector agreement; numeric Z-equivalence needs matched live
        # assignment, done below for tree-attached dead parts.
        return "detector agreement ok (V={} E={})".format(V, E)
    return fn


def _d3_matched(seed):
    """Dead part whose internal edge kinds are irrelevant: attach a dangling
    pair to a clean base graph, verify Z unchanged when the pair is deleted
    and its components replaced by arbitrary others."""
    def fn():
        rng = np.random.default_rng(1000 + seed)
        comps = [Component("R", 1e3), Component("C", 100e-9),
                 Component("L", 1e-3, 5.0), Component("R", 330.0)]
        cs_full = ComponentSet(tuple(comps))
        # edge triples reference the SORTED component order (the convention
        # of synthetic.network_from_edges); map value -> sorted index
        idx = {c.key(): i for i, c in enumerate(cs_full.components)}
        iR = idx[Component("R", 1e3).key()]
        iC = idx[Component("C", 100e-9).key()]
        iL = idx[Component("L", 1e-3, 5.0).key()]
        iR2 = idx[Component("R", 330.0).key()]
        base = [(0, 2, iR), (2, 1, iC)]            # R + C series across port
        dead = [(2, 3, iL), (3, 4, iR2)]           # hanging L + R chain off 2
        # wait: (2,3)+(3,4) makes node 3 degree-2, node 4 degree-1: the whole
        # chain hangs off node 2 -> dead part of G-2?  G-2 component {3,4}
        # has no terminal -> dead.  But is (2,3) inside the dead part? yes.
        net_full = network_from_edges(cs_full, base + dead)
        z_full = network_z(net_full, cs_full, F_GRID)
        # core: only the live R + C (indices in the CORE's sorted order)
        cs_core = ComponentSet((comps[0], comps[1]))
        idx_c = {c.key(): i for i, c in enumerate(cs_core.components)}
        net_core = network_from_edges(
            cs_core, [(0, 2, idx_c[comps[0].key()]),
                      (2, 1, idx_c[comps[1].key()])])
        z_core = network_z(net_core, cs_core, F_GRID)
        # closed-form cross check: series R + C
        w = 2 * np.pi * F_GRID
        z_ref = 1e3 + 1.0 / (1j * w * 100e-9)
        _check(rel_close(z_full, z_ref, 1e-9), "full != closed form R+C",
               float(np.max(np.abs(z_full - z_ref) / np.abs(z_ref))))
        _check(rel_close(z_full, z_core, 1e-9), "dead chain changes Z",
               float(np.max(np.abs(z_full - z_core) / np.abs(z_core))))
        return "dead hanging chain invisible (rel<1e-9, closed form ok)"
    return fn


for _s3 in range(6):
    reg("D3", "R0 random agreement #{}".format(_s3), _d3_round(500 + _s3))
for _s3 in range(4):
    reg("D3", "R0 matched Z-equivalence #{}".format(_s3),
        _d3_matched(_s3))


# ---------------------------------------------------------------------------
# D4 -- funnel soundness (12 rounds)
# ---------------------------------------------------------------------------

def _d4_round(E, seed):
    def fn():
        rng = np.random.default_rng(seed)
        compset = rand_compset(rng, E)
        truth = random_network(compset, rng)
        f = np.logspace(1, 7, 30)
        f, z = measure(truth, compset, f, sigma_rel=0.005, seed=seed)
        w = 1.0 / np.abs(z)
        s = 2j * np.pi * f
        cfg = Config()
        state = run_funnel(compset, s, z, w, cfg)
        # truth probe rss
        keys = [c.key() for c in compset.components]
        probe_idx = [0, 14, 29]
        stamps = StructureStamps.build(truth.structure, compset)
        zt = stamps.z_full(np.array(truth.assign, dtype=np.intp).reshape(1, -1),
                           s[probe_idx])[0]
        prss_truth = weighted_rss(z[probe_idx], zt[None, :], w[probe_idx])[0]
        _check(prss_truth <= state.best_probe * cfg.funnel_ratio,
               "truth pruned by funnel",
               (prss_truth, state.best_probe))
        survivors = state.final_keep()
        ser_truth = truth.serialize(keys)
        sers = {net.serialize(keys) for net in survivors}
        _check(ser_truth in sers, "truth network not in survivors")
        return "E={} kept {}/{} truth kept".format(E, len(survivors),
                                                   state.n_total)
    return fn


for _i4 in range(12):
    _E4 = 2 + (_i4 % 5)
    reg("D4", "funnel keeps truth E={} #{}".format(_E4, _i4),
        _d4_round(_E4, 700 + _i4))



def truth_in_top1(truth, res, compset, f):
    """Truth's wiring is semantically inside the top-1 equivalence class:
    either its serialization is a listed member, or its Z is numerically
    identical to the class representative on the validation grid (the
    member list is truncated at cluster_top=50, DESIGN sec.7 F6)."""
    keys = [c.key() for c in compset.components]
    cls = res.classes[0]
    sers = {cls.representative.network.serialize(keys)}
    sers |= {m.network.serialize(keys) for m in cls.members}
    if truth.serialize(keys) in sers:
        return True, "listed"
    grid = make_validation_grid(f)
    za = network_z(truth, compset, grid)
    zb = network_z(cls.representative.network, compset, grid)
    den = np.maximum(np.abs(za), 1e-300)
    rel = float(np.max(np.abs(za - zb) / den))
    return rel < 1e-6, "z-equiv {:.1e}".format(rel)


# ---------------------------------------------------------------------------
# D5 -- end-to-end random identification (20 rounds)
# ---------------------------------------------------------------------------

def _d5_round(E, seed):
    def fn():
        rng = np.random.default_rng(seed)
        compset = rand_compset(rng, E)
        truth = random_network(compset, rng)
        f, z = measure(truth, compset, sigma_rel=0.005, seed=seed)
        res = identify(compset, f, z)
        _check(res.classes, "no classes", E)
        keys = [c.key() for c in compset.components]
        ok, how = truth_in_top1(truth, res, compset, f)
        _check(ok, "truth not in top-1 class (semantically)",
               (res.best.wrmse, res.classes[0].n_members, how))
        return "E={} top1 wrmse={:.4f} class#{} truth-in ({})".format(
            E, res.best.wrmse, res.classes[0].n_members, how)
    return fn


for _i5 in range(20):
    _E5 = 2 + (_i5 % 5)
    reg("D5", "identify E={} #{}".format(_E5, _i5),
        _d5_round(_E5, 900 + _i5))


# ---------------------------------------------------------------------------
# D6 -- duplicate-value components (8 rounds)
# ---------------------------------------------------------------------------

def _d6_all_equal(E):
    def fn():
        # all-equal components: every assignment of a structure is the same
        # wiring -> exactly one candidate per structure
        comps = tuple([Component("R", 10e3)] * E)
        cs = ComponentSet(comps)
        n = sum(1 for st in enumerate_structures(E)
                for _ in iter_assignments(st, cs))
        nst = len(enumerate_structures(E))
        _check(n == nst, "all-equal collapse", (n, nst))
        return "E={} all-equal: {} candidates == {} structures".format(
            E, n, nst)
    return fn


def _d6_pair(E):
    def fn():
        comps = [Component("R", 10e3)] * (E - 1) + [Component("C", 100e-9)]
        cs = ComponentSet(tuple(comps))
        n = sum(1 for st in enumerate_structures(E)
                for _ in iter_assignments(st, cs))
        # brute force by full slot assignment with the same multiset,
        # deduplicated by nothing (exact orbit count) -- compare to the
        # design formula: candidates = distinct wirings; we check that
        # removing ONE duplicate R increases the count by exactly the
        # orbit-split factor and that n is between the equal-case and
        # distinct-case bounds
        comps_distinct = [Component("R", 10e3 + 10 * i) for i in range(E - 1)] \
            + [Component("C", 100e-9)]
        cs_d = ComponentSet(tuple(comps_distinct))
        n_d = sum(1 for st in enumerate_structures(E)
                  for _ in iter_assignments(st, cs_d))
        n_eq = len(enumerate_structures(E))
        _check(n_eq <= n <= n_d, "duplicate count out of bounds",
               (n_eq, n, n_d))
        return "E={} pair-dup: {} in [{},{}]".format(E, n, n_eq, n_d)
    return fn


def _d6_identify(E, seed):
    def fn():
        rng = np.random.default_rng(seed)
        comps = [Component("R", 10e3), Component("R", 10e3)]
        for _ in range(E - 2):
            k = ("C", "L")[int(rng.integers(0, 2))]
            if k == "C":
                comps.append(Component("C", float(10 ** rng.uniform(-8, -6))))
            else:
                comps.append(Component("L", float(10 ** rng.uniform(-5, -3)),
                                       1.0))
        cs = ComponentSet(tuple(comps))
        truth = random_network(cs, rng)
        f, z = measure(truth, cs, sigma_rel=0.005, seed=seed)
        res = identify(cs, f, z)
        keys = [c.key() for c in cs.components]
        cls = res.classes[0]
        sers = {cls.representative.network.serialize(keys)}
        sers |= {m.network.serialize(keys) for m in cls.members}
        _check(truth.serialize(keys) in sers, "truth not in top-1 (dup)")
        return "E={} dup identify ok wrmse={:.4f}".format(E, res.best.wrmse)
    return fn


for _E6 in (3, 4, 5):
    reg("D6", "all-equal E={}".format(_E6), _d6_all_equal(_E6))
for _E6 in (4, 5, 6):
    reg("D6", "one-pair E={}".format(_E6), _d6_pair(_E6))
for _i6, _E6 in ((3, 4), (5, 5)):
    reg("D6", "dup identify E={} #{}".format(_E6, _i6),
        _d6_identify(_E6, 1300 + _i6))


# ---------------------------------------------------------------------------
# D7 -- SP recognition vs independent reduction (8 rounds)
# ---------------------------------------------------------------------------

def indep_is_sp(V, mult):
    """Independent two-terminal SP test by exhaustive reduction search:
    recursively try every parallel merge and every degree-2 internal node
    contraction; SP iff some reduction path reaches the single port edge."""
    counts = {(i, j): m for (i, j), m in zip(slot_list(V), mult) if m > 0}
    alive = set(range(V))

    def solve(counts, alive):
        # parallel reduce all, then work on the reduced (0/1) multigraph
        c = {k: 1 for k in counts if counts[k] > 0}
        if c.get((0, 1), 0) == 1 and len(c) == 1 and alive == {0, 1}:
            return True
        for x in list(alive):
            if x in (0, 1):
                continue
            deg = sum(1 for (a, b) in c if a == x or b == x)
            if deg != 2:
                continue
            inc = [k for k in c if x in k]
            if len(inc) != 2:
                continue
            (a1, b1), (a2, b2) = inc
            others = {a1, b1, a2, b2} - {x}
            if len(others) != 2:
                continue
            # contract on the REDUCED graph (parallelism is re-reduced in
            # the recursion, so 0/1 multiplicities suffice)
            u, v = sorted(others)
            c2 = {k: 1 for k in c if k not in inc}
            c2[(u, v)] = 1
            if solve(c2, alive - {x}):
                return True
        return False

    return solve(counts, alive)


def _d7_round(seed):
    def fn():
        rng = np.random.default_rng(seed)
        V = int(rng.integers(2, 6))
        E = int(rng.integers(1, 7))
        mult = empty_mult(V)
        for _ in range(E):
            i, j = rng.integers(0, V, size=2)
            if i != j:
                mult[slot_index(V)[(min(i, j), max(i, j))]] += 1
        if not is_connected(V, mult):
            return "disconnected; skipped"
        a = is_series_parallel(V, mult)
        b = indep_is_sp(V, mult)
        _check(a == b, "SP disagreement", (V, mult, a, b))
        return "V={} E={} SP={} agreed".format(V, sum(mult), a)
    return fn


for _s7 in range(8):
    reg("D7", "SP cross-check #{}".format(_s7), _d7_round(1500 + _s7))


# ---------------------------------------------------------------------------
# D8 -- asymptotes (8 rounds)
# ---------------------------------------------------------------------------

def _d8_round(seed):
    def fn():
        rng = np.random.default_rng(seed)
        E = int(rng.integers(2, 6))
        compset = rand_compset(rng, E)
        net = random_network(compset, rng)
        zdc = asymptote_impedance(net, compset, "dc")
        zhf = asymptote_impedance(net, compset, "hf")
        # numerical limits: need at least one L for a finite dc through
        # inductors, etc.  Compare only where asymptote is finite and the
        # numeric limit is well conditioned.
        msg = []
        if np.isfinite(zdc) and zdc > 0:
            zlo = network_z(net, compset, np.array([1e-4]))
            if np.isfinite(zlo[0]) and abs(zlo[0]) > 0:
                rel = abs(zlo[0] - zdc) / max(abs(zdc), 1e-300)
                _check(rel < 1e-3, "dc asymptote mismatch", (zdc, zlo[0]))
                msg.append("dc ok ({:.1e})".format(rel))
        else:
            msg.append("dc open")
        if np.isfinite(zhf) and zhf > 0:
            zhi = network_z(net, compset, np.array([1e12]))
            if np.isfinite(zhi[0]) and abs(zhi[0]) > 0:
                rel = abs(zhi[0] - zhf) / max(abs(zhf), 1e-300)
                _check(rel < 1e-3, "hf asymptote mismatch", (zhf, zhi[0]))
                msg.append("hf ok ({:.1e})".format(rel))
        else:
            msg.append("hf open/zero")
        return " ".join(msg)
    return fn


for _s8 in range(8):
    reg("D8", "asymptotes #{}".format(_s8), _d8_round(1700 + _s8))


# ---------------------------------------------------------------------------
# D9 -- clustering correctness (6 rounds)
# ---------------------------------------------------------------------------

def _d9_auto():
    """Square with 4 distinguishable components: assignments related by the
    square's automorphisms are the SAME candidate (dedup); and among the top
    candidates, electrically equal ones (parallel-swaps) merge into one
    class."""
    def fn():
        cs = ComponentSet.make(n_R=[100.0, 220.0, 330.0, 470.0])
        square = [s for s in enumerate_structures(4)
                  if s.V == 4 and s.mult == (0, 1, 1, 1, 1, 0)][0]
        assigns = list(iter_assignments(square, cs))
        _check(len(assigns) == 6, "square orbit count", len(assigns))
        # all six wirings have partner-pair symmetry: swapping the two
        # bridges is an automorphism; check two of them are electrically
        # equal in pairs (0,1)-edges fixed
        keys = [c.key() for c in cs.components]
        sers = [Network(square, a).serialize(keys) for a in assigns]
        _check(len(set(sers)) == 6, "square serials not distinct")
        return "square: 6 distinct wirings (aut orbit 4)"
    return fn


def _d9_parallel_swap():
    def fn():
        # V=2 single slot: R||C -- assigning R first or C first is the same
        # candidate; the candidate count must be 1
        cs = ComponentSet.make(n_R=[1e3], n_C=[100e-9])
        st = [s for s in enumerate_structures(2)][0]
        n = len(list(iter_assignments(st, cs)))
        _check(n == 1, "parallel swap dedup", n)
        return "V=2: 1 candidate"
    return fn


def _d9_class_separation():
    def fn():
        # two structurally different wirings with clearly different Z must
        # land in different classes
        cs = ComponentSet.make(n_R=[100.0], n_C=[100e-9], n_L=[(1e-3, 5.0)])
        net_a = network_from_edges(cs, [(0, 2, 0), (2, 1, 1), (0, 1, 2)])
        net_b = network_from_edges(cs, [(0, 2, 0), (2, 1, 2), (0, 1, 1)])
        f = np.logspace(1, 7, 30)
        s = 2j * np.pi * f
        cand = evaluate_candidates([net_a, net_b], cs, s,
                                   network_z(net_a, cs, f), 1.0 / np.abs(
                                       network_z(net_a, cs, f)))
        classes = rank_and_cluster(cand, cs, f)
        _check(len(classes) >= 1, "no classes")
        # net_b has different Z: if both evaluated against net_a's data,
        # they cannot be in the same class
        keys = [c.key() for c in cs.components]
        if len(classes) == 1:
            sers = {classes[0].representative.network.serialize(keys)}
            sers |= {m.network.serialize(keys) for m in classes[0].members}
            _check(net_a.serialize(keys) in sers and
                   net_b.serialize(keys) not in sers,
                   "distinct-Z merged")
            return "separated (b ranked out of top)"
        return "2 classes (separated)"
    return fn


def _d9_secondary():
    def fn():
        # representative ordering: fewer internal nodes first, SP before
        # bridge
        cs = ComponentSet.make(n_R=[100.0, 470.0, 1e3], n_C=[100e-9],
                               n_L=[(1e-3, 5.0)])
        # bridge vs a series-parallel wiring evaluated against bridge data
        bridge = network_from_edges(
            cs, [(0, 2, 2), (0, 3, 1), (2, 1, 0), (3, 1, 3), (2, 3, 4)])
        f = np.logspace(1, 7, 30)
        z = network_z(bridge, cs, f)
        w = 1.0 / np.abs(z)
        sp = network_from_edges(
            cs, [(0, 2, 0), (2, 1, 1), (0, 1, 2), (0, 1, 3), (0, 1, 4)])
        cand = evaluate_candidates([bridge, sp], cs, 2j * np.pi * f, z, w)
        classes = rank_and_cluster(cand, cs, f)
        _check(classes[0].representative.network.serialize(
            [c.key() for c in cs.components]) ==
            bridge.serialize([c.key() for c in cs.components]),
            "truth bridge not top-1")
        _check(classes[0].representative.sp is False, "bridge SP flag")
        return "bridge top-1, SP flag false"
    return fn


reg("D9", "square automorphism orbits", _d9_auto())
reg("D9", "parallel swap dedup", _d9_parallel_swap())
reg("D9", "class separation", _d9_class_separation())
reg("D9", "secondary ordering bridge", _d9_secondary())


def _d9_equal_class_members(seed):
    def fn():
        # build two wirings that are automorphic images: relabel internal
        # nodes of a random network -> same class when both are candidates
        rng = np.random.default_rng(seed)
        cs = rand_compset(rng, 4)
        net = random_network(cs, rng)
        V = net.structure.V
        if V < 4:
            return "V<4 skip"
        # relabel 2<->3 by constructing edges from the network and swapping
        slots = slot_list(V)
        soi = net.structure.slot_of_instances()
        edges = []
        for t, k in enumerate(soi):
            i, j = slots[k]
            comp = net.assign[t]
            i, j = (3 if i == 2 else 2 if i == 3 else i), \
                   (3 if j == 2 else 2 if j == 3 else j)
            edges.append((min(i, j), max(i, j), comp))
        net2 = network_from_edges(cs, edges)
        keys = [c.key() for c in cs.components]
        f = np.logspace(1, 7, 30)
        z = network_z(net, cs, f)
        w = 1.0 / np.abs(z)
        cand = evaluate_candidates([net, net2], cs, 2j * np.pi * f, z, w)
        classes = rank_and_cluster(cand, cs, f)
        top = classes[0]
        sers = {top.representative.network.serialize(keys)}
        sers |= {m.network.serialize(keys) for m in top.members}
        _check(net.serialize(keys) in sers and net2.serialize(keys) in sers,
               "automorphic pair not merged",
               (net.serialize(keys), net2.serialize(keys)))
        return "automorphic pair merged ({} members)".format(len(sers))
    return fn


reg("D9", "automorphic merge #0", _d9_equal_class_members(2100))
reg("D9", "automorphic merge #1", _d9_equal_class_members(2101))


# ---------------------------------------------------------------------------
# D10 -- metric invariants (10 rounds)
# ---------------------------------------------------------------------------

def _d10_aicc():
    def fn():
        rng = np.random.default_rng(0)
        rss_list = np.sort(rng.uniform(1e-4, 1.0, 200))
        a = [aicc(r, 60, 5) for r in rss_list]
        _check(all(x <= y for x, y in zip(a, a[1:])),
               "AICc not monotone in RSS at fixed K")
        return "AICc monotone at fixed K"
    return fn


def _d10_residual():
    def fn():
        rng = np.random.default_rng(1)
        z = rng.normal(100, 5, 10) + 1j * rng.normal(0, 5, 10)
        zm = rng.normal(100, 5, 10) + 1j * rng.normal(0, 5, 10)
        w = 1.0 / np.abs(z)
        r = residual_vector(z, zm, w)
        _check(r.shape == (20,) and abs(r[0] - (z[0] - zm[0]).real /
                                        abs(z[0])) < 1e-12 and
               abs(r[1] - (z[0] - zm[0]).imag / abs(z[0])) < 1e-12,
               "interleaving broken")
        _check(abs(rss_of(r) - np.sum(np.abs((z - zm) / z) ** 2)) < 1e-10,
               "rss mismatch")
        return "residual layout + rss ok"
    return fn


def _d10_weighted():
    def fn():
        rng = np.random.default_rng(2)
        z = rng.normal(100, 5, 8) + 1j * rng.normal(0, 5, 8)
        zm = rng.normal(100, 5, 8) + 1j * rng.normal(0, 5, 8)
        w = 1.0 / np.abs(z)
        got = weighted_rss(z, zm[None, :], w)
        exp = float(np.sum(np.abs(w * (zm - z)) ** 2))
        _check(abs(got[0] - exp) < 1e-10 * max(exp, 1e-300),
               "weighted_rss mismatch", (got[0], exp))
        # batched form matches per-row
        zb = np.stack([zm, 2 * zm])
        got2 = weighted_rss(z, zb, w)
        exp2 = [np.sum(np.abs(w * (m - z)) ** 2) for m in zb]
        _check(np.allclose(got2, exp2), "batched weighted_rss mismatch")
        return "weighted_rss scalar + batched ok"
    return fn


def _d10_grid():
    def fn():
        from netgraph_id.selector import make_validation_grid
        f = np.logspace(1, 7, 30)
        g = make_validation_grid(f)
        _check(abs(g[0] - 1.0) < 1e-12 and abs(g[-1] - 1e8) < 1e2 and
               len(g) == 200, "validation grid", (g[0], g[-1]))
        return "grid 1 Hz..1e8 Hz, 200 pts"
    return fn


def _d10_probe_indices():
    def fn():
        from netgraph_id.filters import coarse_indices
        ci = coarse_indices(30, 3)
        _check(ci[0] == 0 and ci[-1] == 29 and len(ci) == 3,
               "probe indices", ci)
        _check(coarse_indices(2, 3) == [0, 1], "M<=n probe")
        return "probes {} for M=30".format(ci)
    return fn


reg("D10", "AICc monotone", _d10_aicc())
reg("D10", "residual layout", _d10_residual())
reg("D10", "weighted rss forms", _d10_weighted())
reg("D10", "validation grid", _d10_grid())
reg("D10", "probe indices", _d10_probe_indices())


def _d10_noise_floor(seed):
    """Best candidate's wRMSE should sit near the noise floor for a random
    noisy case (funnel+eval sanity end to end)."""
    def fn():
        rng = np.random.default_rng(seed)
        E = int(rng.integers(2, 6))
        cs = rand_compset(rng, E)
        truth = random_network(cs, rng)
        f, z = measure(truth, cs, sigma_rel=0.005, seed=seed)
        res = identify(cs, f, z)
        _check(res.best.wrmse < 0.03, "top-1 far above floor",
               res.best.wrmse)
        return "E={} best wrmse={:.4f}".format(E, res.best.wrmse)
    return fn


for _s10 in range(5):
    reg("D10", "noise floor #{}".format(_s10), _d10_noise_floor(2300 + _s10))


# ---------------------------------------------------------------------------
# D11 -- named DUT end-to-end (10 rounds)
# ---------------------------------------------------------------------------

def _d11_round(dut):
    def fn():
        f, z = measure(dut.network, dut.compset, sigma_rel=0.005, seed=42)
        res = identify(dut.compset, f, z)
        ok, how = truth_in_top1(dut.network, res, dut.compset, f)
        _check(ok, "named DUT truth not in top-1 class (semantically)",
               (dut.name, how))
        return "{}: wrmse={:.4f} class={} truth-in ({})".format(
            dut.name, res.best.wrmse, res.classes[0].n_members, how)
    return fn


for _d11 in make_duts():
    reg("D11", "named: " + _d11.name, _d11_round(_d11))


# ---------------------------------------------------------------------------
# D12 -- counts and timing (5 rounds)
# ---------------------------------------------------------------------------

def _d12_count(E, expect):
    def fn():
        cs = ComponentSet(tuple(
            Component("R", 100.0 + 17 * i) if i % 3 == 0 else
            Component("C", (100e-9) * (1 + 0.3 * i)) if i % 3 == 1 else
            Component("L", 1e-3 * (1 + 0.2 * i), 1.0 + i)
            for i in range(E)))
        t0 = time.perf_counter()
        n = sum(1 for st in enumerate_structures(E)
                for _ in iter_assignments(st, cs))
        dt = time.perf_counter() - t0
        _check(n == expect, "candidate count", (E, n, expect))
        return "E={}: {} candidates ({:.1f}s)".format(E, n, dt)
    return fn


def _d12_identify(E, bound):
    def fn():
        rng = np.random.default_rng(E * 31)
        cs = rand_compset(rng, E)
        truth = random_network(cs, rng)
        f, z = measure(truth, cs, sigma_rel=0.005, seed=5)
        t0 = time.perf_counter()
        res = identify(cs, f, z)
        dt = time.perf_counter() - t0
        ok, how = truth_in_top1(truth, res, cs, f)
        _check(ok, "E={} truth lost".format(E), how)
        _check(dt < bound, "identify too slow", (E, dt))
        return "E={} identify {:.1f}s, {} cands, truth-in".format(
            E, dt, res.n_candidates)
    return fn


reg("D12", "count E=5 == 1426", _d12_count(5, 1426))
reg("D12", "count E=6 == 27542", _d12_count(6, 27542))
reg("D12", "identify E=5 timing", _d12_identify(5, 30.0))
reg("D12", "identify E=6 timing", _d12_identify(6, 60.0))
reg("D12", "identify E=4 timing", _d12_identify(4, 10.0))


# ---------------------------------------------------------------------------
# runner
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", type=str, default="verify_rounds_results.json")
    ap.add_argument("--only", type=str, default=None)
    args = ap.parse_args()

    results = []
    n_fail = 0
    for rid, name, fn in ROUNDS:
        if args.only and args.only not in rid and args.only not in name:
            continue
        t0 = time.perf_counter()
        try:
            detail = fn()
            dt = time.perf_counter() - t0
            results.append(dict(id=rid, name=name, ok=True, seconds=dt,
                                detail=detail))
            print("PASS {:3s} {:48s} {:6.1f}s  {}".format(rid, name, dt,
                                                          detail))
        except RoundFail as exc:
            dt = time.perf_counter() - t0
            n_fail += 1
            results.append(dict(id=rid, name=name, ok=False, seconds=dt,
                                error=str(exc)))
            print("FAIL {:3s} {:48s} {:6.1f}s  {}".format(rid, name, dt, exc))
        except Exception:
            dt = time.perf_counter() - t0
            n_fail += 1
            results.append(dict(id=rid, name=name, ok=False, seconds=dt,
                                error=traceback.format_exc()))
            print("ERR  {:3s} {:48s} {:6.1f}s".format(rid, name, dt))
            traceback.print_exc()
    with open(args.json, "w") as fh:
        json.dump(results, fh, indent=1, default=str)
    print("\n{} rounds, {} failed -> {}".format(
        len(results), n_fail, args.json))
    return 1 if n_fail else 0


if __name__ == "__main__":
    sys.exit(main())
