"""Enumeration completeness and no-duplication against brute force."""

from __future__ import annotations

from itertools import product

import pytest

from netgraph_id.components import Component, ComponentSet
from netgraph_id.enumerate import enumerate_structures, iter_assignments
from netgraph_id.graph import (Network, canonical_mult, empty_mult,
                               has_dead_part, is_connected, n_slots,
                               perm_group, permute_mult, permute_slot_keys,
                               slot_list)


def _full_network_serials(compset: ComponentSet, E: int):
    """Brute force: every assignment of the E (distinguishable) components
    to node-pair slots, for every V, canonicalized exactly like the
    enumerator: relabel to the canonical structure first, then minimize the
    per-slot serialization over the structure automorphism group."""
    keys = [c.key() for c in compset.components]
    serials: set = set()
    for V in range(2, E + 2):
        S = n_slots(V)
        for assign in product(range(S), repeat=E):
            per_slot: list[list[tuple]] = [[] for _ in range(S)]
            mult = empty_mult(V)
            for comp_idx, slot in enumerate(assign):
                per_slot[slot].append(keys[comp_idx])
                mult[slot] += 1
            if not is_connected(V, mult):
                continue
            if has_dead_part(V, mult):
                continue
            cm = canonical_mult(V, mult)
            p_star = next(p for p in perm_group(V)
                          if permute_mult(V, mult, p) == cm)
            si = {(i, j): k for k, (i, j) in enumerate(slot_list(V))}
            ser = [()] * S
            for comp_idx, slot in enumerate(assign):
                i, j = slot_list(V)[slot]
                a, b = p_star[i], p_star[j]
                if a > b:
                    a, b = b, a
                block = list(ser[si[(a, b)]])
                block.append(keys[comp_idx])
                ser[si[(a, b)]] = tuple(sorted(block))
            ser = tuple(ser)
            aut = tuple(p for p in perm_group(V)
                        if permute_mult(V, cm, p) == cm)
            if len(aut) > 1:
                ser = min(permute_slot_keys(V, ser, p) for p in aut)
            serials.add(ser)
    return serials


def _enumerated_serials(compset: ComponentSet, E: int):
    keys = [c.key() for c in compset.components]
    serials: set = set()
    for st in enumerate_structures(E):
        for assign in iter_assignments(st, compset):
            net = Network(structure=st, assign=assign)
            serials.add(net.serialize(keys))
    return serials


class TestStructureCounts:
    """Counts locked by hand derivation (see DESIGN.md section 4.4)."""

    def test_E1(self):
        assert len(enumerate_structures(1)) == 1

    def test_E2(self):
        # all-parallel and 2-chain
        assert len(enumerate_structures(2)) == 2

    def test_E3(self):
        # parallel-triple; triangle; (2,1)-wye; 3-chain
        assert len(enumerate_structures(3)) == 4

    def test_E4(self):
        assert len(enumerate_structures(4)) == 11

    def test_all_admissible(self):
        for E in (2, 3, 4):
            for st in enumerate_structures(E):
                assert st.n_edges == E
                assert is_connected(st.V, st.mult)
                assert not has_dead_part(st.V, st.mult)
                assert canonical_mult(st.V, st.mult) == st.mult


class TestBruteForceCrossCheck:
    def test_matches_brute_force_E2_E3_E4(self):
        for E, compset in [
            (2, ComponentSet.make(n_R=[100.0], n_L=[(1e-3, 5.0)])),
            (3, ComponentSet.make(n_R=[100.0, 1e3], n_C=[100e-9])),
            (4, ComponentSet.make(n_R=[100.0, 1e3], n_C=[100e-9],
                                  n_L=[(1e-3, 5.0)])),
        ]:
            assert _enumerated_serials(compset, E) == _full_network_serials(compset, E)

    def test_identical_components_E3(self):
        # two equal resistors + one cap; interchangeability must collapse orbits
        cs = ComponentSet((Component("R", 10e3), Component("R", 10e3),
                           Component("C", 100e-9)))
        n_total = sum(1 for st in enumerate_structures(3)
                      for _ in iter_assignments(st, cs))
        # chain R-C-R is 1 wiring; R-R-C and R-C-R... count by enumeration and
        # compare against brute force with the same multiset
        assert _enumerated_serials(cs, 3) == _full_network_serials(cs, 3)

    def test_parallel_slot_collapses(self):
        # V=2: three distinguishable components, all in the single slot:
        # order within a parallel group is irrelevant -> exactly 1 assignment
        cs = ComponentSet.make(n_R=[100.0, 1e3], n_C=[100e-9])
        st = [s for s in enumerate_structures(3) if s.V == 2][0]
        assigns = list(iter_assignments(st, cs))
        assert len(assigns) == 1


class TestAutDedup:
    def test_symmetric_structure_dedup(self):
        # square 0-a-1-b-0 (V=4): Aut = {id, (0 1), (a b), (0 1)(a b)},
        # order 4; 4 distinguishable components -> 4!/4 = 12/... = 6 wirings
        cs = ComponentSet.make(n_R=[100.0, 220.0, 330.0, 470.0])
        square = None
        for st in enumerate_structures(4):
            if st.V == 4 and st.mult == (0, 1, 1, 1, 1, 0):
                square = st
        assert square is not None
        assert len(square.aut) == 4
        n = sum(1 for _ in iter_assignments(square, cs))
        assert n == 6


def test_cache_returns_same_object():
    a = enumerate_structures(4)
    b = enumerate_structures(4)
    assert a is b
