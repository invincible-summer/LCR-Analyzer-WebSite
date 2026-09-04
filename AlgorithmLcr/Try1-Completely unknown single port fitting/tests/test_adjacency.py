"""Tests for adjacency.py: unified output format (../../OUTPUT_FORMAT.md).

Structure assertions plus the Z cross-validation required by the root spec
(sec.6): rebuild the graph from the matrix and re-evaluate Z(f) with an
independent nodal solver; it must match ``circuits.evaluate`` exactly.
An L edge carries its fitted series DC resistance (v2 model).
"""

import numpy as np
import pytest

from rlc_id.adjacency import Adjacency, Edge, _n_chain_nodes, tree_to_adjacency
from rlc_id.circuits import (PAR, SER, Leaf, evaluate_f, make_node,
                             n_leaves)
from rlc_id.synthetic import make_duts


# ---------------------------------------------------------------------------
# independent nodal solver for the Z cross-validation
# ---------------------------------------------------------------------------

def _edge_impedance(e: Edge, s: complex) -> complex:
    if e.type == "R":
        return e.parameter + 0.0j
    if e.type == "L":
        return s * e.parameter + e.dcr
    return 1.0 / (s * e.parameter)


def z_from_adjacency(adj: Adjacency, f) -> np.ndarray:
    """Driving-point impedance via nodal analysis, ports 0-1, 1 A injected.

    Node 1 is grounded (exact reduction, no pseudo-inverse truncation).
    """
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
        keep = [n for n in range(V) if n != 1]      # ground terminal 1
        Yr = Y[np.ix_(keep, keep)]
        Ir = np.zeros(V - 1, dtype=complex)
        Ir[keep.index(0)] = 1.0                     # 1 A into terminal 0
        v = np.linalg.solve(Yr, Ir)
        z_out[m] = v[keep.index(0)]
    return z_out


def _is_connected(adj: Adjacency) -> bool:
    seen = {0}
    stack = [0]
    nbr = {n: [] for n in range(adj.V)}
    for i, j, _ in adj.occupied():
        nbr[i].append(j)
        nbr[j].append(i)
    while stack:
        for y in nbr[stack.pop()]:
            if y not in seen:
                seen.add(y)
                stack.append(y)
    return len(seen) == adj.V


# ---------------------------------------------------------------------------
# structure
# ---------------------------------------------------------------------------

