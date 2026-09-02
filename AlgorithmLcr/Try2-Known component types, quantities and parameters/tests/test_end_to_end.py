"""End-to-end identification of synthetic DUTs (DESIGN.md section 9)."""

from __future__ import annotations

import numpy as np
import pytest

import netgraph_id as ng
from netgraph_id.selector import are_equivalent, make_validation_grid
from netgraph_id.synthetic import make_duts, network_str, random_network


def _truth_hit(res: ng.IdentifyResult, dut) -> bool:
    """Top-1 class representative electrically equivalent to the truth?"""
    if res.best is None:
        return False
    grid = make_validation_grid(np.logspace(1, 7, 30))
    tol = max(1e-3, 3.0 * res.best.wrmse)
    return are_equivalent(res.best.representative.network, dut.network,
                          dut.compset, grid, tol)


class TestNamedDuts:
    """Named DUTs, 0.5% relative noise, default band 10 Hz .. 10 MHz."""

    def test_small_duts(self):
        duts = make_duts()
        for dut in duts[:5]:            # E = 2 and 3 DUTs
            f, z = ng.synthetic.measure(dut.network, dut.compset,
                                        sigma_rel=0.005, seed=42)
            res = ng.identify(dut.compset, f, z)
            assert _truth_hit(res, dut), (
                f"{dut.name}: best={network_str(res.best.representative.network, dut.compset)} "
                f"truth={dut.describe()}")

    def test_ladder_and_twin(self):
        duts = make_duts()
        for dut in (duts[7], duts[8]):  # E = 4, E = 3 with twin resistors
            f, z = ng.synthetic.measure(dut.network, dut.compset,
                                        sigma_rel=0.005, seed=17)
            res = ng.identify(dut.compset, f, z)
            assert _truth_hit(res, dut)

    def test_bridge_is_found_and_flagged(self):
        """The Wheatstone bridge is beyond Try1's series-parallel space;
        Try2 must find it and flag the wiring as non-SP."""
        dut = make_duts()[5]
        f, z = ng.synthetic.measure(dut.network, dut.compset,
                                    sigma_rel=0.005, seed=5)
        res = ng.identify(dut.compset, f, z)
        assert _truth_hit(res, dut)
        assert res.best.representative.sp is False

    def test_double_L_parallel_edges(self):
        dut = make_duts()[6]
        f, z = ng.synthetic.measure(dut.network, dut.compset,
                                    sigma_rel=0.005, seed=9)
        res = ng.identify(dut.compset, f, z)
        assert _truth_hit(res, dut)

    @pytest.mark.slow
    def test_six_component_mixed(self):
        dut = make_duts()[9]            # E = 6, all three kinds, bridge edge
        f, z = ng.synthetic.measure(dut.network, dut.compset,
                                    sigma_rel=0.005, seed=23)
        res = ng.identify(dut.compset, f, z)
        assert _truth_hit(res, dut)


class TestRandomDuts:
    def test_random_small(self):
        rng = np.random.default_rng(2024)
        cs = ng.ComponentSet.make(n_R=[330.0], n_C=[47e-9],
                                  n_L=[(2.2e-3, 8.0)])
        for _ in range(4):
            net = random_network(cs, rng)
            f, z = ng.synthetic.measure(net, cs, sigma_rel=0.005, seed=31)
            res = ng.identify(cs, f, z)
            grid = make_validation_grid(f)
            tol = max(1e-3, 3.0 * res.best.wrmse)
            assert are_equivalent(res.best.representative.network, net, cs,
                                  grid, tol)

    def test_random_E4(self):
        rng = np.random.default_rng(77)
        cs = ng.ComponentSet.make(n_R=[1e3, 47e3], n_C=[10e-9],
                                  n_L=[(330e-6, 3.0)])
        for _ in range(2):
            net = random_network(cs, rng)
            f, z = ng.synthetic.measure(net, cs, sigma_rel=0.005, seed=13)
            res = ng.identify(cs, f, z)
            grid = make_validation_grid(f)
            tol = max(1e-3, 3.0 * res.best.wrmse)
            assert are_equivalent(res.best.representative.network, net, cs,
                                  grid, tol)


class TestNoiseRobustness:
    def test_noiseless_exact(self):
        dut = make_duts()[3]            # E = 3
        f = ng.synthetic.default_frequencies()
        z = dut.z_exact(f)
        res = ng.identify(dut.compset, f, z)
        assert res.best.rss < 1e-20
        assert _truth_hit(res, dut)

    def test_quieter_noise_still_finds_truth(self):
        dut = make_duts()[0]
        f, z = ng.synthetic.measure(dut.network, dut.compset,
                                    sigma_rel=0.02, seed=99)
        res = ng.identify(dut.compset, f, z)
        assert _truth_hit(res, dut)


class TestFunnelSafety:
    def test_funnel_keeps_truth(self):
        """The true candidate's wiring-orbit must always survive the probe
        filter (the enumeration emits one canonical representative per
        orbit, so we compare canonical serializations)."""
        dut = make_duts()[3]
        f, z = ng.synthetic.measure(dut.network, dut.compset,
                                    sigma_rel=0.005, seed=3)
        cfg = ng.Config()
        from netgraph_id.filters import run_funnel
        w = ng.metric.default_weights(z)
        state = run_funnel(dut.compset, 2j * np.pi * f, z, w, cfg)
        keys = [c.key() for c in dut.compset.components]
        kept = {net.serialize(keys) for net in state.final_keep()}
        assert dut.network.serialize(keys) in kept
