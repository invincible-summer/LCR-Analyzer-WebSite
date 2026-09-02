#!/usr/bin/env python3
"""Replay the C++ benchmark sweep cases through the Python engine.

Reads /tmp/bench_cases/index.txt ("<canonical> <maxN> <file>") written by
apps/bench_dump.cpp -- the exact same topologies, parameters and frequency
grid the C++ benchmark timed -- and measures the unmodified Python
identify() wall time per case.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

import numpy as np  # noqa: E402

from rlc_id import Config, identify  # noqa: E402
from rlc_id.circuits import canonical  # noqa: E402


def main():
    casedir = sys.argv[1] if len(sys.argv) > 1 else "/tmp/bench_cases"
    f = np.geomspace(10.0, 10e6, 30)
    total = 0.0
    n = 0
    per_case = []
    verdicts = open(os.path.join(casedir, "py_verdict.txt"), "w")
    for line in open(os.path.join(casedir, "index.txt")):
        name, maxn, fname = line.split()
        zz = np.loadtxt(os.path.join(casedir, fname))
        z = zz[:, 0] + 1j * zz[:, 1]
        cfg = Config(max_n=int(maxn))
        t0 = time.perf_counter()
        res = identify(f, z, config=cfg)
        dt = time.perf_counter() - t0
        total += dt
        per_case.append((name, dt))
        n += 1
        top1 = (canonical(res.classes[0].representative.tree)
                if res.classes else "(none)")
        wrmse = (res.classes[0].representative.wrmse
                 if res.classes else -1.0)
        verdicts.write(f"{name} | {top1} | {wrmse!r}\n")
        flag = "ok " if top1 == name else "dif"
        print(f"[{n:3d}] {dt:6.2f}s {flag} {name}", flush=True)
    verdicts.close()
    print(f"\nPython sweep replay: {n} cases, {total:.1f} s total "
          f"({total / max(n, 1) * 1e3:.0f} ms/case)")


if __name__ == "__main__":
    main()
