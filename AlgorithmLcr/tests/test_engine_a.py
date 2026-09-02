"""Tests for fit_engine_a.py: parameter fitting accuracy (noiseless + noisy)."""

import numpy as np
import pytest

from rlc_id import pruning, synthetic
from rlc_id.circuits import bounds, canonical
from rlc_id.fit_engine_a import (EngineAConfig, aicc, default_weights,
                                 fit_library, fit_topology, heuristic_starts,
                                 lhs_starts)

DUTS = {d.name: d for d in synthetic.make_duts()}


def _fit_true_topology(dut, sigma_rel, seed=0):
    f, z = synthetic.measure(dut, sigma_rel=sigma_rel, seed=seed)
    s = 1j * 2 * np.pi * f
    w = default_weights(z)
    hints = pruning.hints_from_features(
        pruning.extract_asymptotics(2 * np.pi * f, z))
    lb, ub = bounds(dut.tree)
    starts = heuristic_starts(dut.tree, hints)
    # resonant topologies are multimodal; the designed flow (appendix B.2)
    # seeds engine A with engine B's Foster solutions -- mirror that here
    from rlc_id.fit_engine_b import foster_candidates
    fcands, _, _ = foster_candidates(2 * np.pi * f, z, w, max_order=4)
    for c in fcands:
        if not c.skipped and c.canonical == canonical(dut.tree):
            starts.append(c.theta)
    starts += lhs_starts(12, lb, ub, np.random.default_rng(seed))
    return fit_topology(dut.tree, s, z, w, starts, tol=1e-12)


class TestNoiselessRecovery:
    """Noiseless data: parameters must come back to ~machine precision."""

    @pytest.mark.parametrize("name", ["dut2a_ser_RL", "dut3a_par_RC",
                                      "dut4_ind_parasitic", "dut6_relaxation"])
    def test_param_error(self, name):
        dut = DUTS[name]
        cand = _fit_true_topology(dut, sigma_rel=0.0)
        assert synthetic.max_param_error(cand.theta, dut) < 1e-4
        assert cand.wrmse < 1e-10


class TestNoisyRecovery:
    """0.5% noise: parameter error must stay well below the noise level."""

    @pytest.mark.parametrize("name", ["dut2a_ser_RL", "dut3a_par_RC",
                                      "dut4_ind_parasitic"])
    def test_param_error(self, name):
        dut = DUTS[name]
        cand = _fit_true_topology(dut, sigma_rel=0.005)
        assert synthetic.max_param_error(cand.theta, dut) < 0.02


def test_aicc_prefers_truth_on_noiseless():
    """AICc must rank the true topology first on noiseless data."""
    dut = DUTS["dut3a_par_RC"]
    f, z = synthetic.measure(dut, sigma_rel=0.0)
    s = 1j * 2 * np.pi * f
    w = default_weights(z)
    hints = pruning.hints_from_features(
        pruning.extract_asymptotics(2 * np.pi * f, z))
    from rlc_id import library
    from rlc_id.circuits import canonical
    trees = [t for t in library.get_library(3)]
    fits = fit_library(trees, s, z, w, config=EngineAConfig(), hints=hints)
    fits.sort(key=lambda c: c.aicc_val)
    assert fits[0].canonical == canonical(dut.tree)
