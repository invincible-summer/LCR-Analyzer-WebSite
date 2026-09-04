"""Unit tests: fitting engine (noiseless recovery, reports, weak params)."""

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import numpy as np
import pytest

from topofit_id import FitConfig, identify
from topofit_id.graph import eval_group
from topofit_id.synthetic import default_frequencies, make_duts

F = default_frequencies()
DUTS = {d.name: d for d in make_duts()}


def true_groups(dut):
    from topofit_id import reduce_graph
    red = reduce_graph(dut.edges)
    return red, [eval_group(g.expr, dut.values) for g in red.edges]


@pytest.mark.parametrize("name", ["ser_rc", "par_rlc", "ind_parasitic",
                                  "ladder", "par_rc", "par_ll",
                                  "ser_rl_absorb", "cap_parasitic",
                                  "double_tank", "nested_red"])
def test_noiseless_exact_recovery(name):
    dut = DUTS[name]
    z = dut.z_exact(F)
    r = identify(F, z, dut.edges, FitConfig(seed=1))
    red, tv = true_groups(dut)
    assert r.wrmse < 1e-8
    assert r.jac_rank == r.n_params          # identifiable by construction
    for got, want, g in zip(r.group_values(), tv, red.edges):
        for i in range(len(want) - 1):
            assert got[1 + i] == pytest.approx(want[1 + i], rel=1e-4)


def test_noiseless_bridge_curve_only():
    """Bridge: 5 params vs first-order Z -> 2-dof exact family.  The curve
    must fit to machine precision; parameters are flagged unidentifiable."""
    dut = DUTS["bridge"]
    z = dut.z_exact(F)
    r = identify(F, z, dut.edges, FitConfig(seed=1))
    assert r.wrmse < 1e-8
    assert r.jac_rank < r.n_params
    # dense in-band curve matches truth
    fd = np.logspace(1, 7, 100)
    rel = np.max(np.abs((r.z_model(fd) - dut.z_exact(fd)) / dut.z_exact(fd)))
    assert rel < 1e-6


def test_reducible_dangling_reported():
    dut = DUTS["reducible"]
    z = dut.z_exact(F)
    r = identify(F, z, dut.edges, FitConfig(seed=1))
    assert len(r.groups) == 1                 # R1+R2 merged, C dangling
    rep = {e.index: e for e in r.edges_out}
    assert rep[2].status == "dropped" and rep[2].note == "dangling"
    assert rep[0].status == "merged" and rep[1].status == "merged"
    agg = r.group_values()[0]
    assert agg[1] == pytest.approx(320.0, rel=1e-4)


def test_weak_param_flagged_for_out_of_band_element():
    """R || big C (corner 1/(2pi RC) far below the band): in-band the
    capacitor dominates |Z| ~ 1/(wC); the resistor is invisible and must
    be flagged weak while the curve still fits and C is accurate."""
    edges = [(0, 1, "R"), (0, 1, "C")]
    from topofit_id.nodal import NodalModel
    m = NodalModel.from_edges(edges)
    z = m.z_linear(np.array([1e4, 5e-4]), 1j * 2 * np.pi * F)
    r = identify(F, z, edges, FitConfig(seed=1))
    rg = [g for g in r.groups if g.kind == "R"][0]
    cg = [g for g in r.groups if g.kind == "C"][0]
    assert rg.weak_params == ("v",)
    assert cg.weak_params == ()
    assert cg.value[1] == pytest.approx(5e-4, rel=1e-3)
    assert r.wrmse < 0.01


def test_noisy_recovery_small_error():
    from topofit_id.synthetic import measure
    dut = DUTS["ind_parasitic"]
    f, z = measure(dut, sigma_rel=0.005, seed=11)
    r = identify(f, z, dut.edges, FitConfig(seed=2))
    red, tv = true_groups(dut)
    assert r.wrmse < 0.01                    # at the noise floor ~ 0.007
    for got, want in zip(r.group_values(), tv):
        for i in range(len(want) - 1):
            assert got[1 + i] == pytest.approx(want[1 + i], rel=0.05)
    # dense-grid curve close to truth in band
    fd = np.logspace(1.3, 6.7, 100)
    rel = np.max(np.abs((r.z_model(fd) - dut.z_exact(fd)) / dut.z_exact(fd)))
    assert rel < 0.03


def test_escalation_helps_hard_case():
    """double_tank with noise: with escalation enabled the fit must land
    at the noise floor."""
    from topofit_id.synthetic import measure
    dut = DUTS["double_tank"]
    f, z = measure(dut, sigma_rel=0.005, seed=5)
    r = identify(f, z, dut.edges, FitConfig(seed=3))
    assert r.wrmse < 0.012


def test_stage_e_last_resort_rescues_stuck_basin():
    """Regression (2026-09-03): a multi-tank weakly-determined random case
    that stages A-D leave stuck at 7x the floor must be pulled to the floor
    by stage E's mixed multi-scale restarts (basin hopping)."""
    from topofit_id.synthetic import measure, random_case
    rng = np.random.default_rng(7007)
    dut = random_case(rng)
    f, z = measure(dut, sigma_rel=0.005, seed=7007 * 2 + 1)
    r = identify(f, z, dut.edges, FitConfig(seed=7007))
    assert r.wrmse <= 3.0 * np.sqrt(2.0) * 0.005
