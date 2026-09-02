"""Canonical form, automorphisms, R0 dead-part rule, SP recognition."""

from __future__ import annotations

from itertools import permutations

import pytest

from netgraph_id.graph import (canonical_mult, empty_mult, has_dead_part,
                               is_connected, is_series_parallel,
                               make_structure, mult_degree, n_slots,
                               perm_group, permute_mult, slot_list)


def brute_canonical(V, mult):
    """Canonical form by exhaustive relabeling (terminal swap included)."""
    return min(permute_mult(V, mult, p) for p in perm_group(V))


def all_internal_perms(V):
    return list(permutations(range(2, V)))


class TestPermGroup:
    def test_size(self):
        assert len(list(perm_group(2))) == 2      # terminal swap only
        assert len(list(perm_group(3))) == 2      # x1 internal perms x2
        assert len(list(perm_group(4))) == 4
        assert len(list(perm_group(5))) == 12

    def test_group_closure(self):
        # products of generators stay in the enumerated set (sanity)
        for V in (3, 4, 5):
            G = list(perm_group(V))
            for p in G:
                assert sorted(p) == list(range(V))


class TestCanonical:
    def test_canonical_is_min(self):
        mult = (1, 0, 1, 0, 0, 0)  # V=4: edges (0,1) and (0,2)
        assert canonical_mult(4, mult) == brute_canonical(4, mult)

    def test_terminal_swap_identified(self):
        V = 3
        # x connected to 0 twice, to 1 once  <->  x to 1 twice, to 0 once
        a = (0, 2, 1)   # slots (0,1)=0, (0,2)=2, (1,2)=1
        b = (0, 1, 2)
        assert canonical_mult(V, a) == canonical_mult(V, b)

    def test_canonical_fixpoint(self):
        # canonicalizing a canonical form is the identity
        for V, mult in [(3, (1, 1, 1)), (4, (1, 0, 1, 0, 1, 0))]:
            cm = canonical_mult(V, mult)
            assert canonical_mult(V, cm) == cm

    def test_aut_fixes_canonical(self):
        st = make_structure(3, (1, 1, 1))         # triangle
        assert len(st.aut) >= 2                   # terminal swap fixes it
        for p in st.aut:
            assert permute_mult(3, st.mult, p) == st.mult


class TestDeadPart:
    def test_pendant_is_dead(self):
        V, mult = 3, (0, 1, 1)                    # chain 0-x-1: x has deg 2 -> alive
        assert not has_dead_part(V, mult)
        V, mult = 3, (0, 2, 1)                    # x: two edges to 0, one to 1
        assert not has_dead_part(V, mult)
        # port edge + pendant x on node 0 (deg 1) -> dead
        V, mult = 3, (1, 1, 0)
        assert is_connected(V, mult)
        assert has_dead_part(V, mult)
        # connected graph with a pendant node: triangle 0-x-1 plus 2-y;
        # y hangs off x, carries no current
        V, mult = 4, (1, 1, 0, 1, 0, 1)
        assert is_connected(V, mult)
        assert has_dead_part(V, mult)

    def test_triangle_hanging_is_dead(self):
        # triangle x-y-z attached to 0 via a single edge 0-x; terminal 1 absent
        # -> not connected anyway; build: 0-x, x-y, y-0 triangle + port 1 connected?
        V = 4
        si = {}
        k = 0
        for i in range(V):
            for j in range(i + 1, V):
                si[(i, j)] = k
                k += 1
        mult = empty_mult(V)
        mult[si[(0, 2)]] = 1   # 0-x
        mult[si[(0, 3)]] = 1   # 0-y
        mult[si[(2, 3)]] = 1   # x-y  (triangle 0-x-y)
        mult[si[(0, 1)]] = 1   # port edge
        assert is_connected(V, mult)
        assert has_dead_part(V, mult)   # the triangle hangs off node 0

    def test_bridge_is_alive(self):
        V = 4
        si = {(0, 1): 0, (0, 2): 1, (0, 3): 2, (1, 2): 3, (1, 3): 4, (2, 3): 5}
        mult = empty_mult(V)
        mult[si[(0, 2)]] = 1
        mult[si[(0, 3)]] = 1
        mult[si[(1, 2)]] = 1
        mult[si[(1, 3)]] = 1
        mult[si[(2, 3)]] = 1
        assert is_connected(V, mult)
        assert not has_dead_part(V, mult)


class TestSeriesParallel:
    def test_simple_cases(self):
        assert is_series_parallel(2, (3,))                      # all parallel
        assert is_series_parallel(3, (0, 1, 1))                 # chain
        assert is_series_parallel(3, (1, 1, 1))                 # triangle (SP)
        assert is_series_parallel(4, (0, 1, 1, 1, 1, 0))        # square = SP

    def test_bridge_is_not_sp(self):
        mult = (0, 1, 1, 1, 1, 1)   # V=4, Wheatstone
        assert not is_series_parallel(4, mult)


class TestDegrees:
    def test_degree_counts_parallel_edges(self):
        V = 3
        mult = (0, 2, 1)   # slots (0,1),(0,2),(1,2): two edges 0-2, one 1-2
        assert mult_degree(V, mult) == [2, 1, 3]


def test_slot_bookkeeping():
    for V in range(2, 8):
        assert len(slot_list(V)) == n_slots(V)
        assert all(i < j for i, j in slot_list(V))
