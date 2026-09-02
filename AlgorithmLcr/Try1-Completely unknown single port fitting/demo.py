#!/usr/bin/env python
"""End-to-end demo: identify all 12 synthetic DUTs (DESIGN.md section 8.2).

Usage:
    conda run -n lcr python demo.py            # noisy (0.5%) suite
    conda run -n lcr python demo.py --noiseless
    conda run -n lcr python demo.py --dut dut7_tank --verbose
"""

from __future__ import annotations

import argparse
import time

import numpy as np

from rlc_id import Config, identify, synthetic, to_string
from rlc_id.circuits import canonical, n_leaves
from rlc_id.fit_engine_a import Candidate
from rlc_id.report import format_report
from rlc_id.selector import are_equivalent, make_validation_grid


def classify(dut, res, f, equiv_tol):
    """Return (status, detail): EXACT / EQUIV / MISS plus top-1 description."""
    if not res.classes:
        return "MISS", "no candidates"
    best = res.classes[0]
    rep = best.representative
    if canonical(rep.tree) == canonical(dut.tree):
        perr = synthetic.max_param_error(rep.theta, dut)
        return "EXACT", f"param_err={perr:.2e}"
    truth = Candidate(tree=dut.tree, theta=dut.theta, rss=0.0, aicc_val=0.0,
                      wrmse=0.0, max_rel_err=0.0)
    fg = make_validation_grid(f)
    if any(are_equivalent(m, truth, fg, tol=equiv_tol) for m in best.members):
        return "EQUIV", "electrically equivalent realization (T2)"
    return "MISS", f"top1={to_string(rep.tree)}"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--noiseless", action="store_true",
                    help="run with sigma_rel=0 instead of 0.5%%")
    ap.add_argument("--dut", type=str, default=None,
                    help="run only this DUT (substring match)")
    ap.add_argument("--verbose", action="store_true",
                    help="print the full ranked report for each DUT")
    args = ap.parse_args()

    sigma = 0.0 if args.noiseless else synthetic.DEFAULT_SIGMA_REL
    equiv_tol = 1e-6 if args.noiseless else 2e-2

    duts = synthetic.make_duts()
    if args.dut:
        duts = [d for d in duts if args.dut in d.name]
        if not duts:
            print(f"no DUT matches {args.dut!r}")
            return 2

    print(f"rlc_id end-to-end demo | {len(duts)} DUTs | "
          f"sigma_rel={sigma} | band {synthetic.F_MIN:g}..{synthetic.F_MAX:g} Hz"
          f" | {synthetic.N_POINTS} pts")
    print()

    n_exact = n_equiv = n_miss = 0
    t_all = time.time()
    for dut in duts:
        f, z = synthetic.measure(dut, sigma_rel=sigma)
        cfg = Config(max_n=5 if n_leaves(dut.tree) > 4 else 4)
        t0 = time.time()
        res = identify(f, z, config=cfg)
        dt = time.time() - t0
        status, detail = classify(dut, res, f, equiv_tol)
        n_exact += status == "EXACT"
        n_equiv += status == "EQUIV"
        n_miss += status == "MISS"
        rep = res.best.representative if res.classes else None
        print(f"[{status:5s}] {dut.name:<22} ({dt:4.1f}s)")
        print(f"        truth: {dut.describe()}")
        if rep is not None:
            print(f"        top-1: {to_string(rep.tree, rep.theta)}"
                  f"   wRMSE={rep.wrmse:.2e}  [{detail}]")
        if args.verbose or status == "MISS":
            print(format_report(dut.name, res.classes,
                                truth=dut.describe(), top_k=5))
        print()

    dt_all = time.time() - t_all
    print("=" * 72)
    print(f"SUMMARY: exact={n_exact}  equivalent={n_equiv}  miss={n_miss}  "
          f"of {len(duts)}   (total {dt_all:.1f}s)")
    return 0 if n_miss == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
