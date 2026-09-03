"""Random-DUT campaign for Try3 (DESIGN.md sec.8).

N independent random multigraph DUTs (random labelled tree + 0..3 extra
parallel/cross edges, V in 2..6, kinds uniform, values log-uniform in
physical ranges), measured with the Try1/Try2 protocol (30 log-spaced
points 10 Hz..10 MHz, 0.5% relative complex Gaussian noise), then
identified with the known true topology and per-edge kinds.

Per case we record:
  fit_ok      wRMSE <= 3x noise floor (optimizer reached the floor)
  curve_ok    floored dense-grid curve error vs truth <= 0.05
  param stats matched-group relative errors, excluding params that are
              band-invisible *at the true values* (benchmark privilege)
  rank/cond   Jacobian rank/condition at the solution
  seconds     wall time

Usage: python run_campaign.py --n 2000 --jobs 20 [--seed 12345]
"""

from __future__ import annotations

import argparse
import json
import multiprocessing as mp
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from topofit_id import FitConfig, identify, reduce_graph
from topofit_id.graph import eval_group
from topofit_id.metric import (curve_max_rel_floored, matched_group_errors)
from topofit_id.nodal import model_from_reduced
from topofit_id.synthetic import (DEFAULT_SIGMA_REL, measure, random_case)

FLOOR = float(np.sqrt(2.0) * DEFAULT_SIGMA_REL)   # E[wRMSE] for a perfect fit
CURVE_TOL = 0.05
VIS = 0.1                                          # elasticity visibility cut

_G = {}


def _init(sigma):
    _G["sigma"] = sigma


def run_case(seed: int) -> dict:
    rng = np.random.default_rng(seed)
    dut = random_case(rng)
    f, z = measure(dut, sigma_rel=_G["sigma"], seed=seed * 2 + 1)
    t0 = time.perf_counter()
    try:
        r = identify(f, z, dut.edges, FitConfig(seed=seed))
    except Exception as exc:                       # defensive: record & move on
        return dict(seed=seed, error=repr(exc), edges=len(dut.edges))
    seconds = time.perf_counter() - t0

    red = r.reduction
    true_vals = [eval_group(g.expr, dut.values) for g in red.edges]
    errs, labels = matched_group_errors(r.group_values(), true_vals, red.edges)

    # ground-truth visibility: elasticity at the true parameters
    model = model_from_reduced(red)
    flat_true = []
    for v in true_vals:
        flat_true.extend(v[1:])
    w = 2.0 * np.pi * f
    w0 = float(np.exp(np.mean(np.log(w))))
    z0 = float(np.exp(np.mean(np.log(np.abs(z)))))
    scales = []
    for (u, v, k) in [(g.u, g.v, g.kind) for g in red.edges]:
        if k == "R":
            scales.append(z0)
        elif k == "C":
            scales.append(1.0 / (z0 * w0))
        else:
            scales.append(z0 / w0)
            scales.append(z0)
    theta_true = np.log10(np.asarray(flat_true) / np.asarray(scales))
    E = model.elasticity(theta_true, 1j * w / w0)
    max_el = np.max(np.abs(E), axis=1)
    # ground-truth visibility per parameter, keyed like matched labels
    invisible = set()
    t = 0
    for g in red.edges:
        key = (min(g.u, g.v), max(g.u, g.v), g.kind)
        for nm in (["v"] if g.kind != "L" else ["v", "Rd"]):
            if max_el[t] < VIS:
                invisible.add((key, nm))
            t += 1

    kept = [e for e, lab in zip(errs, labels) if lab not in invisible]

    fd = np.logspace(1, 7, 100)
    curve = curve_max_rel_floored(dut.z_exact(fd), r.z_model(fd))

    return dict(
        seed=seed, edges=len(dut.edges),
        nodes=len({n for e in dut.edges for n in e[:2]}),
        n_params=r.n_params, rank=r.jac_rank, cond=float(r.jac_cond),
        wrmse=float(r.wrmse), curve=float(curve),
        seconds=float(seconds), starts=r.n_starts_used,
        n_groups=red.n_groups,
        n_dropped=len(red.dropped),
        n_invisible=len(invisible),
        param_err=[float(e) for e in errs],
        param_err_kept=[float(e) for e in kept],
        error=None,
    )


def _pct(x, q):
    return float(np.percentile(np.asarray(x), q)) if len(x) else float("nan")


