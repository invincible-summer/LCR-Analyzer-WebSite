"""Tests for library.py: canonical topology enumeration counts (DESIGN.md §4.2).

Locked counts (verified by hand for n<=4, asserted by code for n=5,6):
  depth<=2:  n = 1..6 -> 3, 6, 20, 36, 54, 78
  depth<=3:  n = 4    -> 90
"""

from rlc_id import library
from rlc_id.circuits import canonical

EXPECTED_DEPTH2 = {1: 3, 2: 6, 3: 20, 4: 36, 5: 54, 6: 78}


def test_counts_depth2():
    assert library.counts(6, 2) == EXPECTED_DEPTH2


def test_counts_depth3_n4():
    assert library.counts(4, 3)[4] == 90


def test_no_duplicate_canonicals():
    lib = library.get_library(4)
    cans = [canonical(t) for t in lib]
    assert len(cans) == len(set(cans))


def test_library_cumulative():
    lib = library.get_library(4)
    assert len(lib) == sum(EXPECTED_DEPTH2[n] for n in range(1, 5))
