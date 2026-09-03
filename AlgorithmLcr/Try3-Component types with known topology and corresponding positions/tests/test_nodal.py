"""Unit tests: nodal evaluation against closed forms + adjoint Jacobian."""

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import numpy as np
import pytest

from topofit_id.graph import PortOpenError
from topofit_id.nodal import NodalModel


def s_of(f):
    return 1j * 2 * np.pi * np.asarray(f, dtype=float)


def test_port_node_required():
    with pytest.raises(PortOpenError):
        NodalModel.from_edges([(2, 3, "R")])


def test_series_rc_closed_form():
    m = NodalModel.from_edges([(0, 2, "R"), (2, 1, "C")])
    f = np.logspace(2, 6, 20)
    z = m.z_linear(np.array([1e3, 100e-9]), s_of(f))
    z_ref = 1e3 + 1.0 / (s_of(f) * 100e-9)
    assert np.allclose(z, z_ref, rtol=1e-12)


def test_parallel_rc_multiedge_closed_form():
    m = NodalModel.from_edges([(0, 1, "R"), (0, 1, "C")])
    f = np.logspace(2, 6, 20)
    z = m.z_linear(np.array([1e3, 10e-9]), s_of(f))
    z_ref = 1.0 / (1.0 / 1e3 + s_of(f) * 10e-9)
    assert np.allclose(z, z_ref, rtol=1e-12)


def test_parallel_rlc_with_dcr_closed_form():
    m = NodalModel.from_edges([(0, 1, "R"), (0, 1, "C"), (0, 1, "L")])
    f = np.logspace(1, 7, 30)
    z = m.z_linear(np.array([1e3, 10e-9, 1e-3, 1.0]), s_of(f))
    z_ref = 1.0 / (1.0 / 1e3 + s_of(f) * 10e-9 + 1.0 / (1.0 + s_of(f) * 1e-3))
    assert np.allclose(z, z_ref, rtol=1e-12)


def test_ladder_closed_form():
    m = NodalModel.from_edges([(0, 2, "L"), (2, 1, "C"), (2, 1, "R")])
    f = np.logspace(1, 7, 30)
    z = m.z_linear(np.array([1e-3, 2.0, 100e-9, 10e3]), s_of(f))
    s = s_of(f)
    z_ref = (2.0 + s * 1e-3) + 1.0 / (s * 100e-9 + 1.0 / 10e3)
    assert np.allclose(z, z_ref, rtol=1e-9)


def test_bridge_passivity_and_positivity():
    """No closed form handy for the bridge; check passivity Re Z >= 0 and
    consistency with a directly assembled Y matrix (independent code path)."""
    edges = [(0, 2, "R"), (0, 3, "R"), (2, 1, "R"), (3, 1, "R"), (2, 3, "C")]
    m = NodalModel.from_edges(edges)
    rng = np.random.default_rng(0)
    vals = 10.0 ** rng.uniform(-2, 4, size=m.n_params)
    f = np.logspace(0, 8, 60)
    z = m.z_linear(vals, s_of(f))
    assert np.all(z.real >= -1e-9 * np.abs(z))
    # independent dense assembly
    labels = sorted({n for e in edges for n in e[:2]})
    idx = {lab: i for i, lab in enumerate(labels)}
    z2 = np.empty_like(z)
    vi = 0
    yedges = []
    for (u, v, k) in edges:
        if k == "R":
            yedges.append((u, v, lambda s, r=vals[vi]: 1.0 / r))
            vi += 1
        elif k == "C":
            yedges.append((u, v, lambda s, c=vals[vi]: s * c))
            vi += 1
        else:
            raise AssertionError
    for fi, s in enumerate(s_of(f)):
        n = len(labels)
        Y = np.zeros((n, n), dtype=complex)
        for (u, v, fun) in yedges:
            y = fun(s)
            i, j = idx[u], idx[v]
            Y[i, i] += y
            Y[j, j] += y
            Y[i, j] -= y
            Y[j, i] -= y
        keep = [i for i, lab in enumerate(labels) if lab != 0]
        Yr = Y[np.ix_(keep, keep)]
        e0 = np.zeros(len(keep), dtype=complex)
        e0[keep.index(idx[1])] = 1.0
        z2[fi] = np.linalg.solve(Yr, e0)[keep.index(idx[1])]
    assert np.allclose(z, z2, rtol=1e-10)


def test_jacobian_matches_finite_differences():
    rng = np.random.default_rng(3)
    edges = [(0, 2, "R"), (0, 3, "R"), (2, 1, "L"), (3, 1, "R"), (2, 3, "C")]
    m = NodalModel.from_edges(edges)
    theta = rng.uniform(-3, 3, size=m.n_params)
    s = s_of(np.logspace(1, 7, 12))
    _, J = m.z_and_jac(theta, s)
    h = 1e-6
    scale = np.max(np.abs(J))
    for t in range(m.n_params):
        tp = theta.copy(); tp[t] += h
        tm = theta.copy(); tm[t] -= h
        fd = (m.z_and_jac(tp, s)[0] - m.z_and_jac(tm, s)[0]) / (2 * h)
        # mixed tolerance: FD itself cancels on channels with tiny |J|
        err = np.max(np.abs(J[t] - fd))
        assert err < 1e-6 * scale + 1e-5 * np.max(np.abs(fd)), f"param {t}"


def test_elasticity_matches_finite_differences():
    rng = np.random.default_rng(4)
    edges = [(0, 1, "R"), (0, 1, "C"), (0, 1, "L")]
    m = NodalModel.from_edges(edges)
    theta = rng.uniform(-2, 2, size=m.n_params)
    s = s_of(np.logspace(1, 7, 9))
    E = m.elasticity(theta, s)
    v = 10.0 ** theta
    h = 1e-5
    for t in range(m.n_params):
        vp = v.copy(); vp[t] *= (1 + h)
        vm = v.copy(); vm[t] *= (1 - h)
        z0 = m.z_linear(v, s)
        zp = m.z_linear(vp, s)
        zm = m.z_linear(vm, s)
        fd = (zp - zm) / (2 * h * z0)          # dlnZ/dlnv
        err = np.max(np.abs(E[t] - fd))
        assert err < 1e-6 * np.max(np.abs(E)) + 1e-5 * np.max(np.abs(fd))
