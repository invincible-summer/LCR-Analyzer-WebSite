#!/usr/bin/env python3
"""Generate 2000 random Try3 consistency cases.

Each case: a random connected multigraph (Pruefer tree + 0..3 extra edges,
kinds uniform, values log-uniform per topofit_id.synthetic.RANDOM_RANGES),
measured noiseless or with 0.5% noise, written in the unified text formats
(measurements.txt + topology.txt, INPUT_FORMAT.md sec.1/2.3) plus truth.json.
Both engines load the SAME files.

Usage: python gen_cases.py <out_root> [n_cases] [seed0]
"""
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

import numpy as np  # noqa: E402

from topofit_id.iofmt import format_measurements, format_topology  # noqa: E402
from topofit_id.synthetic import RANDOM_RANGES, default_frequencies, measure  # noqa: E402


def random_case(rng):
    V = int(rng.integers(2, 7))
    code = rng.integers(0, V, size=V - 2) if V > 2 else np.array([], dtype=int)
    degree = np.ones(V, dtype=int)
    for c in code:
        degree[c] += 1
    import heapq
    leaves = [n for n in range(V) if degree[n] == 1]
    heapq.heapify(leaves)
    edges = []
    for c in code:
        leaf = heapq.heappop(leaves)
        edges.append((min(leaf, int(c)), max(leaf, int(c))))
        degree[leaf] -= 1
        degree[c] -= 1
        if degree[c] == 1:
            heapq.heappush(leaves, int(c))
    a, b = heapq.heappop(leaves), heapq.heappop(leaves)
    edges.append((min(a, b), max(a, b)))
    n_extra = int(rng.integers(0, 4))
    for _ in range(n_extra):
        u, v = rng.integers(0, V, size=2)
        if u == v:
            continue
        edges.append((min(u, v), max(u, v)))
    kinds, values = [], {}
    for i, (u, v) in enumerate(edges):
        k = ("R", "C", "L")[int(rng.integers(0, 3))]
        kinds.append(k)
        if k == "R":
            values[i] = (k, float(10 ** rng.uniform(*RANDOM_RANGES["R"])))
        elif k == "C":
            values[i] = (k, float(10 ** rng.uniform(*RANDOM_RANGES["C"])))
        else:
            values[i] = (k, float(10 ** rng.uniform(*RANDOM_RANGES["L"])),
                         float(10 ** rng.uniform(*RANDOM_RANGES["Rd"])))
    edge_list = [(u, v, k) for (u, v), k in zip(edges, kinds)]
    return edge_list, values


def main():
    out_root = sys.argv[1]
    n_cases = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
    seed0 = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    for ci in range(n_cases):
        seed = seed0 + ci
        rng = np.random.default_rng(seed)
        edge_list, values = random_case(rng)
        sigma = 0.005 if rng.random() < 0.7 else 0.0

        from topofit_id.synthetic import DUT
        dut = DUT(name="rand", edges=list(edge_list), values=values)
        f = default_frequencies()
        _, z = measure(dut, f, sigma_rel=sigma, seed=seed + 900000)

        case = os.path.join(out_root, f"case_{ci:05d}")
        os.makedirs(case, exist_ok=True)
        with open(os.path.join(case, "measurements.txt"), "w") as fh:
            fh.write(format_measurements(f, z))
        with open(os.path.join(case, "topology.txt"), "w") as fh:
            fh.write(format_topology(edge_list))
        truth = {
            "seed": seed, "sigma": sigma,
            "edges": [[int(u), int(v), k] for u, v, k in edge_list],
            "values": {str(i): list(v) for i, v in values.items()},
        }
        with open(os.path.join(case, "truth.json"), "w") as fh:
            json.dump(truth, fh)
    print(f"wrote {n_cases} cases under {out_root}")


if __name__ == "__main__":
    main()
