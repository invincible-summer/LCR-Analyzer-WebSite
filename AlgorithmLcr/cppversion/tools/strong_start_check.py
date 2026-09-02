#!/usr/bin/env python3
"""Spot-check: on benchmark-replay cases where only C++ recovered the truth
topology, re-run Python identify with a strong multi-start configuration.
If Python then also recovers the truth, the disagreement is start-lottery,
not a porting difference."""
import os
import sys
import time

sys.path.insert(0, "/home/invincible/daily/program/ESP32/LCR/AlgorithmLcr")

import numpy as np  # noqa: E402

from rlc_id import Config, identify  # noqa: E402
from rlc_id.circuits import canonical  # noqa: E402

CASES = [30, 53, 119]  # cpp exact, py not (from compare_verdicts output)
f = np.geomspace(10.0, 10e6, 30)

idx = {}
for line in open("/tmp/bench_cases/index.txt"):
    name, maxn, fname = line.split()
    idx.setdefault(name, []).append((fname, int(maxn)))

for i in CASES:
    line = list(open("/tmp/bench_cases/index.txt"))[i]
    name, maxn, fname = line.split()
    zz = np.loadtxt(os.path.join("/tmp/bench_cases", fname))
    z = zz[:, 0] + 1j * zz[:, 1]
    cfg = Config(max_n=int(maxn), n_starts_coarse=8, n_starts_refine=40)
    t0 = time.perf_counter()
    res = identify(f, z, config=cfg)
    dt = time.perf_counter() - t0
    top1 = (canonical(res.classes[0].representative.tree)
            if res.classes else "(none)")
    wrmse = (res.classes[0].representative.wrmse
             if res.classes else -1.0)
    tag = "RECOVERED" if top1 == name else "still-diff"
    print(f"case {i:3d} {name:28s} -> {top1:28s} wrmse={wrmse:.2e} "
          f"{tag} ({dt:.1f}s)", flush=True)
