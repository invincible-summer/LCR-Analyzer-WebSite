#!/usr/bin/env python3
"""Python reference runner for the Try3 consistency harness.

Usage: python run_py.py <cases_root> [nprocs]
"""
import json
import os
import sys
import time
from multiprocessing import Pool

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

from topofit_id import FitConfig, identify  # noqa: E402
from topofit_id.iofmt import load_measurements, load_topology  # noqa: E402


def run_one(case):
    try:
        f, z = load_measurements(os.path.join(case, "measurements.txt"))
        edges = load_topology(os.path.join(case, "topology.txt"))
        t0 = time.perf_counter()
        r = identify(f, z, edges, FitConfig(n_starts=16, n_center=16))
        dt = time.perf_counter() - t0
        out = {
            "n_groups": len(r.reduction.edges),
            "n_passes": r.reduction.n_passes,
            "groups": [[e.u, e.v, e.kind, list(e.members),
                        ("e" if e.expr[0] == "e" else e.expr[0])]
                       for e in r.reduction.edges],
            "dropped": [[i, why] for i, why in sorted(r.reduction.dropped.items())],
            "rss": float(r.rss), "wrmse": float(r.wrmse),
            "max_rel": float(r.max_rel), "aicc": float(r.aicc_val),
            "n_params": int(r.n_params), "n_starts_used": int(r.n_starts_used),
            "jac_rank": int(r.jac_rank), "jac_cond": float(r.jac_cond),
            "fit_values": [[g.value[0], float(g.value[1])] +
                           ([float(g.value[2])] if g.kind == "L" else [])
                           for g in r.groups],
            "weak": sum(len(g.weak_params) for g in r.groups),
            "at_bound": sum(len(g.at_bound) for g in r.groups),
            "theta_norm": [float(v) for v in r.theta_norm],
            "seconds": dt,
        }
        with open(os.path.join(case, "py_result.json"), "w") as fh:
            json.dump(out, fh)
        return None
    except Exception as exc:  # noqa: BLE001
        return {"case": case, "error": repr(exc)}


def main():
    root = sys.argv[1]
    nprocs = int(sys.argv[2]) if len(sys.argv) > 2 else 18
    cases = sorted(
        os.path.join(root, d) for d in os.listdir(root)
        if d.startswith("case_") and os.path.isdir(os.path.join(root, d)))
    with Pool(nprocs) as pool:
        errors = [e for e in pool.map(run_one, cases) if e]
    for e in errors:
        print("ERROR", e["case"], e["error"])
    print(f"py runner: {len(cases)} cases, {len(errors)} errors")


if __name__ == "__main__":
    main()
