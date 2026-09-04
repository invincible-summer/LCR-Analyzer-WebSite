#!/usr/bin/env python
"""End-to-end demo: identify all 14 synthetic DUTs (DESIGN.md section 8.2)
or a measurement file (../../INPUT_FORMAT.md section 1).

Usage:
    conda run -n lcr python demo.py            # noisy (0.5%) synthetic suite
    conda run -n lcr python demo.py --noiseless
    conda run -n lcr python demo.py --dut dut7_tank --verbose
    conda run -n lcr python demo.py --measurements measurements.txt \
        [--count count.txt]        # optional exact device-count prior
"""

from __future__ import annotations

import argparse
import time

import numpy as np

from rlc_id import Config, identify, iofmt, synthetic, to_string
from rlc_id.adjacency import candidate_to_adjacency
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


def run_measurements(path: str, exact_n: int | None) -> int:
    """Identify from a measurements.txt file (+ optional count.txt prior)."""
    f, z = iofmt.load_measurements(path)
    cfg = Config(max_n=5, exact_n=exact_n)
    t0 = time.time()
    res = identify(f, z, config=cfg)
    dt = time.time() - t0
    prior = f"exact_n={exact_n}" if exact_n is not None else "free search"
    print(f"rlc_id input mode | {path} | {len(f)} points | {prior} | {dt:.1f}s")
    print()
    if res.classes:
        rep = res.best.representative
        print(f"top-1: {to_string(rep.tree, rep.theta)}"
              f"   wRMSE={rep.wrmse:.2e}  ({n_leaves(rep.tree)} devices)")
        for line in candidate_to_adjacency(rep).format_block(label=1).splitlines():
            print(line)
        print()
        print(format_report(path, res.classes, top_k=5))
        return 0
    print("(no valid candidates)")
    return 1


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--noiseless", action="store_true",
                    help="run with sigma_rel=0 instead of 0.5%%")
    ap.add_argument("--dut", type=str, default=None,
                    help="run only this DUT (substring match)")
    ap.add_argument("--verbose", action="store_true",
                    help="print the full ranked report for each DUT")
    ap.add_argument("--exact-n", type=int, default=None,
                    help="prior: the circuit has exactly N devices "
                         "(L + series DCR = one device)")
    ap.add_argument("--count", type=str, default=None,
                    help="optional count.txt with the exact device count "
                         "(INPUT_FORMAT.md sec 2.1); mutually exclusive "
                         "with --exact-n")
    ap.add_argument("--measurements", type=str, default=None,
                    help="identify from a measurements.txt file instead of "
                         "the synthetic suite")
    args = ap.parse_args()

    exact_n = args.exact_n
    if args.count is not None:
        if exact_n is not None:
            ap.error("--exact-n and --count are mutually exclusive")
        exact_n = iofmt.load_count(args.count)
    if exact_n is not None and exact_n < 1:
        ap.error("--exact-n must be a positive integer")

    if args.measurements is not None:
        return run_measurements(args.measurements, exact_n)

    sigma = 0.0 if args.noiseless else synthetic.DEFAULT_SIGMA_REL
    equiv_tol = 1e-6 if args.noiseless else 2e-2

    duts = synthetic.make_duts()
    if args.dut:
        duts = [d for d in duts if args.dut in d.name]
        if not duts:
            print(f"no DUT matches {args.dut!r}")
            return 2

    prior = f" | exact_n={exact_n}" if exact_n is not None else ""
    print(f"rlc_id end-to-end demo | {len(duts)} DUTs | "
          f"sigma_rel={sigma} | band {synthetic.F_MIN:g}..{synthetic.F_MAX:g} Hz"
          f" | {synthetic.N_POINTS} pts{prior}")
    print()

    n_exact = n_equiv = n_miss = 0
    t_all = time.time()
    for dut in duts:
        f, z = synthetic.measure(dut, sigma_rel=sigma)
        cfg = Config(max_n=5 if n_leaves(dut.tree) > 4 else 4, exact_n=exact_n)
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
            for line in candidate_to_adjacency(rep).format_block(label=1).splitlines():
                print(f"        {line}")
        if args.verbose or status == "MISS":
            print(format_report(dut.name, res.classes,
                                truth=dut.describe(), top_k=5))
            for rank, eq in enumerate(res.classes[:5], start=1):
                for line in candidate_to_adjacency(eq.representative) \
                        .format_block(label=rank).splitlines():
                    print(line)
        print()

    dt_all = time.time() - t_all
    print("=" * 72)
    print(f"SUMMARY: exact={n_exact}  equivalent={n_equiv}  miss={n_miss}  "
          f"of {len(duts)}   (total {dt_all:.1f}s)")
    return 0 if n_miss == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
