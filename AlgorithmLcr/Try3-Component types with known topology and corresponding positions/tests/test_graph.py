"""Unit tests: topology reduction rules and aggregation math."""

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import numpy as np
import pytest

from topofit_id.graph import (PortOpenError, eval_group, reduce_graph)
from topofit_id.nodal import NodalModel, model_from_reduced


def z_of(edges, group_vals, f):
    s = 1j * 2 * np.pi * np.asarray(f, dtype=float)
    red = reduce_graph(edges)
    model = model_from_reduced(red)
    flat = []
    for g, v in zip(red.edges, group_vals):
        flat.extend(v[1:])
    return model.z_linear(np.asarray(flat, dtype=float), s)


def test_series_rr_merge():
    r = reduce_graph([(0, 2, "R"), (2, 1, "R")])
    assert len(r.edges) == 1 and r.edges[0].kind == "R"
    v = eval_group(r.edges[0].expr, {0: ("R", 100.0), 1: ("R", 220.0)})
    assert v == ("R", pytest.approx(320.0))


def test_parallel_rr_merge():
    r = reduce_graph([(0, 1, "R"), (0, 1, "R")])
    assert len(r.edges) == 1
    v = eval_group(r.edges[0].expr, {0: ("R", 100.0), 1: ("R", 100.0)})
    assert v == ("R", pytest.approx(50.0))


def test_parallel_cc_merge_series_cc():
    r = reduce_graph([(0, 1, "C"), (0, 1, "C")])
    v = eval_group(r.edges[0].expr, {0: ("C", 1e-9), 1: ("C", 3e-9)})
    assert v == ("C", pytest.approx(4e-9))
    r2 = reduce_graph([(0, 2, "C"), (2, 1, "C")])
    v2 = eval_group(r2.edges[0].expr, {0: ("C", 1e-9), 1: ("C", 3e-9)})
    assert v2 == ("C", pytest.approx(0.75e-9))


def test_parallel_ll_not_merged():
    r = reduce_graph([(0, 1, "L"), (0, 1, "L")])
    assert len(r.edges) == 2


def test_series_rl_absorbed_into_l():
    r = reduce_graph([(0, 2, "R"), (2, 1, "L")])
    assert len(r.edges) == 1 and r.edges[0].kind == "L"
    v = eval_group(r.edges[0].expr, {0: ("R", 50.0), 1: ("L", 1e-3, 2.0)})
    assert v == ("L", pytest.approx(1e-3), pytest.approx(52.0))


def test_mixed_parallel_edges_kept():
    r = reduce_graph([(0, 1, "R"), (0, 1, "C"), (0, 1, "L")])
    assert len(r.edges) == 3
    kinds = sorted(e.kind for e in r.edges)
    assert kinds == ["C", "L", "R"]


def test_dangling_and_disconnected_dropped():
    r = reduce_graph([(0, 2, "R"), (2, 1, "R"), (2, 3, "C"), (5, 6, "R")])
    assert len(r.edges) == 1
    assert r.dropped[2] == "dangling"
    assert r.dropped[3] == "disconnected"


def test_self_loop_dropped():
    r = reduce_graph([(0, 1, "R"), (1, 1, "C")])
    assert len(r.edges) == 1
    assert r.dropped[1] == "self-loop"


def test_nested_parallel_then_series():
    # (R1 || R2) + R3 with a dangling C off the middle node
    r = reduce_graph([(4, 1, "R"), (4, 1, "R"), (0, 4, "R"), (4, 5, "C")])
    assert len(r.edges) == 1
    vals = {0: ("R", 1e3), 1: ("R", 2e3), 2: ("R", 300.0)}
    v = eval_group(r.edges[0].expr, vals)
    assert v == ("R", pytest.approx(1.0 / (1.0 / 1e3 + 1.0 / 2e3) + 300.0))


def test_port_open_raises():
    with pytest.raises(PortOpenError):
        reduce_graph([(0, 2, "R"), (1, 3, "R")])       # 0 and 1 disconnected
    with pytest.raises(PortOpenError):
        reduce_graph([(2, 3, "R")])                    # port untouched
    with pytest.raises(PortOpenError):
        reduce_graph([(1, 2, "R"), (2, 3, "R")])       # chain dangles away


def test_series_rc_not_merged():
    # R and C in series are NOT reducible to one primitive edge (unlike
    # R + L which absorbs into the inductor DCR)
    r = reduce_graph([(0, 4, "R"), (4, 1, "C")])
    assert len(r.edges) == 2
    kinds = sorted(e.kind for e in r.edges)
    assert kinds == ["C", "R"]


@pytest.mark.parametrize("seed", range(30))
def test_reduction_preserves_z_random(seed):
    """Z(original) == Z(reduced group aggregates) for random graphs/values."""
    from topofit_id.synthetic import random_case
    rng = np.random.default_rng(1000 + seed)
    dut = random_case(rng)
    f = np.logspace(1, 7, 25)
    try:
        z1 = dut.z_exact(f)
        z2 = dut.z_exact_reduced(f)
    except PortOpenError:
        pytest.skip("random case left the port open")
    # 1e-6: the UNREDUCED reference solve itself loses precision on stiff
    # random cases (reduction improves conditioning); real reduction bugs
    # produce O(1) errors
    rel = np.max(np.abs((z1 - z2) / z1))
    assert rel < 1e-6
