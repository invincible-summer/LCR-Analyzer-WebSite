"""Try3 demo: known-topology identification on named multigraph DUTs.

Run:  python demo.py            (0.5% noise, Try1/Try2 protocol)
      python demo.py --noiseless
      python demo.py --ranking  (multi-candidate topology ranking)
"""

from __future__ import annotations

import argparse
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from topofit_id import FitConfig, identify, identify_many, reduce_graph
from topofit_id.graph import eval_group
from topofit_id.metric import matched_group_errors
from topofit_id.synthetic import make_duts, measure


def fmt_val(v):
    if v[0] == "R":
        return "R = {:.6g} ohm".format(v[1])
    if v[0] == "C":
        return "C = {:.6g} F".format(v[1])
    return "L = {:.6g} H, Rd = {:.6g} ohm".format(v[1], v[2])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--noiseless", action="store_true")
    ap.add_argument("--ranking", action="store_true")
    args = ap.parse_args()
    sigma = 0.0 if args.noiseless else 0.005

    duts = make_duts()
    if args.ranking:
        dut = next(d for d in duts if d.name == "ind_parasitic")
        f, z = measure(dut, sigma_rel=sigma, seed=3)
        wrong1 = [(0, 2, "R"), (2, 1, "C"), (0, 1, "L")]
        wrong2 = [(0, 2, "R"), (2, 1, "L"), (0, 2, "C")]
        print("ranking demo on ind_parasitic (3 candidate graphs, "
              "sigma_rel={})".format(sigma))
        for rank, r in enumerate(identify_many(f, z, [wrong1, dut.edges, wrong2],
                                               FitConfig(seed=4))):
            print("\n#{} AICc={:.1f} wRMSE={:.4g}".format(rank + 1, r.aicc_val, r.wrmse))
            print(r.describe())
        return

    n_ok = 0
    for dut in duts:
        f, z = measure(dut, sigma_rel=sigma, seed=0)
        r = identify(f, z, dut.edges, FitConfig(seed=1))
        red = reduce_graph(dut.edges)
        tv = [eval_group(g.expr, dut.values) for g in red.edges]
        errs, _ = matched_group_errors(r.group_values(), tv, red.edges)
        weak = {lab for g in r.groups for lab in
                [((min(g.u, g.v), max(g.u, g.v), g.kind), nm) for nm in g.weak_params]}
        # recompute kept errors honoring fit-side weak flags
        from topofit_id.metric import matched_group_errors as mge
        errs, labels = mge(r.group_values(), tv, red.edges)
        kept = [e for e, lab in zip(errs, labels) if lab not in weak]
        status = "OK "
        if r.wrmse > (0.012 if sigma > 0 else 1e-8):
            status = "BAD"
        else:
            n_ok += 1
        print("=" * 72)
        print("[{}] {}   edges={} params={}".format(status, dut.name,
                                                    len(dut.edges), r.n_params))
        for g, want in zip(r.groups, tv):
            marker = ""
            if g.weak_params:
                marker = "  (weak: {})".format(",".join(g.weak_params))
            if g.at_bound:
                marker += "  (at bound: {})".format(",".join(g.at_bound))
            print("  g{} {}".format(g.gid, fmt_val(g.value)) + marker)
            print("      true {}".format(fmt_val(want)))
        if r.jac_rank < r.n_params:
            print("  [!] rank {}/{}: parameter vector jointly unidentifiable;"
                  " Z-curve is still fitted at the noise floor".format(
                      r.jac_rank, r.n_params))
        if kept:
            print("  param err: median {:.2%}  max {:.2%}   wRMSE={:.4g}".format(
                float(np.median(kept)), float(np.max(kept)), r.wrmse))
        for i, why in sorted(r.reduction.dropped.items()):
            print("  edge {} ({}) dropped: {}".format(i, dut.edges[i][2], why))
    print("=" * 72)
    print("{}/{} DUTs fitted at {} level".format(
        n_ok, len(duts), "noise floor" if sigma > 0 else "machine precision"))


if __name__ == "__main__":
    main()
