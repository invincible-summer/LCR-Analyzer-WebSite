"""Tests for adjacency.py: unified output format (../../OUTPUT_FORMAT.md).

Slot-expansion consistency against network_str, edge conservation, dcr
passthrough, and the Z cross-validation required by the root spec (sec.6):
re-evaluate Z(f) from the matrix with an independent nodal solver and
compare with the engine's ``network_z``.
"""

from __future__ import annotations

import numpy as np
import pytest

from netgraph_id.adjacency import Edge, network_to_adjacency
from netgraph_id.components import Component, ComponentSet
from netgraph_id.synthetic import default_frequencies, make_duts, network_str


def _label(compset: ComponentSet, e: Edge) -> str:
    """Component label of an Edge (components with equal keys share labels)."""
    return Component(kind=e.type, value=e.parameter, dcr=e.dcr).label()


def _edge_impedance(e: Edge, s: complex) -> complex:
    if e.type == "R":
        return e.parameter + 0.0j
    if e.type == "L":
        return s * e.parameter + e.dcr
    return 1.0 / (s * e.parameter)


def z_from_adjacency(adj, f) -> np.ndarray:
    """Independent nodal solver: ground terminal 1, inject 1 A at terminal 0."""
    V = adj.V
    z_out = np.empty(len(f), dtype=complex)
    for m, fm in enumerate(np.asarray(f, dtype=float)):
        s = 2j * np.pi * fm
        Y = np.zeros((V, V), dtype=complex)
        for i, j, edges in adj.occupied():
            for e in edges:
                y = 1.0 / _edge_impedance(e, s)
                Y[i, i] += y
                Y[j, j] += y
                Y[i, j] -= y
                Y[j, i] -= y
        keep = [n for n in range(V) if n != 1]
        Yr = Y[np.ix_(keep, keep)]
        Ir = np.zeros(V - 1, dtype=complex)
        Ir[keep.index(0)] = 1.0
        v = np.linalg.solve(Yr, Ir)
        z_out[m] = v[keep.index(0)]
    return z_out


class TestHandCases:

    def test_parallel_rc_multiedge(self):
        # components sorted canonically: C=0, R=1
        cs = ComponentSet.make(n_R=[1e3], n_C=[100e-9])
        from netgraph_id.synthetic import network_from_edges
        net = network_from_edges(cs, [(0, 1, 0), (0, 1, 1)])
        adj = network_to_adjacency(net, cs)
        assert adj.V == 2
        assert adj.slot(0, 1) == [Edge("C", 100e-9, 0.0), Edge("R", 1e3, 0.0)]

    def test_series_rl_dcr_passthrough(self):
        # components sorted canonically: L=0 (with dcr 5), R=1
        cs = ComponentSet.make(n_R=[100.0], n_L=[(10e-3, 5.0)])
        from netgraph_id.synthetic import network_from_edges
        net = network_from_edges(cs, [(0, 2, 0), (2, 1, 1)])
        adj = network_to_adjacency(net, cs)
        assert adj.V == 3
        assert adj.slot(0, 2) == [Edge("L", 10e-3, 5.0)]
        assert adj.slot(1, 2) == [Edge("R", 100.0, 0.0)]


class TestInvariantsOverDuts:
    """Spec sec.6: shape + conservation + network_str consistency."""

    @pytest.mark.parametrize("dut", make_duts(), ids=lambda d: d.name)
    def test_edges_conserved(self, dut):
        adj = network_to_adjacency(dut.network, dut.compset)
        assert adj.V == dut.network.structure.V
        for i in range(adj.V):
            assert len(adj.rows[i]) == adj.V - 1 - i
        assert adj.n_edges == dut.compset.n

    @pytest.mark.parametrize("dut", make_duts(), ids=lambda d: d.name)
    def test_matches_network_str(self, dut):
        """Each occupied slot and its in-slot edge list agree with the
        human-readable wiring string (independent formatting path)."""
        adj = network_to_adjacency(dut.network, dut.compset)
        from_str = {}
        for part in network_str(dut.network, dut.compset).split():
            slot, labels = part.split(":")
            i, j = (int(x) for x in slot.split("-"))
            from_str[(min(i, j), max(i, j))] = labels.split("||")
        from_adj = {(i, j): [_label(dut.compset, e) for e in edges]
                    for i, j, edges in adj.occupied()}
        assert from_adj == from_str

    @pytest.mark.parametrize("dut", make_duts(), ids=lambda d: d.name)
    def test_deterministic(self, dut):
        a1 = network_to_adjacency(dut.network, dut.compset)
        a2 = network_to_adjacency(dut.network, dut.compset)
        assert a1.occupied() == a2.occupied()

    @pytest.mark.parametrize("dut", make_duts(), ids=lambda d: d.name)
    def test_z_cross_validation(self, dut):
        from netgraph_id.nodal import network_z
        adj = network_to_adjacency(dut.network, dut.compset)
        f = default_frequencies()
        # rtol 1e-6 (not 1e-9): DUTs like dut5_cpar mix admittances over 8
        # decades (2 nH at 10 Hz), so two nodal formulations (ground-0 vs
        # ground-1) legitimately differ by ~3e-7; a wrong slot or component
        # mapping would show up at O(1).
        np.testing.assert_allclose(
            z_from_adjacency(adj, f),
            network_z(dut.network, dut.compset, f),
            rtol=1e-6, atol=1e-9)


class TestFormat:

    def test_format_block(self):
        cs = ComponentSet.make(n_R=[1e3], n_C=[100e-9])
        from netgraph_id.synthetic import network_from_edges
        net = network_from_edges(cs, [(0, 1, 0), (0, 1, 1)])
        adj = network_to_adjacency(net, cs)
        text = adj.format_block(label=2)
        lines = text.splitlines()
        assert lines[0] == "adjacency[2] V=2 (ports 0,1):"
        assert lines[1] == "  (0,1): C 1.000e-07 | R 1.000e+03"

    def test_dcr_shown_for_real_inductor(self):
        cs = ComponentSet.make(n_L=[(10e-3, 5.0)])
        from netgraph_id.synthetic import network_from_edges
        net = network_from_edges(cs, [(0, 1, 0)])
        text = network_to_adjacency(net, cs).format_block()
        assert "L 1.000e-02 dcr 5.000e+00" in text
