"""Tests for the auto-mode ranking engine."""
from __future__ import annotations
import numpy as np
import pytest

from app.dsp.fit_auto import fit_auto
from app.dsp.topology_fit import MODELS


def _z_model(name, p, f):
    md = MODELS[name]
    return md.func(np.asarray(p, dtype=float), 2 * np.pi * np.asarray(f))


def test_series_rlc_prefers_named_topology():
    f = np.logspace(2, 6, 60)
    Z = _z_model("series_RLC", [10.0, 1e-4, 1e-7], f)
    res = fit_auto(f, Z)
    assert res.best.kind == "topology" and res.best.name == "series_RLC"
    assert res.best.topo.params["R"] == pytest.approx(10.0, rel=0.01)
    rows = res.to_summary()
    assert rows[0]["selected"] or any(r["selected"] for r in rows)
    assert len(rows) == 1 + len(MODELS)


def test_two_resonance_network_needs_vf():
    """Series chain of two parallel RLC sections: no fixed topology can
    capture it -- the vector-fitting candidate must win."""
    f = np.logspace(2, 7, 160)
    s = 1j * 2 * np.pi * f
    z1 = 1 / (1 / 1000 + 1 / (s * 1e-2) + s * 1e-6)
    z2 = 1 / (1 / 50 + 1 / (s * 1e-5) + s * 1e-9)
    Z = z1 + z2
    res = fit_auto(f, Z)
    assert res.best.kind == "vf"
    assert res.best.synthesis is not None
    assert res.best.synthesis.passive
    # every fixed topology must have a clearly worse AICc here
    topo_rows = [r for r in res.to_summary() if r["kind"] == "topology"]
    assert all(r["aicc"] > res.best.aicc for r in topo_rows)


def test_ranking_is_sorted_and_complete():
    f = np.logspace(2, 6, 40)
    Z = _z_model("parallel_RC", [330.0, 4.7e-9], f)
    res = fit_auto(f, Z)
    aiccs = [c.aicc for c in res.ranking]
    assert aiccs == sorted(aiccs)
    assert res.best.name == "parallel_RC"
