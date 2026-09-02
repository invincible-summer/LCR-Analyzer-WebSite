#!/usr/bin/env python3
"""Dump the Python reference identification results as JSON.

Runs the unmodified rlc_id package from the parent AlgorithmLcr directory on
the 12 synthetic DUTs (noiseless + 0.5% noise) and writes one record per run:
status classification (EXACT / EQUIV / MISS), top-1 canonical topology,
fitted log10 parameters, wRMSE, parameter error and runtime.

Usage:  python tools/dump_python.py out.json
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

import numpy as np  # noqa: E402

from rlc_id import Config, identify, synthetic  # noqa: E402
from rlc_id.circuits import canonical, n_leaves  # noqa: E402
from rlc_id.fit_engine_a import Candidate  # noqa: E402
from rlc_id.selector import are_equivalent, make_validation_grid  # noqa: E402


def classify(dut, res, f, equiv_tol):
    if not res.classes:
        return "MISS", None, None
    best = res.classes[0]
    rep = best.representative
    if canonical(rep.tree) == canonical(dut.tree):
        perr = synthetic.max_param_error(rep.theta, dut)
        return "EXACT", perr, rep
    truth = Candidate(tree=dut.tree, theta=dut.theta, rss=0.0, aicc_val=0.0,
                      wrmse=0.0, max_rel_err=0.0)
    fg = make_validation_grid(f)
    for m in best.members:
        if are_equivalent(m, truth, fg, tol=equiv_tol):
            return "EQUIV", None, rep
    return "MISS", None, rep


def main():
    out_path = sys.argv[1]
    records = []
    for dut in synthetic.make_duts():
        for sigma in (0.0, 0.005):
            f, z = synthetic.measure(dut, sigma_rel=sigma)
            cfg = Config(max_n=5 if n_leaves(dut.tree) > 4 else 4)
            t0 = time.perf_counter()
            res = identify(f, z, config=cfg)
            dt = time.perf_counter() - t0
            status, perr, rep = classify(dut, res, f, 1e-6 if sigma == 0 else 2e-2)
            records.append({
                "dut": dut.name,
                "noise": sigma,
                "status": status,
                "top1": canonical(rep.tree) if rep else None,
                "theta": [float(v) for v in rep.theta] if rep else None,
                "wrmse": float(rep.wrmse) if rep else None,
                "param_err": float(perr) if perr is not None else None,
                "seconds": dt,
            })
    with open(out_path, "w") as fh:
        json.dump(records, fh, indent=1)
    print(f"wrote {len(records)} records to {out_path}")


if __name__ == "__main__":
    main()
