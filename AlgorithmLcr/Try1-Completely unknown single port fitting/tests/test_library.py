"""Tests for library.py: canonical topology enumeration counts (DESIGN.md §4.2).

Locked counts under the v2 model (real inductors L + Rd; series R + L is
absorbed into one L device, parallel L leaves are distinct topologies):
  depth<=2:  n = 1..6 -> 3, 6, 22, 45, 87, 162
  depth<=3:  n = 4    -> 99
Compare v1 (ideal L): 3, 6, 20, 36, 54, 78 (depth-3 n=4: 90).
"""

import pytest

from rlc_id import library
from rlc_id.circuits import Leaf, canonical, make_node, n_leaves, normalize, SER, PAR

EXPECTED_DEPTH2 = {1: 3, 2: 6, 3: 22, 4: 45, 5: 87, 6: 162}


def test_counts_depth2():
    assert library.counts(6, 2) == EXPECTED_DEPTH2


def test_counts_depth3_n4():
    assert library.counts(4, 3)[4] == 99


def test_no_duplicate_canonicals():
    lib = library.get_library(4)
    cans = [canonical(t) for t in lib]
    assert len(cans) == len(set(cans))


def test_library_cumulative():
    lib = library.get_library(4)
    assert len(lib) == sum(EXPECTED_DEPTH2[n] for n in range(1, 5))


def test_library_matches_normalize():
    # every generated tree is already canonical (normalize is identity on it)
    for t in library.get_library(5):
        assert canonical(normalize(t)) == canonical(t)


class TestChildrenRules:
    """R2' (parallel multi-L) and R4 (series R-absorption) in generation."""

    def test_parallel_multi_l_topologies_present(self):
        cans = {canonical(t) for t in library.trees_of_size(3, 2)}
        assert "P(L,L,L)" in cans
        assert "P(C,L,L)" in cans
        assert "P(L,L,R)" in cans

    def test_series_r_l_never_generated(self):
        # R4: S(L,R) is electrically one L device -- excluded everywhere
        cans = {canonical(t) for t in library.get_library(6)}
        assert "S(L,R)" not in cans
        assert "S(C,L,R)" not in cans

    def test_children_ok_rules(self):
        LL = [Leaf("L"), Leaf("L")]
        assert library._children_ok(PAR, LL)
        assert not library._children_ok(SER, LL)
        RL = [Leaf("R"), Leaf("L")]
        assert not library._children_ok(SER, RL)   # R4
        assert library._children_ok(PAR, RL)
        RR = [Leaf("R"), Leaf("R")]
        assert not library._children_ok(SER, RR)
        assert not library._children_ok(PAR, RR)
        RLC = [Leaf("R"), Leaf("L"), Leaf("C")]
        assert not library._children_ok(SER, RLC)  # R4 (R with L)
        assert library._children_ok(PAR, RLC)


class TestExactLayer:
    """trees_of_size: the single-layer library used by the exact_n prior."""

    def test_layer_has_exact_device_count(self):
        for n in range(1, 5):
            for t in library.trees_of_size(n):
                assert n_leaves(t) == n

    def test_layer_size(self):
        assert len(library.trees_of_size(1)) == 3
        assert len(library.trees_of_size(2)) == 6
        assert len(library.trees_of_size(3)) == 22

    def test_rejects_non_positive(self):
        with pytest.raises(ValueError):
            library.trees_of_size(0)
