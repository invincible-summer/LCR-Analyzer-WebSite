#!/usr/bin/env python3
"""Generate 2000 random Try2 consistency cases.

Each case: random component multiset (>=1 storage element, occasional
duplicates), a random admissible wiring (netgraph_id.synthetic.random_network),
measurements under noiseless / 0.5% noise, written to the unified text formats
(measurements.txt + components.txt, INPUT_FORMAT.md sec.1/2.2) plus truth.json
for the comparator.  Both the Python engine and the C++ port load the SAME
files, so numeric inputs are bit-identical.

Usage: python gen_cases.py <out_root> [n_cases] [seed0]
"""
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

import numpy as np  # noqa: E402

from netgraph_id import Component, ComponentSet  # noqa: E402
from netgraph_id.iofmt import format_components, format_measurements  # noqa: E402
from netgraph_id.synthetic import measure, random_network  # noqa: E402


def random_compset(rng):
    e_budget = 6 if rng.random() < 0.10 else int(rng.integers(2, 6))  # E in 2..5/6
    comps = []
    for _ in range(e_budget):
        k = "RCL"[int(rng.integers(0, 3))]
        if k == "R":
            comps.append(Component("R", float(10 ** rng.uniform(1, 6))))
        elif k == "C":
            comps.append(Component("C", float(10 ** rng.uniform(-10, -5))))
        else:
            dcr = 0.0 if rng.random() < 0.3 else float(10 ** rng.uniform(-2, 2))
            comps.append(Component("L", float(10 ** rng.uniform(-6, 0)), dcr))
    # A4: at least one storage element
    if not any(c.kind in "LC" for c in comps):
        comps[int(rng.integers(0, len(comps)))] = Component(
            "C", float(10 ** rng.uniform(-9, -6)))
    # duplicates exercise the orbit collapse (10%)
    if rng.random() < 0.10 and len(comps) >= 2:
        i = int(rng.integers(0, len(comps)))
        j = int(rng.integers(0, len(comps)))
        comps[i] = comps[j]
    return ComponentSet(tuple(comps))


def main():
    out_root = sys.argv[1]
    n_cases = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
    seed0 = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    for ci in range(n_cases):
        seed = seed0 + ci
        rng = np.random.default_rng(seed)
        cs = random_compset(rng)
        net = random_network(cs, rng)
        sigma = 0.0 if rng.random() < 0.5 else 0.005
        f, z = measure(net, cs, sigma_rel=sigma, seed=seed + 500000)
        case = os.path.join(out_root, f"case_{ci:05d}")
        os.makedirs(case, exist_ok=True)
        with open(os.path.join(case, "measurements.txt"), "w") as fh:
            fh.write(format_measurements(f, z))
        with open(os.path.join(case, "components.txt"), "w") as fh:
            fh.write(format_components(cs))
        truth = {
            "seed": seed, "sigma": sigma,
            "V": net.structure.V, "mult": list(net.structure.mult),
            "assign": list(net.assign),
            "serial": [list(g) for g in net.serialize([c.key() for c in cs.components])],
            "comps": [[c.kind, c.value, c.dcr] for c in cs.components],
        }
        with open(os.path.join(case, "truth.json"), "w") as fh:
            json.dump(truth, fh)
    print(f"wrote {n_cases} cases under {out_root}")


if __name__ == "__main__":
    main()
