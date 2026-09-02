#!/usr/bin/env python3
"""Decisive experiment for the 30 'cpp-exact / py-not' benchmark cases:
fit the TRUTH topology directly with Python's own fit_topology +
heuristic_starts on the exact replayed data.  If Python's optimizer
reaches machine precision there, identify()'s miss is a start/selector
issue; if it plateaus near 1e-6, Python's TRF cannot descend this valley
and the C++ LM is simply stronger on this problem class."""
import sys

sys.path.insert(0, "/home/invincible/daily/program/ESP32/LCR/AlgorithmLcr")

import numpy as np  # noqa: E402

from rlc_id.circuits import canonical, make_node  # noqa: E402
from rlc_id.circuits import Leaf  # noqa: E402
from rlc_id.fit_engine_a import (default_weights, fit_topology,  # noqa: E402
                                 heuristic_starts)
from rlc_id.library import get_library

f = np.geomspace(10.0, 10e6, 30)
s = 2j * np.pi * f


def tree_of(name):
    for n in (1, 2, 3, 4, 5, 6):
        for t in get_library(n, 2):
            if canonical(t) == name:
                return t
    raise KeyError(name)


CASES = []
cpp_v = [l.split("|") for l in open("/tmp/bench_cases/cpp_verdict.txt")]
py_v = [l.split("|") for l in open("/tmp/bench_cases/py_verdict.txt")]
for i, ((cn, ct, _), (pn, pt, _)) in enumerate(zip(cpp_v, py_v)):
    if ct.strip() == cn.strip() and pt.strip() != pn.strip():
        CASES.append(i)
print("cpp-exact / py-not cases:", CASES)
lines = list(open("/tmp/bench_cases/index.txt"))
for i in CASES[:5]:
    name, maxn, fname = lines[i].split()
    zz = np.loadtxt("/tmp/bench_cases/" + fname)
    z = zz[:, 0] + 1j * zz[:, 1]
    tree = tree_of(name)
    w = default_weights(z)
    starts = heuristic_starts(tree, None)
    cand = fit_topology(tree, s, z, w, starts)
    if cand is None:
        print(f"case {i:3d} {name:26s} -> no candidate")
        continue
    print(f"case {i:3d} {name:26s} -> direct-fit wrmse={cand.wrmse:.3e}")
