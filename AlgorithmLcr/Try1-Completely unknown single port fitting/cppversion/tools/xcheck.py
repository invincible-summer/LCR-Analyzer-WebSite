#!/usr/bin/env python3
"""Cross-check sweep failures against the Python reference.

Reads a list of "<canonical>#<draw>" case names (one per line), loads the
exact impedance data dumped by the C++ side (tools/../dump tool), runs the
unmodified Python identify() on it and prints the Python verdict for each.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

import numpy as np  # noqa: E402

from rlc_id import Config, identify  # noqa: E402
from rlc_id.circuits import canonical, n_leaves  # noqa: E402


def main():
    case_file = sys.argv[1]
    zdir = sys.argv[2]
    f = np.geomspace(10.0, 10e6, 30)
    for line in open(case_file):
        line = line.strip()
        if not line or "#" not in line:
            continue
        name, draw = line.rsplit("#", 1)
        path = os.path.join(zdir, f"{name.replace('/', '_')}_{draw}.txt")
        zz = np.loadtxt(path)
        z = zz[:, 0] + 1j * zz[:, 1]
        # max_n per sweep protocol
        from rlc_id.library import get_library
        maxn = 4
        for t in get_library(6, 2):
            if canonical(t) == name:
                maxn = max(4, n_leaves(t))
                break
        res = identify(f, z, config=Config(max_n=maxn))
        if not res.classes:
            print(f"{line:40s} PY: no-candidates")
            continue
        rep = res.classes[0].representative
        match = "MATCH" if canonical(rep.tree) == name else "DIFF "
        print(f"{line:40s} PY: top1={canonical(rep.tree):30s} wrmse={rep.wrmse:.3e} {match}")


if __name__ == "__main__":
    main()
