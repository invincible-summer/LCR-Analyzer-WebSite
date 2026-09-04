#!/usr/bin/env python3
"""Python reference runner for the Try1 consistency harness (files only,
no change to existing Try1 code).

Usage: python run_py1.py <cases_root> [nprocs]
"""
import json
import os
import sys
import time
from multiprocessing import Pool

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

from rlc_id import Config, identify  # noqa: E402
from rlc_id.iofmt import load_measurements  # noqa: E402


def run_one(case):
    try:
        with open(os.path.join(case, "truth.json")) as fh:
            truth = json.load(fh)
        f, z = load_measurements(os.path.join(case, "measurements.txt"))
        cfg = Config(exact_n=truth["n"]) if truth.get("exact") else Config(max_n=5)
        t0 = time.perf_counter()
        res = identify(f, z, config=cfg)
        dt = time.perf_counter() - t0
        classes = []
        for cl in res.classes[:5]:
            c = cl.representative
            classes.append({
                "canonical": c.canonical,
                "theta": [float(v) for v in c.theta],
                "wrmse": float(c.wrmse), "rss": float(c.rss),
                "aicc": float(c.aicc_val), "n_members": 1 + len(cl.members),
            })
        out = {"classes": classes, "seconds": dt}
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
