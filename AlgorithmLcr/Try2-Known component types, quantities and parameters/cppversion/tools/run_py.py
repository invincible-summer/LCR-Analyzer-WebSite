#!/usr/bin/env python3
"""Python reference runner for the Try2 consistency harness.

Loads each case through netgraph_id.iofmt (the exact INPUT_FORMAT.md
contract), runs identify() and writes py_result.json with the same schema as
the C++ case_run app.  Multiprocessing over cases.

Usage: python run_py.py <cases_root> [nprocs]
"""
import json
import os
import sys
import time
from multiprocessing import Pool

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

from netgraph_id import Config, identify  # noqa: E402
from netgraph_id.iofmt import load_components, load_measurements  # noqa: E402


def run_one(case):
    try:
        f, z = load_measurements(os.path.join(case, "measurements.txt"))
        cs = load_components(os.path.join(case, "components.txt"))
        t0 = time.perf_counter()
        res = identify(cs, f, z, config=Config())
        dt = time.perf_counter() - t0
        classes = []
        comps = cs.components
        for cl in res.classes[:8]:
            c = cl.representative
            classes.append({
                "V": c.network.structure.V,
                "serial": [[list(k) for k in g] for g in
                           c.network.serialize([x.key() for x in comps])],
                "members_serial": [
                    [[list(k) for k in g] for g in
                     m.network.serialize([x.key() for x in comps])]
                    for m in cl.members],
                "rss": float(c.rss), "wrmse": float(c.wrmse),
                "max_rel": float(c.max_rel_err), "sp": bool(c.sp),
                "n_members": int(cl.n_members),
                "adjacency": None,
            })
        out = {
            "n_structures": int(res.n_structures),
            "n_candidates": int(res.n_candidates),
            "n_funnel_kept": int(res.n_funnel_kept),
            "classes": classes, "seconds": dt,
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
