"""Nodal analysis correctness: closed forms, Try1 cross-validation,
passivity, DC/HF asymptotes."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import pytest

from netgraph_id.components import Component, ComponentSet
from netgraph_id.enumerate import enumerate_structures, iter_assignments
from netgraph_id.graph import Network
from netgraph_id.nodal import StructureStamps, asymptote_impedance, network_z
from netgraph_id.synthetic import network_from_edges

F = np.logspace(0, 7, 40)

_TRY1_DIR = Path(__file__).resolve().parents[2] / "Try1-Completely unknown single port fitting"
try:
    sys.path.insert(0, str(_TRY1_DIR))
    import rlc_id  # noqa: E402
    HAS_TRY1 = True
except Exception:  # pragma: no cover
    HAS_TRY1 = False


class TestClosedForms:
    def test_v2_parallel_formula(self):
        """V = 2 must reduce to Z = 1 / sum(Y_e) exactly (DESIGN.md 5.1)."""
        cs = ComponentSet.make(n_R=[100.0, 1e3], n_C=[100e-9],
                               n_L=[(1e-3, 5.0)])
        st = [s for s in enumerate_structures(cs.n) if s.V == 2][0]
        net = Network(structure=st, assign=tuple(range(cs.n)))
        s = 2j * np.pi * F
        y = (1 / 100.0 + 1 / 1000.0) + s * 100e-9 + 1.0 / (5.0 + s * 1e-3)
        z_expect = 1.0 / y
        z = network_z(net, cs, F)
        np.testing.assert_allclose(z, z_expect, rtol=1e-12)

    def test_series_chain(self):
        cs = ComponentSet.make(n_R=[100.0], n_C=[1e-6])
        net = network_from_edges(cs, [(0, 2, 0), (2, 1, 1)])
        s = 2j * np.pi * F
        np.testing.assert_allclose(network_z(net, cs, F),
                                   100.0 + 1.0 / (s * 1e-6), rtol=1e-12)

    def test_wheatstone_bridge_hand_value(self):
        """Balanced Wheatstone bridge: the bridge element carries no current
        and the port sees two 300-ohm paths in parallel = 150 ohm."""
        cs = ComponentSet.make(n_R=[100.0, 100.0, 200.0, 200.0, 1e6])
        # arms: 0-2:R(100) 0-3:R(100) 1-2:R(200) 1-3:R(200), bridge 2-3:R(1M)
        net = network_from_edges(cs, [(0, 2, 0), (0, 3, 1),
                                      (1, 2, 2), (1, 3, 3), (2, 3, 4)])
        z = network_z(net, cs, np.array([1e3]))
        expect = (100.0 + 200.0) * (100.0 + 200.0) / (2 * 300.0)   # 300 || 300
        np.testing.assert_allclose(z[0], expect, rtol=1e-9)

    def test_batch_equals_single(self):
        cs = ComponentSet.make(n_R=[50.0, 1e3], n_C=[10e-9],
                               n_L=[(100e-6, 2.0)])
        for st in enumerate_structures(cs.n):
            assigns = [a for _, a in zip(range(7), iter_assignments(st, cs))]
            arr = np.array(assigns[:5], dtype=np.intp)
            stamps = StructureStamps.build(st, cs)
            zb = stamps.z_full(arr, 2j * np.pi * F)
            for row in range(arr.shape[0]):
                np.testing.assert_allclose(
                    zb[row], network_z(Network(st, tuple(arr[row])), cs, F),
                    rtol=1e-10)


class TestPassivity:
    def test_real_part_nonnegative(self):
        rng = np.random.default_rng(7)
        cs = ComponentSet.make(n_R=[rng.uniform(1, 1e4)],
                               n_C=[rng.uniform(1e-9, 1e-6)],
                               n_L=[(rng.uniform(1e-6, 1e-3), rng.uniform(0, 10))])
        f = np.logspace(0, 8, 60)
        for st in enumerate_structures(3):
            for assign, _ in zip(iter_assignments(st, cs), range(5)):
                z = network_z(Network(st, assign), cs, f)
                assert np.all(z.real > -1e-9 * np.maximum(1.0, np.abs(z)))


class TestAsymptotes:
    def test_series_RC(self):
        cs = ComponentSet.make(n_R=[100.0], n_C=[1e-6])
        net = network_from_edges(cs, [(0, 2, 0), (2, 1, 1)])
        assert np.isinf(asymptote_impedance(net, cs, "dc"))
        assert asymptote_impedance(net, cs, "hf") == pytest.approx(100.0)

    def test_parallel_RL(self):
        cs = ComponentSet.make(n_R=[100.0], n_L=[(1e-3, 5.0)])
        net = network_from_edges(cs, [(0, 1, 0), (0, 1, 1)])
        assert asymptote_impedance(net, cs, "dc") == pytest.approx(100.0 * 5.0 / 105.0)
        assert asymptote_impedance(net, cs, "hf") == pytest.approx(100.0)

    def test_ideal_L_shorts_dc(self):
        cs = ComponentSet.make(n_R=[10.0], n_L=[(1e-3, 0.0)])
        net = network_from_edges(cs, [(0, 1, 0), (0, 1, 1)])   # R || L(ideal)
        assert asymptote_impedance(net, cs, "dc") == pytest.approx(0.0)
        assert asymptote_impedance(net, cs, "hf") == pytest.approx(10.0)

    def test_series_L_blocks_hf(self):
        cs = ComponentSet.make(n_R=[10.0], n_L=[(1e-3, 2.0)])
        net = network_from_edges(cs, [(0, 2, 0), (2, 1, 1)])
        assert asymptote_impedance(net, cs, "dc") == pytest.approx(12.0)
        assert np.isinf(asymptote_impedance(net, cs, "hf"))


@pytest.mark.skipif(not HAS_TRY1, reason="Try1 package not found")
class TestTry1CrossValidation:
    """Same circuit built as a Try1 series-parallel tree and as a Try2
    graph must give identical Z(f)."""

    @staticmethod
    def _try1_z(tree, leaf_values, f):
        """leaf_values: list of (leaf_object, value) in construction order;
        theta follows leaves(tree) whatever ordering make_node applied."""
        from rlc_id.circuits import evaluate_f, leaves
        by_id = {id(lf): v for lf, v in leaf_values}
        theta = np.array([np.log10(by_id[id(lf)]) for lf in leaves(tree)])
        return evaluate_f(tree, theta, f)

    def test_parallel_tank(self):
        from rlc_id.circuits import Leaf, make_node
        lr, ll, lc = Leaf("R"), Leaf("L"), Leaf("C")
        cs = ComponentSet.make(n_R=[1e3], n_C=[10e-9], n_L=[(100e-6, 0.0)])
        net = network_from_edges(cs, [(0, 1, 0), (0, 1, 1), (0, 1, 2)])
        tree = make_node("PAR", (lr, ll, lc))
        np.testing.assert_allclose(
            network_z(net, cs, F), self._try1_z(
                tree, [(lr, 1e3), (ll, 100e-6), (lc, 10e-9)], F),
            rtol=1e-10)

    def test_inductor_parasitic(self):
        from rlc_id.circuits import Leaf, make_node
        rs, rd, ll, lc = Leaf("R"), Leaf("R"), Leaf("L"), Leaf("C")
        cs = ComponentSet.make(n_R=[1.0], n_C=[50e-12], n_L=[(10e-6, 0.5)])
        net = network_from_edges(cs, [(0, 2, 2), (2, 1, 1), (0, 1, 0)])
        tree = make_node("PAR", (make_node("SER", (rs, rd, ll)), lc))
        np.testing.assert_allclose(
            network_z(net, cs, F), self._try1_z(
                tree, [(rs, 1.0), (rd, 0.5), (ll, 10e-6), (lc, 50e-12)], F),
            rtol=1e-10)

    def test_ladder(self):
        from rlc_id.circuits import Leaf, make_node
        r1, r2, c1, c2 = Leaf("R"), Leaf("R"), Leaf("C"), Leaf("C")
        # R1 + (C1 || (R2 + C2))
        cs = ComponentSet.make(n_R=[100.0, 1e3], n_C=[100e-9, 1e-6])
        net = network_from_edges(cs, [(0, 2, 2), (2, 3, 3),
                                      (3, 1, 1), (2, 1, 0)])
        tree = make_node("SER", (r1, make_node(
            "PAR", (c1, make_node("SER", (r2, c2))))))
        np.testing.assert_allclose(
            network_z(net, cs, F), self._try1_z(
                tree, [(r1, 100.0), (c1, 100e-9), (r2, 1e3), (c2, 1e-6)], F),
            rtol=1e-10)