class TestStructure:

    def test_bare_leaf(self):
        adj = tree_to_adjacency(Leaf("R"), np.array([2.0]))  # 100 ohm
        assert adj.V == 2
        assert adj.slot(0, 1) == [Edge("R", 100.0, 0.0)]
        assert adj.n_edges == 1

    def test_bare_inductor_carries_dcr(self):
        # L device consumes two theta entries -> Edge("L", L, Rd)
        # (values chosen to roundtrip exactly through log10)
        adj = tree_to_adjacency(Leaf("L"),
                                np.log10([1e-3, 0.5]))  # 1 mH, 0.5 ohm
        assert adj.slot(0, 1) == [Edge("L", 1e-3, 0.5)]

    def test_series_rl_canonical_order(self):
        # make_node sorts children by canonical string: L before R
        t = make_node(SER, [Leaf("R"), Leaf("L")])
        adj = tree_to_adjacency(t, np.log10([1e-3, 0.5, 1e2]))
        assert adj.V == 3
        assert adj.slot(0, 2) == [Edge("L", 1e-3, 0.5)]
        assert adj.slot(1, 2) == [Edge("R", 1e2, 0.0)]

    def test_parallel_rc_multiedge(self):
        t = make_node(PAR, [Leaf("R"), Leaf("C")])
        adj = tree_to_adjacency(t, np.log10([1e-8, 1e3]))  # C first (canon)
        assert adj.V == 2
        assert adj.slot(0, 1) == [Edge("C", 1e-8, 0.0), Edge("R", 1e3, 0.0)]

    def test_parallel_ll_multiedge_with_dcr(self):
        # two real inductors in parallel: a legal multi-edge, each with dcr
        t = make_node(PAR, [Leaf("L"), Leaf("L")])
        theta = np.log10([1e-3, 0.5, 1e-5, 1e1])
        adj = tree_to_adjacency(t, theta)
        assert adj.V == 2
        assert adj.slot(0, 1) == [Edge("L", 1e-3, 0.5),
                                  Edge("L", 1e-5, 1e1)]
        # graph Z must equal tree Z (independent MNA)
        f = np.logspace(2.0, 6.0, 25)
        np.testing.assert_allclose(
            z_from_adjacency(adj, f),
            evaluate_f(t, theta, f),
            rtol=1e-9, atol=1e-12)

    def test_nested_ser_par(self):
        # SER(PAR(L, C), R): PAR first, chain 0-2-1, PAR children C then L
        t = make_node(SER, [make_node(PAR, [Leaf("L"), Leaf("C")]), Leaf("R")])
        adj = tree_to_adjacency(t, np.log10([1e-9, 1e-3, 1e1, 1e1]))
        assert adj.V == 3
        assert adj.slot(0, 2) == [Edge("C", 1e-9, 0.0),
                                  Edge("L", 1e-3, 10.0)]
        assert adj.slot(1, 2) == [Edge("R", 10.0, 0.0)]

    def test_theta_length_validated(self):
        # SER(R, L) needs 3 parameters (L carries two): 1 must raise
        with pytest.raises(ValueError):
            tree_to_adjacency(make_node(SER, [Leaf("R"), Leaf("L")]),
                              np.array([1.0]))

    def test_slot_bounds_validated(self):
        adj = Adjacency(3)
        with pytest.raises(ValueError):
            adj.slot(1, 0)
        with pytest.raises(ValueError):
            adj.slot(0, 3)


class TestInvariantsOverDuts:
    """Spec sec.6: shape + conservation + connectivity on all DUTs."""

    @pytest.mark.parametrize("dut", make_duts(), ids=lambda d: d.name)
    def test_shape_and_connectivity(self, dut):
        adj = tree_to_adjacency(dut.tree, dut.theta)
        assert adj.V == 2 + _n_chain_nodes(dut.tree)
        for i in range(adj.V):
            assert len(adj.rows[i]) == adj.V - 1 - i  # strict upper triangle
        assert adj.n_edges == n_leaves(dut.tree)
        assert _is_connected(adj)

    @pytest.mark.parametrize("dut", make_duts(), ids=lambda d: d.name)
    def test_deterministic(self, dut):
        a1 = tree_to_adjacency(dut.tree, dut.theta)
        a2 = tree_to_adjacency(dut.tree, dut.theta)
        assert a1.occupied() == a2.occupied()

    @pytest.mark.parametrize("dut", make_duts(), ids=lambda d: d.name)
    def test_z_cross_validation(self, dut):
        adj = tree_to_adjacency(dut.tree, dut.theta)
        f = np.logspace(1.0, 7.0, 30)
        z_tree = evaluate_f(dut.tree, dut.theta, f)
        z_graph = z_from_adjacency(adj, f)
        np.testing.assert_allclose(z_graph, z_tree, rtol=1e-9, atol=1e-12)


class TestFormat:

    def test_format_block(self):
        t = make_node(SER, [Leaf("R"), Leaf("L")])
        adj = tree_to_adjacency(t, np.log10([1e-3, 1e1, 1e2]))
        text = adj.format_block(label=1)
        assert text.splitlines()[0] == "adjacency[1] V=3 (ports 0,1):"
        assert "(0,2): L 1.000e-03 dcr 1.000e+01" in text
        assert "(1,2): R 1.000e+02" in text

    def test_dcr_omitted_when_zero(self):
        adj = Adjacency(2)
        adj.add(0, 1, Edge("L", 1e-3, 0.0))
        assert "dcr" not in adj.format_block()
        adj.add(0, 1, Edge("L", 2e-3, 0.5))
        assert "dcr 5.000e-01" in adj.format_block()
