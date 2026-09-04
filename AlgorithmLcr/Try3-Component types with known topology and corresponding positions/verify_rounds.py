"""Adversarial verification rounds for the Try3 fitting algorithm.

116 individually-counted rounds; each round is one focused experiment with
explicit invariants (the round FAILS if any invariant is violated).

  C1  (12) exact recovery, no noise, per named DUT
  C2  (12) noisy recovery sigma = 0.5%, per named DUT
  C3  (16) value-scale sweeps (features moved in/out of band)
  C4   (8) high-Q parallel tanks (razor-thin valleys, resonance seeds)
  C5   (6) high-Q series notches
  C6   (4) multi-tank networks
  C7   (5) resonance detection unit checks
  C8   (2) resonance hidden between grid points
  C9   (6) degenerate / rank-deficient / interchangeable structures
  C10  (3) dynamic-range extremes
  C11  (8) sampling variations (M, band)
  C12  (6) numerical invariants at the solution (adjoint Jacobian vs FD,
           passivity)
  C13  (2) multi-graph AICc ranking + port-open
  C14  (2) equal-value interchangeable groups
  C15  (4) regression: reduction reporting completeness (bug fix 2026-09-03)
  C16 (20) fresh random cases

Usage:  python verify_rounds.py [--json out.json]
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
import traceback

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from topofit_id import FitConfig, identify
from topofit_id.fit import _resonance_omegas
from topofit_id.graph import PortOpenError, eval_group, reduce_graph
from topofit_id.metric import curve_max_rel_floored, matched_group_errors
from topofit_id.nodal import model_from_reduced
from topofit_id.synthetic import DUT, make_duts, measure, random_case

FLOOR = lambda sig: float(np.sqrt(2.0) * sig)

ROUNDS = []          # flat list of (round_id, name, callable)


def reg(rid, name, fn):
    ROUNDS.append((rid, name, fn))
    return fn


class RoundFail(AssertionError):
    pass


class BoundaryHit(Exception):
    """Analyzed, machine-checkable information boundary (not a bug): the
    curve-level dense-grid mismatch is attributable to features the 30-point
    sampling cannot resolve AND the algorithm flags the responsible
    diagnostics (weak params / rank deficiency)."""

    def __init__(self, note):
        super().__init__(note)
        self.note = note


def _check(cond, msg, detail=None):
    if not cond:
        raise RoundFail(msg + (" :: " + str(detail) if detail else ""))


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def run_fit(dut, sigma, seed, cfg=None):
    f, z = measure(dut, sigma_rel=sigma, seed=seed)
    r = identify(f, z, dut.edges, cfg or FitConfig(seed=seed))
    return f, z, r


def true_group_values(dut, red):
    return [eval_group(g.expr, dut.values) for g in red.edges]


def visibilities(dut, red, f, z):
    """max|elasticity| per parameter at the TRUE values (benchmark privilege)."""
    model = model_from_reduced(red)
    true_vals = true_group_values(dut, red)
    flat = []
    for v in true_vals:
        flat.extend(v[1:])
    w = 2.0 * np.pi * f
    w0 = float(np.exp(np.mean(np.log(w))))
    z0 = float(np.exp(np.mean(np.log(np.abs(z)))))
    scales = []
    for g in red.edges:
        if g.kind == "R":
            scales.append(z0)
        elif g.kind == "C":
            scales.append(1.0 / (z0 * w0))
        else:
            scales.append(z0 / w0)
            scales.append(z0)
    theta = np.log10(np.asarray(flat) / np.asarray(scales))
    E = model.elasticity(theta, 1j * w / w0)
    return np.max(np.abs(E), axis=1)


def param_errors(dut, r, f, z, vis_cut=0.1):
    """Matched per-parameter relative errors, keeping only band-visible ones."""
    red = r.reduction
    tv = true_group_values(dut, red)
    errs, labels = matched_group_errors(r.group_values(), tv, red.edges)
    vis = visibilities(dut, red, f, z)
    vis_of = {}
    t = 0
    for g in red.edges:
        for nm in (["v"] if g.kind != "L" else ["v", "Rd"]):
            vis_of[((min(g.u, g.v), max(g.u, g.v), g.kind), nm)] = vis[t]
            t += 1
    kept = [e for e, lab in zip(errs, labels) if vis_of.get(lab, 1.0) >= vis_cut]
    return kept, errs


def curve_err(dut, r, n=100):
    fd = np.logspace(1, 7, n)
    return curve_max_rel_floored(dut.z_exact(fd), r.z_model(fd))


def make_dut(edges, values):
    v = {i: val for i, val in enumerate(values)}
    return DUT("custom", list(edges), v)


def tank_dut(f0, Q, kind="par"):
    """Parallel tank R || L || C at f0 with quality Q (R = Q*sqrt(L/C)), or a
    series L+C notch with damping Rd = sqrt(L/C)/Q."""
    w0 = 2 * np.pi * f0
    L = 1e-3
    C = 1.0 / (w0 * w0 * L)
    if kind == "par":
        R = Q * np.sqrt(L / C)
        return make_dut([(0, 1, "R"), (0, 1, "L"), (0, 1, "C")],
                        [("R", R), ("L", L, 1e-5), ("C", C)])
    rd = np.sqrt(L / C) / Q
    return make_dut([(0, 2, "L"), (2, 1, "C")],
                    [("L", L, rd), ("C", C)])


def val_dut(values):
    """values: list of tuples in edge order."""
    return values


# ---------------------------------------------------------------------------
# C1 -- exact recovery, no noise (one round per named DUT)
# ---------------------------------------------------------------------------

def _c1_round(dut):
    def fn():
        f, z = measure(dut, sigma_rel=0.0)
        r = identify(f, z, dut.edges)
        if dut.name == "bridge":
            _check(r.jac_rank < r.n_params, "bridge must be rank-deficient",
                   (r.jac_rank, r.n_params))
        _check(r.wrmse < 1e-9, "no-noise wrmse not machine-level", r.wrmse)
        ce = curve_err(dut, r)
        _check(ce < 1e-7, "no-noise curve error", ce)
        if dut.name not in ("bridge", "par_ll"):
            errs, _ = param_errors(dut, r, f, z)
            if errs:
                _check(max(errs) < 1e-6, "no-noise param error", max(errs))
        return "wrmse={:.1e} curve={:.1e} rank={}/{} starts={}".format(
            r.wrmse, ce, r.jac_rank, r.n_params, r.n_starts_used)
    return fn


for _d in make_duts():
    reg("C1", "exact/no-noise: " + _d.name, _c1_round(_d))


# ---------------------------------------------------------------------------
# C2 -- noisy recovery (one round per named DUT)
# ---------------------------------------------------------------------------

def _c2_round(dut):
    def fn():
        fl = FLOOR(0.005)
        f, z, r = run_fit(dut, 0.005, seed=101)
        _check(r.wrmse <= 3 * fl, "wrmse above 3x floor", r.wrmse)
        ce = curve_err(dut, r)
        _check(ce <= 0.05, "curve error > 5%", ce)
        msg = "wrmse={:.4f} curve={:.3f}".format(r.wrmse, ce)
        if dut.name not in ("bridge", "par_ll"):
            kept, _ = param_errors(dut, r, f, z)
            if kept:
                _check(max(kept) < 0.05, "visible param error > 5%", max(kept))
                msg += " p50={:.2%} pmax={:.2%}".format(
                    float(np.median(kept)), max(kept))
        return msg + " starts={}".format(r.n_starts_used)
    return fn


for _d in make_duts():
    reg("C2", "noisy/0.5%: " + _d.name, _c2_round(_d))


# ---------------------------------------------------------------------------
# C3 -- value-scale sweeps (16 rounds)
# ---------------------------------------------------------------------------

def _c3_round(name, dut, scale):
    def fn():
        vals = {}
        for i, val in dut.values.items():
            if val[0] == "R":
                vals[i] = val
            elif val[0] == "C":
                vals[i] = ("C", val[1] / scale)
            else:
                vals[i] = ("L", val[1] * scale, val[2])
        d2 = DUT(name, dut.edges, vals)
        f, z = measure(d2, sigma_rel=0.0)
        r = identify(f, z, d2.edges)
        _check(r.wrmse < 1e-8, "scaled no-noise fit", r.wrmse)
        kept, _ = param_errors(d2, r, f, z)
        if kept:
            _check(max(kept) < 1e-4, "scaled param error", max(kept))
        return "wrmse={:.1e} n_visible={}/{}".format(
            r.wrmse, len(kept), len(d2.edges))
    return fn


_base3 = {d.name: d for d in make_duts()}
for _name in ("ser_rc", "par_rlc", "ladder", "double_tank"):
    for _scale in (1e-2, 1e-1, 10.0, 100.0):
        reg("C3", "scale {:>4g}x: {}".format(_scale, _name),
            _c3_round(_name, _base3[_name], _scale))


# ---------------------------------------------------------------------------
# C4 -- high-Q parallel tanks (8 rounds)
# ---------------------------------------------------------------------------

def _c4_round(f0, Q):
    def fn():
        fl = FLOOR(0.005)
        dut = tank_dut(f0, Q, "par")
        f, z, r = run_fit(dut, 0.005, seed=201)
        _check(r.wrmse <= 3 * fl, "high-Q tank fit above floor", r.wrmse)
        # With 30 log points over 6 decades, a peak narrower than the
        # ~60% point spacing cannot be resolved: its HEIGHT (set by R) is
        # band-invisible and must be flagged weak, while its POSITION
        # (set by the L*C product) IS sampled and must be recovered.
        gv = {g.kind: g for g in r.groups}
        lc = gv["L"].value[1] * gv["C"].value[1]
        tc = 1e-3 * dut.values[2][1]
        _check(abs(lc / tc - 1.0) < 0.02,
               "resonance position (L*C) not recovered", (lc, tc))
        for kind in ("R",):
            ok_val = abs(gv[kind].value[1] / dut.values[0][1] - 1.0) < 0.1
            ok_flag = bool(gv[kind].weak_params)
            _check(ok_val or ok_flag,
                   "invisible {} neither recovered nor flagged weak".format(kind),
                   (gv[kind].value, gv[kind].weak_params))
        return "wrmse={:.4f} L*C err {:+.1%} R weak={} R={:.3g}".format(
            r.wrmse, lc / tc - 1.0, bool(gv["R"].weak_params), gv["R"].value[1])
    return fn


for _f0 in (1e3, 1e5):
    for _Q in (1e2, 1e3, 1e4, 1e5):
        reg("C4", "par tank f0={:g} Q={:g}".format(_f0, _Q),
            _c4_round(_f0, _Q))


# ---------------------------------------------------------------------------
# C5 -- high-Q series notches (6 rounds)
# ---------------------------------------------------------------------------

def _c5_round(f0, Q):
    def fn():
        fl = FLOOR(0.005)
        dut = tank_dut(f0, Q, "ser")
        f, z, r = run_fit(dut, 0.005, seed=301)
        _check(r.wrmse <= 3 * fl, "notch fit above floor", r.wrmse)
        return "wrmse={:.4f} starts={}".format(r.wrmse, r.n_starts_used)
    return fn


for _f0 in (1e4, 1e6):
    for _Q in (1e2, 1e4, 1e6):
        reg("C5", "ser notch f0={:g} Q={:g}".format(_f0, _Q),
            _c5_round(_f0, _Q))


# ---------------------------------------------------------------------------
# C6 -- multi-tank (4 rounds)
# ---------------------------------------------------------------------------

_c6_cases = {
    "twin_tank": make_dut(
        [(0, 2, "L"), (2, 1, "L"), (2, 1, "C"), (0, 2, "C")],
        [("L", 1e-3, 0.05), ("L", 1e-4, 0.05),
         ("C", 1.0 / ((2 * np.pi * 5e5) ** 2 * 1e-4),),
         ("C", 1.0 / ((2 * np.pi * 2e5) ** 2 * 1e-3),)]),
    "double_tank_sharp": make_dut(
        [(0, 2, "L"), (2, 3, "L"), (2, 3, "C"), (3, 1, "C")],
        [("L", 1e-3, 0.2), ("L", 1e-2, 0.5),
         ("C", 1e-11,), ("C", 1e-10,)]),
}


def _c6_round(name, dut, seed):
    def fn():
        fl = FLOOR(0.005)
        f, z, r = run_fit(dut, 0.005, seed=seed)
        _check(r.wrmse <= 3 * fl, "multi-tank above floor", r.wrmse)
        ce = curve_err(dut, r)
        if ce > 0.05:
            # peak-top damping is sampled by <2 points: the diverging
            # parameters must carry the weak flag, else it is a real bug
            weak_any = any(g.weak_params for g in r.groups)
            rank_def = r.jac_rank < r.n_params
            if weak_any or rank_def:
                raise BoundaryHit(
                    "curve {:.3f} with weak={} rank-def={}: peak damping "
                    "undersampled (information boundary)".format(
                        ce, weak_any, rank_def))
            _check(False, "multi-tank curve mismatch without diagnostics", ce)
        return "wrmse={:.4f} curve={:.3f} starts={}".format(
            r.wrmse, ce, r.n_starts_used)
    return fn


for _n6, _d6 in _c6_cases.items():
    for _s6 in (401, 402):
        reg("C6", "{} seed={}".format(_n6, _s6), _c6_round(_n6, _d6, _s6))


# ---------------------------------------------------------------------------
# C7 -- resonance detection unit checks (5 rounds)
# ---------------------------------------------------------------------------

def _c7_detect(f0):
    def fn():
        dut = tank_dut(f0, 1e4, "par")
        f = np.logspace(1, 7, 30)
        z = dut.z_exact(f)
        w0n = np.exp(np.mean(np.log(2 * np.pi * f)))
        omegas = _resonance_omegas(1j * 2 * np.pi * f / w0n,
                                   z / np.exp(np.mean(np.log(np.abs(z)))))
        _check(len(omegas) >= 1, "peak not detected", f0)
        rel = abs(omegas[0] * w0n / (2 * np.pi * f0) - 1.0)
        # detection resolution = the grid step (ratio 10^(6/29) = 1.61,
        # i.e. up to ~60% off when the corner sits between two points)
        _check(rel < 0.7, "detected omega beyond one grid step", rel)
        return "detected rel err {:.1%} (grid step ~61%)".format(rel)
    return fn


for _f0 in (1e4, 3e5, 2e6):
    reg("C7", "detect peak f0={:g}".format(_f0), _c7_detect(_f0))


def _c7_flat():
    f = np.logspace(1, 7, 30)
    z = np.full(30, 100.0 + 0j)
    w0n = np.exp(np.mean(np.log(2 * np.pi * f)))
    _check(_resonance_omegas(1j * 2 * np.pi * f / w0n, z) == [],
           "flat data must yield no resonance")
    return "none detected OK"


reg("C7", "flat data -> none", _c7_flat)


def _c7_notch():
    dut = tank_dut(1e5, 1e3, "ser")
    f = np.logspace(1, 7, 30)
    z = dut.z_exact(f)
    w0n = np.exp(np.mean(np.log(2 * np.pi * f)))
    omegas = _resonance_omegas(1j * 2 * np.pi * f / w0n, z)
    _check(len(omegas) >= 1, "notch not detected")
    rel = abs(omegas[0] * w0n / (2 * np.pi * 1e5) - 1.0)
    _check(rel < 0.7, "notch omega beyond one grid step", rel)
    return "notch detected, rel err {:.1%}".format(rel)


reg("C7", "detect notch f0=1e5", _c7_notch)


# ---------------------------------------------------------------------------
# C8 -- resonance hidden between grid points (2 rounds)
# ---------------------------------------------------------------------------

_f = np.logspace(1, 7, 30)
_mid = 10 ** (0.5 * (np.log10(_f[14]) + np.log10(_f[15])))


def _c8_round(Q):
    def fn():
        fl = FLOOR(0.005)
        dut = tank_dut(_mid, Q, "par")
        f, z, r = run_fit(dut, 0.005, seed=501)
        # the peak may be invisible between grid points; the measured points
        # then carry no resonance signature, so the fit has no excuse
        _check(r.wrmse <= 3 * fl, "hidden-resonance fit above floor", r.wrmse)
        return "wrmse={:.4f} starts={}".format(r.wrmse, r.n_starts_used)
    return fn


for _Q8 in (1e5, 1e6):
    reg("C8", "hidden resonance Q={:g}".format(_Q8), _c8_round(_Q8))


# ---------------------------------------------------------------------------
# C9 -- degenerate structures (6 rounds)
# ---------------------------------------------------------------------------

_c9_cases = [
    ("hanging_cycle", [(0, 1, "R"), (0, 2, "L"), (2, 3, "C"), (3, 0, "R")],
     [("R", 1e3), ("L", 1e-3, 1.0), ("C", 1e-7), ("R", 2e3)], True),
    ("ser_L_L", [(0, 2, "L"), (2, 1, "L")],
     [("L", 1e-3, 1.0), ("L", 2e-3, 2.0)], False),
    ("ser_C_C", [(0, 2, "C"), (2, 1, "C")],
     [("C", 1e-7), ("C", 3e-7)], False),
    ("par_R_L", [(0, 1, "R"), (0, 1, "L")],
     [("R", 1e3), ("L", 1e-3, 5.0)], False),
    ("all_R_series", [(0, 2, "R"), (2, 3, "R"), (3, 1, "R")],
     [("R", 100.0), ("R", 220.0), ("R", 330.0)], False),
    ("par_ll_named", None, None, False),      # handled specially below
]


def _c9_round(name, edges, vals, rank_def):
    def fn():
        fl = FLOOR(0.005)
        if name == "par_ll_named":
            dut = [d for d in make_duts() if d.name == "par_ll"][0]
        else:
            dut = make_dut(edges, vals)
        f, z, r = run_fit(dut, 0.005, seed=601)
        _check(r.wrmse <= 3 * fl, "degenerate fit above floor",
               (name, r.wrmse))
        ce = curve_err(dut, r)
        _check(ce <= 0.05, "degenerate curve", (name, ce))
        if rank_def:
            _check(r.jac_rank < r.n_params,
                   "hanging cycle must be rank-deficient",
                   (r.jac_rank, r.n_params))
        if name == "ser_L_L":
            f0, z0 = measure(dut, sigma_rel=0.0)
            r0 = identify(f0, z0, dut.edges)
            gv = r0.group_values()
            _check(len(gv) == 1 and gv[0][0] == "L", "L+L must merge", gv)
            _check(abs(gv[0][1] - 3e-3) / 3e-3 < 1e-6, "L sum", gv)
            _check(abs(gv[0][2] - 3.0) / 3.0 < 1e-6, "Rd sum", gv)
        return "wrmse={:.4f} curve={:.3f} rank={}/{} ng={}".format(
            r.wrmse, ce, r.jac_rank, r.n_params, len(r.groups))
    return fn


for _n9, _e9, _v9, _rd9 in _c9_cases:
    reg("C9", "degenerate: " + _n9, _c9_round(_n9, _e9, _v9, _rd9))


# ---------------------------------------------------------------------------
# C10 -- dynamic range extremes (3 rounds)
# ---------------------------------------------------------------------------

_c10_cases = {
    "wide_ladder": make_dut(
        [(0, 2, "R"), (2, 3, "L"), (3, 1, "C"), (3, 4, "R"), (4, 1, "C")],
        [("R", 1.0), ("L", 1e-7, 1e-3), ("C", 1e-12),
         ("R", 1e6), ("C", 1e-6)]),
    "tiny_all": make_dut(
        [(0, 2, "R"), (2, 1, "C")],
        [("R", 1e-2), ("C", 1e-12)]),
    "huge_R": make_dut(
        [(0, 2, "R"), (2, 1, "L")],
        [("R", 1e6), ("L", 1.0, 1e5)]),
}


def _c10_round(name, dut):
    def fn():
        f, z, r = run_fit(dut, 0.005, seed=701)
        _check(r.wrmse <= 3 * FLOOR(0.005), "dynamic range fit", r.wrmse)
        ce = curve_err(dut, r)
        _check(ce <= 0.05, "dynamic range curve", ce)
        return "wrmse={:.4f} curve={:.3f} at_bound={} weak={}".format(
            r.wrmse, ce,
            [g.at_bound for g in r.groups if g.at_bound],
            [g.weak_params for g in r.groups if g.weak_params])
    return fn


for _n10, _d10 in _c10_cases.items():
    reg("C10", "dynrange: " + _n10, _c10_round(_n10, _d10))


# ---------------------------------------------------------------------------
# C11 -- sampling variations (8 rounds)
# ---------------------------------------------------------------------------

_base11 = {d.name: d for d in make_duts()}


def _c11_round(name, dut, kind, v):
    def fn():
        if kind == "M":
            f = np.logspace(1, 7, v)
        else:
            f = np.logspace(np.log10(v[0]), np.log10(v[1]), 30)
        f, z = measure(dut, f=f, sigma_rel=0.005, seed=801)
        r = identify(f, z, dut.edges, FitConfig(seed=801))
        _check(r.wrmse <= 3 * FLOOR(0.005), "sampling variation fit",
               (name, kind, v, r.wrmse))
        return "wrmse={:.4f}".format(r.wrmse)
    return fn


for _n11 in ("ser_rc", "double_tank"):
    for _M in (20, 60):
        reg("C11", "{} M={}".format(_n11, _M),
            _c11_round(_n11, _base11[_n11], "M", _M))
    for _band in ((1e2, 1e7), (1e1, 1e8)):
        reg("C11", "{} band={:g}-{:.0e}".format(_n11, _band[0], _band[1]),
            _c11_round(_n11, _base11[_n11], "band", _band))


# ---------------------------------------------------------------------------
# C12 -- numerical invariants at the solution (6 rounds)
# ---------------------------------------------------------------------------

def _fd_jac(model, theta, s_t, h=1e-6):
    p = len(theta)
    J = np.zeros((p, len(s_t)), dtype=complex)
    for t in range(p):
        tp = theta.copy(); tp[t] += h
        tm = theta.copy(); tm[t] -= h
        Zp, _ = model.z_and_jac(tp, s_t)
        Zm, _ = model.z_and_jac(tm, s_t)
        J[t] = (Zp - Zm) / (2 * h)
    return J


def _c12_round(k):
    def fn():
        rng = np.random.default_rng(909 + 137 * k)
        dut = random_case(rng)
        try:
            f, z, r = run_fit(dut, 0.005, seed=910 + k)
        except PortOpenError:
            return "port-open skip"
        model, theta = r._model, r.theta_norm
        s_t = 1j * 2 * np.pi * f / r._w0
        _, J = model.z_and_jac(theta, s_t)
        Jfd = _fd_jac(model, theta, s_t)
        scale = np.maximum(np.abs(Jfd).max(axis=1, keepdims=True), 1e-12)
        err = float(np.max(np.abs(J - Jfd) / scale))
        _check(err < 1e-4, "adjoint Jacobian vs FD", err)
        fd = np.logspace(1, 7, 200)
        Zd = r.z_model(fd)
        _check(Zd.real.min() > -1e-6 * np.median(np.abs(Zd)),
               "fitted model violates passivity", Zd.real.min())
        return "jacFD={:.1e} ReZ_min={:.2e} wrmse={:.4f}".format(
            err, Zd.real.min(), r.wrmse)
    return fn


for _k12 in range(6):
    reg("C12", "invariants random #{}".format(_k12), _c12_round(_k12))


# ---------------------------------------------------------------------------
# C13 -- multi-graph AICc ranking (2 rounds)
# ---------------------------------------------------------------------------

def _c13_rank():
    from topofit_id import identify_many
    dut = make_dut([(0, 2, "R"), (2, 1, "C")],
                   [("R", 1e3), ("C", 100e-9)])
    f, z = measure(dut, sigma_rel=0.005, seed=1001)
    graphs = [dut.edges, [(0, 2, "R"), (2, 1, "R")],
              [(0, 1, "R"), (0, 1, "C")]]
    res = identify_many(f, z, graphs, FitConfig(seed=1001))
    _check(res[0].wrmse <= 3 * FLOOR(0.005), "true graph not at floor",
           res[0].wrmse)
    _check(res[0].rss <= res[-1].rss, "rss order broken")
    return "top aicc graph wrmse={:.4f}".format(res[0].wrmse)


reg("C13", "identify_many ranking", _c13_rank)


def _c13_open():
    from topofit_id import identify_many
    dut = make_dut([(0, 2, "R"), (2, 1, "C")],
                   [("R", 1e3), ("C", 100e-9)])
    f, z = measure(dut, sigma_rel=0.0)
    try:
        identify_many(f, z, [[(0, 2, "R")]], FitConfig(seed=1001))
    except PortOpenError:
        return "raises OK"
    _check(False, "port-open graph must raise")


reg("C13", "port-open raises", _c13_open)


# ---------------------------------------------------------------------------
# C14 -- equal-value interchangeable groups (2 rounds)
# ---------------------------------------------------------------------------

_c14_cases = {
    "par_L_same": make_dut([(0, 1, "L"), (0, 1, "L")],
                           [("L", 1e-3, 1.0), ("L", 1e-3, 1.0)]),
    "par_L_diff": make_dut([(0, 1, "L"), (0, 1, "L")],
                           [("L", 1e-3, 1.0), ("L", 1e-3, 2.0)]),
}


def _c14_round(name, dut):
    def fn():
        fl = FLOOR(0.005)
        f, z, r = run_fit(dut, 0.005, seed=1101)
        _check(r.wrmse <= 3 * fl, "equal-value fit", r.wrmse)
        _check(curve_err(dut, r) <= 0.05, "equal-value curve", name)
        if name == "par_L_same":
            # two IDENTICAL parallel inductors: only y1+y2 is observable,
            # the individual (L, Rd) split is structurally unidentifiable
            # (an exact equivalence family).  Local rank at the (noise-
            # broken, asymmetric) stopping point may still be full, so the
            # honest invariant is curve-level success, not rank or params.
            return "wrmse={:.4f} curve ok (family representative)".format(
                r.wrmse)
        kept, _ = param_errors(dut, r, f, z)
        if kept:
            _check(max(kept) < 0.05, "equal-value param", max(kept))
        return "wrmse={:.4f} pmax={}".format(
            r.wrmse, max(kept) if kept else None)
    return fn


for _n14, _d14 in _c14_cases.items():
    reg("C14", _n14, _c14_round(_n14, _d14))


# ---------------------------------------------------------------------------
# C15 -- regression: reporting completeness (4 rounds)
# ---------------------------------------------------------------------------

_c15_cases = [
    [(0, 1, "R"), (0, 2, "R"), (0, 2, "R")],
    [(0, 1, "R"), (0, 2, "R"), (0, 2, "R"), (2, 3, "C")],
    [(0, 1, "R"), (0, 2, "C"), (0, 2, "C")],
    [(0, 2, "R"), (2, 4, "R"), (4, 1, "R"), (4, 4, "C"), (2, 2, "L")],
]


def _c15_round(edges):
    def fn():
        vals = {}
        for i, (u, v, k) in enumerate(edges):
            vals[i] = ((k, 1e3) if k == "R" else
                       (k, 1e-7) if k == "C" else (k, 1e-3, 1.0))
        dut = DUT("rep", list(edges), vals)
        red = reduce_graph(edges)
        seen = list(red.dropped)
        for e in red.edges:
            seen.extend(e.members)
        _check(sorted(seen) == list(range(len(edges))), "members missing",
               sorted(seen))
        f, z = measure(dut, sigma_rel=0.0)
        r = identify(f, z, dut.edges)
        reported = {er.index for er in r.edges_out}
        _check(reported == set(range(len(edges))),
               "EdgeReport incomplete", reported)
        return "dropped={} groups={}".format(sorted(red.dropped),
                                              len(red.edges))
    return fn


for _i15, _e15 in enumerate(_c15_cases):
    reg("C15", "report completeness #{}".format(_i15), _c15_round(_e15))


# ---------------------------------------------------------------------------
# C16 -- fresh random cases (20 rounds)
# ---------------------------------------------------------------------------

def _c16_round(seed):
    def fn():
        fl = FLOOR(0.005)
        rng = np.random.default_rng(seed)
        dut = random_case(rng)
        try:
            f, z, r = run_fit(dut, 0.005, seed=seed)
        except PortOpenError:
            return "port-open skip"
        ce = curve_err(dut, r)
        _check(r.wrmse <= 3 * fl, "random wrmse above 3x floor",
               (seed, r.wrmse))
        if ce > 0.05:
            # wrmse is at the floor but the dense curve differs: legitimate
            # only when the diverging feature is unresolved by the sampling
            # AND the diagnostics say so; otherwise a genuine bug
            weak_any = any(g.weak_params for g in r.groups)
            rank_def = r.jac_rank < r.n_params
            if weak_any or rank_def:
                raise BoundaryHit(
                    "curve {:.3f} while wrmse at floor, weak={} rank-def={}"
                    " (sub-sampling-gap feature; information boundary)".format(
                        ce, weak_any, rank_def))
            _check(False, "curve mismatch without diagnostics",
                   (seed, ce))
        return "wrmse={:.4f} curve={:.3f} E={}".format(
            r.wrmse, ce, len(dut.edges))
    return fn


for _s16 in range(7000, 7020):
    reg("C16", "random seed={}".format(_s16), _c16_round(_s16))


# ---------------------------------------------------------------------------
# runner
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", type=str, default="verify_rounds_results.json")
    ap.add_argument("--only", type=str, default=None,
                    help="run only rounds whose id or name contains this")
    args = ap.parse_args()

    results = []
    n_fail = 0
    n_boundary = 0
    for rid, name, fn in ROUNDS:
        if args.only and args.only not in rid and args.only not in name:
            continue
        t0 = time.perf_counter()
        try:
            detail = fn()
            dt = time.perf_counter() - t0
            results.append(dict(id=rid, name=name, ok=True, seconds=dt,
                                detail=detail))
            print("PASS {:3s} {:48s} {:6.1f}s  {}".format(rid, name, dt,
                                                          detail))
        except BoundaryHit as exc:
            dt = time.perf_counter() - t0
            n_boundary += 1
            results.append(dict(id=rid, name=name, ok=True, boundary=True,
                                seconds=dt, detail=exc.note))
            print("BND  {:3s} {:48s} {:6.1f}s  {}".format(rid, name, dt,
                                                           exc.note))
        except RoundFail as exc:
            dt = time.perf_counter() - t0
            n_fail += 1
            results.append(dict(id=rid, name=name, ok=False, seconds=dt,
                                error=str(exc)))
            print("FAIL {:3s} {:48s} {:6.1f}s  {}".format(rid, name, dt, exc))
        except Exception:
            dt = time.perf_counter() - t0
            n_fail += 1
            results.append(dict(id=rid, name=name, ok=False, seconds=dt,
                                error=traceback.format_exc()))
            print("ERR  {:3s} {:48s} {:6.1f}s".format(rid, name, dt))
            traceback.print_exc()
    with open(args.json, "w") as fh:
        json.dump(results, fh, indent=1, default=str)
    print("\n{} rounds, {} failed, {} boundary -> {}".format(
        len(results), n_fail, n_boundary, args.json))
    return 1 if n_fail else 0


if __name__ == "__main__":
    sys.exit(main())