def aggregate(rows, sigma):
    ok = [r for r in rows if not r.get("error")]
    errs = [r for r in rows if r.get("error")]
    floor = float(np.sqrt(2.0) * sigma)
    fit_ok = [r for r in ok if r["wrmse"] <= 3.0 * floor]
    ident = [r for r in ok if r["rank"] == r["n_params"]]
    degen = [r for r in ok if r["rank"] < r["n_params"]]
    curve_ok = [r for r in ok if r["curve"] <= CURVE_TOL]
    all_kept = [e for r in ok for e in r["param_err_kept"]]
    well_cond = [r for r in ident if r["cond"] <= 1e4]
    wc_kept = [e for r in well_cond for e in r["param_err_kept"]]
    ill_cond = [r for r in ident if r["cond"] > 1e4]
    secs = [r["seconds"] for r in ok]

    def rate(num, den):
        return (len(num) / len(den)) if den else float("nan")

    summary = {
        "n_total": len(rows),
        "n_errors": len(errs),
        "fit_ok_rate": rate(fit_ok, ok),
        "identifiable_share": rate(ident, ok),
        "curve_ok_rate": rate(curve_ok, ok),
        "curve_ok_identifiable": rate(
            [r for r in ident if r["curve"] <= CURVE_TOL], ident),
        "curve_ok_degenerate": rate(
            [r for r in degen if r["curve"] <= CURVE_TOL], degen),
        "degen_curve_pct": {q: _pct([r["curve"] for r in degen], q)
                            for q in (50, 90)},
        "wrmse_pct": {q: _pct([r["wrmse"] for r in ok], q) for q in (50, 90, 99)},
        "curve_pct": {q: _pct([r["curve"] for r in ok], q) for q in (50, 90, 99)},
        "params": {
            "n_kept": len(all_kept),
            "median": _pct(all_kept, 50), "p90": _pct(all_kept, 90),
            "p99": _pct(all_kept, 99),
            "le_5pct_rate": rate([e for e in all_kept if e <= 0.05], all_kept),
            "le_20pct_rate": rate([e for e in all_kept if e <= 0.20], all_kept),
        },
        "identifiable": {
            "n_cases": len(ident),
            "param_median": _pct([e for r in ident for e in r["param_err_kept"]], 50),
            "param_p90": _pct([e for r in ident for e in r["param_err_kept"]], 90),
            "param_p99": _pct([e for r in ident for e in r["param_err_kept"]], 99),
        },
        "well_conditioned": {
            "n_cases": len(well_cond),
            "share": rate(well_cond, ok),
            "param_median": _pct(wc_kept, 50), "param_p90": _pct(wc_kept, 90),
            "param_p99": _pct(wc_kept, 99), "param_max": _pct(wc_kept, 100),
        },
        "ill_conditioned": {
            "n_cases": len(ill_cond),
            "share": rate(ill_cond, ok),
            "param_median": _pct([e for r in ill_cond for e in r["param_err_kept"]], 50),
        },
        "seconds": {"median": _pct(secs, 50), "p90": _pct(secs, 90),
                    "p99": _pct(secs, 99), "total": float(np.sum(secs))},
        "edges_pct": {q: _pct([r["edges"] for r in ok], q) for q in (50, 90, 100)},
        "params_pct": {q: _pct([r["n_params"] for r in ok], q) for q in (50, 90, 100)},
        "starts_pct": {q: _pct([r["starts"] for r in ok], q) for q in (50, 100)},
    }
    return summary


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--n", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=12345)
    ap.add_argument("--sigma", type=float, default=DEFAULT_SIGMA_REL)
    ap.add_argument("--jobs", type=int, default=max(1, os.cpu_count() - 2))
    ap.add_argument("--out", type=str, default="campaign_results.json")
    args = ap.parse_args()

    seeds = [args.seed + i for i in range(args.n)]
    t0 = time.perf_counter()
    if args.jobs > 1:
        with mp.Pool(args.jobs, initializer=_init, initargs=(args.sigma,)) as pool:
            rows = pool.map(run_case, seeds, chunksize=4)
    else:
        _init(args.sigma)
        rows = [run_case(s) for s in seeds]
    wall = time.perf_counter() - t0

    summary = aggregate(rows, args.sigma)
    summary["wall_seconds"] = wall
    summary["seed"] = args.seed
    summary["sigma"] = args.sigma

    with open(args.out, "w") as fh:
        json.dump({"summary": summary, "rows": rows}, fh, indent=1)

    print(json.dumps(summary, indent=1))
    worst = sorted((r for r in rows if not r.get("error")),
                   key=lambda r: -r["wrmse"])[:10]
    print("\nworst wRMSE cases (seed, wrmse, curve, edges, params):")
    for r in worst:
        print("  seed={} wrmse={:.4g} curve={:.4g} E={} p={}".format(
            r["seed"], r["wrmse"], r["curve"], r["edges"], r["n_params"]))
    if any(r.get("error") for r in rows):
        print("\nhard errors:")
        for r in rows:
            if r.get("error"):
                print("  seed={} {}".format(r["seed"], r["error"]))


if __name__ == "__main__":
    main()
