"""Tests for adjacency.py: unified output format (../../OUTPUT_FORMAT.md).

Group placement, empty survival nodes, aggregate/multi-edge slots, notes for
merged/dropped edges, and the Z cross-validation required by the root spec
(sec.6): rebuild an edge list from the matrix, evaluate with NodalModel on
the group values, and compare with ``FitResult.z_model``.
"""

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import numpy as np
import pytest

from topofit_id import FitConfig, identify
from topofit_id.adjacency import Edge, adjacency_notes, fitresult_to_adjacency
from topofit_id.nodal import NodalModel
from topofit_id.synthetic import default_frequencies, make_duts, measure

F = default_frequencies()
DUTS = {d.name: d for d in make_duts()}


@pytest.fixture(scope="module")
def results() -> dict:
    return {name: identify(*measure(dut, sigma_rel=0.005, seed=0),
                           dut.edges, FitConfig(seed=1))
            for name, dut in DUTS.items()}


class TestPlacement:

    def test_simple_series_rc(self, results):
        # edges (0,2)R (2,1)C: two single groups on two slots, V=3
        adj = fitresult_to_adjacency(results["ser_rc"])
        assert adj.V == 3
        assert adj.n_edges == 2
        assert [e.type for e in adj.slot(0, 2)] == ["R"]
        assert [e.type for e in adj.slot(1, 2)] == ["C"]

    def test_parallel_same_kind_stays_multiedge(self, results):
        # par_ll: two parallel L edges are NOT parallel-mergeable -> two
        # separate groups on the same slot -> multi-edge vector
        adj = fitresult_to_adjacency(results["par_ll"])
        assert adj.V == 2
        assert adj.n_edges == 2
        kinds = [e.type for e in adj.slot(0, 1)]
        assert kinds == ["L", "L"]
        assert all(e.dcr > 0.0 for e in adj.slot(0, 1))

    def test_parallel_different_kinds_share_slot(self, results):
        # ladder: (2,1)C and (2,1)R are distinct groups on one slot
        adj = fitresult_to_adjacency(results["ladder"])
        kinds = sorted(e.type for e in adj.slot(1, 2))
        assert kinds == ["C", "R"]

    def test_reduction_survivors(self, results):
        # reducible: dangling C dropped, two series R merged -> 1 aggregate
        res = results["reducible"]
        adj = fitresult_to_adjacency(res)
        assert adj.V == 4                 # node 3 survives empty (dangling C)
        assert adj.n_edges == 1
        assert [e.type for e in adj.slot(0, 1)] == ["R"]
        notes = adjacency_notes(res)
        assert any(n.startswith("e2 (C) dropped") for n in notes)
        assert any(n.startswith("e0 (R) merged") for n in notes)
        assert any(n.startswith("e1 (R) merged") for n in notes)

    def test_f4_absorb_lives_in_dcr(self, results):
        # ser_rl_absorb: series R folded into the L group's dcr
        res = results["ser_rl_absorb"]
        adj = fitresult_to_adjacency(res)
        assert adj.n_edges == 1
        (e,) = adj.slot(0, 1)
        assert e.type == "L"
        assert e.dcr == pytest.approx(52.0, rel=0.2)  # R(50) + Rd(2)
        assert any("merged" in n for n in adjacency_notes(res))

    def test_sparse_original_labels(self, results):
        # nested_red uses nodes 0/1/4/5 -> V=6.  Its whole R ladder reduces
        # to one aggregate on the port slot (par-merge then ser-merge), so
        # every label above 1 survives only as an empty row/slot.
        adj = fitresult_to_adjacency(results["nested_red"])
        assert adj.V == 6
        touched = {n for i, j, _ in adj.occupied() for n in (i, j)}
        assert touched == {0, 1}
        assert adj.slot(2, 3) == []
        assert adj.n_edges == 1


class TestInvariants:
    """Spec sec.6: shape + conservation on all 12 DUTs."""

    @pytest.mark.parametrize("name", sorted(DUTS))
    def test_edges_conserved(self, results, name):
        adj = fitresult_to_adjacency(results[name])
        assert adj.n_edges == results[name].reduction.n_groups
        for i in range(adj.V):
            assert len(adj.rows[i]) == adj.V - 1 - i

    @pytest.mark.parametrize("name", sorted(DUTS))
    def test_z_cross_validation(self, results, name):
        """Rebuild the edge list from the matrix and re-evaluate Z(f) via
        NodalModel on the group values; must match FitResult.z_model."""
        res = results[name]
        adj = fitresult_to_adjacency(res)
        edges, vals = [], []
        for i, j, es in adj.occupied():
            for e in es:
                edges.append((i, j, e.type))
                vals.append(e.parameter)
                if e.type == "L":
                    vals.append(e.dcr)
        model = NodalModel.from_edges(edges)
        z_rebuilt = model.z_linear(np.asarray(vals, dtype=float),
                                   1j * 2.0 * np.pi * F)
        np.testing.assert_allclose(z_rebuilt, res.z_model(F),
                                   rtol=1e-6, atol=1e-9)


class TestFormat:

    def test_format_block_with_notes(self, results):
        res = results["reducible"]
        text = fitresult_to_adjacency(res).format_block(
            extra_lines=adjacency_notes(res))
        lines = text.splitlines()
        assert lines[0] == "adjacency V=4 (ports 0,1):"
        assert lines[1].startswith("  (0,1): R ")
        assert any("dropped" in ln for ln in lines)

    def test_dcr_shown_for_real_inductor(self, results):
        text = fitresult_to_adjacency(results["ser_rl_absorb"]).format_block()
        assert "dcr" in text
