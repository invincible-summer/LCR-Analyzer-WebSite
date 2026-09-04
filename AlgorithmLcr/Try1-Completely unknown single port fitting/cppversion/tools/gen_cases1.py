#!/usr/bin/env python3
"""Generate 2000 random Try1 consistency cases.

Random canonical trees from the topology library layers (1..5 devices),
log-uniform theta within sensible bands, noiseless / 0.5% noise, optional
exact-N prior (count.txt).  Both engines load the SAME measurement files.

Usage: python gen_cases1.py <out_root> [n_cases] [seed0]
"""
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

import numpy as np  # noqa: E402

from rlc_id.circuits import n_leaves, param_kinds  # noqa: E402
from rlc_id.iofmt import format_measurements  # noqa: E402
from rlc_id.library import trees_of_size  # noqa: E402
from rlc_id.synthetic import DUT, measure  # noqa: E402

BANDS = {"R": (1.0, 6.0), "L": (-7.0, 0.0), "Rd": (-1.0, 2.0), "C": (-11.0, -7.0)}


def main():
    out_root = sys.argv[1]
    n_cases = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
    seed0 = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    for ci in range(n_cases):
        seed = seed0 + ci
        rng = np.random.default_rng(seed)
        n = int(rng.integers(1, 6))  # 1..5 devices
        trees = trees_of_size(n)
        tree = trees[int(rng.integers(0, len(trees)))]
        kinds = param_kinds(tree)
        values = [10.0 ** rng.uniform(*BANDS[k]) for k in kinds]
        dut = DUT(name=f"rand{n}", group="rand", tree=tree,
                  values=np.asarray(values, dtype=float))
        sigma = 0.0 if rng.random() < 0.4 else 0.005
        f, z = measure(dut, sigma_rel=sigma, seed=seed + 700000)
        use_exact = rng.random() < 0.5
        case = os.path.join(out_root, f"case_{ci:05d}")
        os.makedirs(case, exist_ok=True)
        with open(os.path.join(case, "measurements.txt"), "w") as fh:
            fh.write(format_measurements(f, z))
        if use_exact:
            with open(os.path.join(case, "count.txt"), "w") as fh:
                fh.write(f"{n_leaves(tree)}\n")
        truth = {"seed": seed, "sigma": sigma, "n": n_leaves(tree),
                 "canonical": None, "values": values, "exact": use_exact}
        from rlc_id.circuits import canonical as canon
        truth["canonical"] = canon(tree)
        with open(os.path.join(case, "truth.json"), "w") as fh:
            json.dump(truth, fh)
    print(f"wrote {n_cases} cases under {out_root}")


if __name__ == "__main__":
    main()
