#!/usr/bin/env python3
"""Compare py_result.json vs cpp_result.json for Try3 cases.

Verdict levels (per case):
  PASS       reduction structurally identical; fit-ok parity (both reach the
             noise floor / machine precision); fitted curves agree py-vs-cpp.
  CURVE_DIFF reduction identical, both fit-ok, but the fitted curves differ
             beyond tolerance (different basin: acceptable when both are at
             the floor and the case is rank-deficient or has weak params,
             else needs investigation).
  FIT_PY_ONLY / FIT_CPP_ONLY   one side failed to reach the floor.
  REDUC_DIFF  reduction structures differ (real port bug).
  RANK_DIFF   jac_rank differs (diagnostic mismatch, counted).

Usage: python compare.py <cases_root>
"""
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

import numpy as np  # noqa: E402

from topofit_id.graph import eval_group, reduce_graph  # noqa: E402
from topofit_id.iofmt import load_measurements, load_topology  # noqa: E402
from topofit_id.nodal import NodalModel  # noqa: E402


def load(path):
    with open(path) as fh:
        return json.load(fh)


def z_of_fit(fit_values, groups, fgrid):
    edges = [(g[0], g[1], g[2]) for g in groups]
    model = NodalModel.from_edges(edges)
    vals = []
    for v in fit_values:
        vals.append(v[1])
        if v[0] == "L":
            vals.append(v[2])
    s = 1j * 2.0 * np.pi * np.asarray(fgrid)
    return model.z_linear(np.asarray(vals), s)


def main():
    root = sys.argv[1]
    cases = sorted(d for d in os.listdir(root) if d.startswith("case_"))
    stats = {"PASS": 0, "CURVE_DIFF": 0, "FIT_PY_ONLY": 0, "FIT_CPP_ONLY": 0,
             "REDUC_DIFF": 0, "RANK_DIFF": 0, "ERROR": 0, "BOTH_MISS": 0}
    fails, curve_diffs = [], []
    for d in cases:
        case = os.path.join(root, d)
        try:
            py = load(os.path.join(case, "py_result.json"))
            cp = load(os.path.join(case, "cpp_result.json"))
            tr = load(os.path.join(case, "truth.json"))
        except Exception:
            stats["ERROR"] += 1
            continue
        # 1. reduction: structural exact match
        pyG = json.dumps(py["groups"], sort_keys=True)
        cpG = json.dumps(cp["groups"], sort_keys=True)
        pyD = json.dumps(py["dropped"], sort_keys=True)
        cpD = json.dumps(cp["dropped"], sort_keys=True)
        if pyG != cpG or pyD != cpD:
            stats["REDUC_DIFF"] += 1
            fails.append((d, "reduction differs"))
            continue
        if py["jac_rank"] != cp["jac_rank"]:
            stats["RANK_DIFF"] += 1  # counted, not fatal
        sigma = tr["sigma"]
        floor = 0.0071 if sigma > 0 else 1e-6
        thr = 3.0 * floor
        py_ok = py["wrmse"] <= thr
        cp_ok = cp["wrmse"] <= thr
        if py_ok and not cp_ok:
            stats["FIT_CPP_ONLY"] += 1  # naming: cpp FAILED
            fails.append((d, f"py fit_ok ({py['wrmse']:.4g}) cpp not ({cp['wrmse']:.4g})"))
            continue
        if cp_ok and not py_ok:
            stats["FIT_PY_ONLY"] += 1  # naming: py FAILED
            fails.append((d, f"cpp fit_ok ({cp['wrmse']:.4g}) py not ({py['wrmse']:.4g})"))
            continue
        if not py_ok and not cp_ok:
            stats["BOTH_MISS"] += 1
            continue
        # both fit-ok: compare fitted curves on the measured band
        f = load_measurements(os.path.join(case, "measurements.txt"))[0]
        zp = z_of_fit(py["fit_values"], py["groups"], f)
        zc = z_of_fit(cp["fit_values"], cp["groups"], f)
        den = np.maximum(np.abs(zp), 0.1 * np.median(np.abs(zp)))
        rel = float(np.max(np.abs(zp - zc) / den))
        # identifiable + no weak params: parameters should agree closely;
        # rank-deficient / weak cases legitimately land on family members
        ident = py["jac_rank"] == py["n_params"] and py["weak"] == 0
        tol = 0.03 if ident else 0.30
        if rel < tol:
            stats["PASS"] += 1
        else:
            stats["CURVE_DIFF"] += 1
            curve_diffs.append((d, rel, ident, py["jac_rank"], py["n_params"], py["weak"]))
    print(f"cases: {len(cases)}")
    for k, v in stats.items():
        print(f"  {k:14s}: {v}")
    if curve_diffs:
        print(f"curve diffs ({len(curve_diffs)}, first 10):")
        for d, rel, ident, rk, np_, wk in curve_diffs[:10]:
            print(f"  {d}: rel={rel:.3g} identifiable={ident} rank={rk}/{np_} weak={wk}")
    if fails:
        print(f"FAILURES ({len(fails)}):")
        for d, why in fails[:25]:
            print(f"  {d}: {why}")
    return 0 if stats["REDUC_DIFF"] == 0 and stats["FIT_CPP_ONLY"] == 0 and \
        stats["FIT_PY_ONLY"] == 0 and stats["ERROR"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
