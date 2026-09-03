"""Unit tests: public API -- single and multi-graph ranking."""

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import numpy as np
import pytest

from topofit_id import FitConfig, identify, identify_many
from topofit_id.synthetic import default_frequencies, make_duts, measure

F = default_frequencies()
DUTS = {d.name: d for d in make_duts()}


def test_identify_returns_ranked_groups():
    dut = DUTS["ladder"]
    z = dut.z_exact(F)
    r = identify(F, z, dut.edges, FitConfig(seed=0))
    assert r.ok
    assert r.n_params == 4
    assert r.z_model(F).shape == (len(F),)
    # AICc finite, wrmse at machine level
    assert np.isfinite(r.aicc_val) and r.wrmse < 1e-8


def test_identify_many_ranks_true_first():
    """True topology vs two perturbed ones (edge moved / kind changed)."""
    dut = DUTS["ind_parasitic"]                 # (0,2,R),(2,1,L),(0,1,C)
    f, z = measure(dut, sigma_rel=0.005, seed=3)
    wrong1 = [(0, 2, "R"), (2, 1, "C"), (0, 1, "L")]     # kinds swapped
    wrong2 = [(0, 2, "R"), (2, 1, "L"), (0, 2, "C")]     # C moved
    res = identify_many(f, z, [wrong1, dut.edges, wrong2], FitConfig(seed=4))
    assert len(res) == 3
    assert res[0].wrmse < 0.01                 # winner fits at the floor
    # both the true graph and the 4-param swapped-kinds graph can sit at
    # the noise floor (their RSS difference is noise luck); the *AICc*
    # order -- which is how identify_many ranks -- must prefer the true one
    assert res[0].aicc_val <= res[1].aicc_val
    assert sorted(res[0].edges) == sorted(dut.edges)


def test_identify_many_prefers_true_over_superset():
    """A superset graph can fit too, but AICc must not punish the true one
    when both reach the floor: the simpler model wins on AICc only via p;
    here true topology must rank first among candidates."""
    dut = DUTS["ser_rc"]
    f, z = measure(dut, sigma_rel=0.005, seed=8)
    sup = [(0, 2, "R"), (2, 1, "C"), (0, 1, "R")]       # extra parallel R
    res = identify_many(f, z, [sup, dut.edges], FitConfig(seed=6))
    assert res[0].n_params <= res[1].n_params
    assert res[0].wrmse < 0.012


def test_z_model_extrapolation_matches_truth_identifiable():
    """For identifiable DUTs the fitted model extrapolates out of band."""
    dut = DUTS["cap_parasitic"]
    f, z = measure(dut, sigma_rel=0.005, seed=21)
    r = identify(f, z, dut.edges, FitConfig(seed=9))
    fd = np.logspace(0, 8, 120)                # one decade beyond each side
    rel = np.max(np.abs((r.z_model(fd) - dut.z_exact(fd)) / dut.z_exact(fd)))
    assert rel < 0.05
