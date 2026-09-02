"""Tests for pruning.py: F2 asymptotic filter and F3 energy bound."""

import numpy as np

from rlc_id import library, pruning, synthetic
from rlc_id.circuits import canonical

DUTS = {d.name: d for d in synthetic.make_duts()}


def test_f2_never_prunes_truth():
    """F2 must keep the true topology for every DUT in the suite."""
    for dut in DUTS.values():
        from rlc_id.circuits import n_leaves
        lib = list(library.get_library(max(4, n_leaves(dut.tree))))
        f, z = synthetic.measure(dut, sigma_rel=0.005)
        feat = pruning.extract_asymptotics(2 * np.pi * f, z)
        kept = [t for t in lib if pruning.prune_f2(t, feat)]
        assert any(canonical(t) == canonical(dut.tree) for t in kept), \
            f"F2 pruned the truth for {dut.name}"


def test_f3_never_prunes_truth():
    """F3 with the true energy count must keep the true topology."""
    for dut in DUTS.values():
        from rlc_id.circuits import leaf_kinds
        true_energy = sum(1 for k in leaf_kinds(dut.tree) if k in "LC")
        assert pruning.prune_f3(dut.tree, true_energy)


def test_f2_actually_prunes():
    """F2 must remove *some* topologies on a clearly capacitive DUT."""
    dut = DUTS["dut1c_C"]
    f, z = synthetic.measure(dut, sigma_rel=0.005)
    feat = pruning.extract_asymptotics(2 * np.pi * f, z)
    lib = list(library.get_library(4))
    kept = [t for t in lib if pruning.prune_f2(t, feat)]
    assert len(kept) < len(lib)


def test_prune_fallback_never_empty():
    """If filters would kill everything, fall back to the full library."""
    dut = DUTS["dut1a_R"]
    f, z = synthetic.measure(dut, sigma_rel=0.005)
    feat = pruning.extract_asymptotics(2 * np.pi * f, z)
    lib = list(library.get_library(2))
    out = pruning.prune(lib, feat, min_energy=99)  # impossible bound
    assert len(out) == len(lib)
