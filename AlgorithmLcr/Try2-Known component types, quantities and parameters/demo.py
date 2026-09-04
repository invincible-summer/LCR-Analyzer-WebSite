"""Demo: identify synthetic DUTs from noisy impedance data.

Usage:
    python demo.py                # named DUTs (E = 2..6) + random DUTs
    python demo.py --stats        # enumeration counts & timing per E
    python demo.py --noiseless    # zero noise (exact recovery expected)
"""

from __future__ import annotations

import argparse
import time

import numpy as np

import netgraph_id as ng
from netgraph_id.adjacency import candidate_to_adjacency
from netgraph_id.synthetic import make_duts, network_str, random_network


def run_stats() -> None:
    print(f"{'E':>2} {'structures':>10} {'candidates':>12} {'enum_s':>8} "
          f"{'assign_s':>9} {'total_s':>8}")
    for E in range(1, 7):
        t0 = time.perf_counter()
        structures = ng.enumerate_structures(E)
        t_enum = time.perf_counter() - t0
        # distinguishable components: alternating types for variety
        kinds = ["R", "C", "L"] * 2
        comps = []
        for i in range(E):
            k = kinds[i % 3]
            comps.append(ng.Component(k, 10.0 ** (i - 4)))
        cs = ng.ComponentSet(tuple(comps))
        t1 = time.perf_counter()
        n = 0
        for st in structures:
            n += sum(1 for _ in ng.iter_assignments(st, cs))
        t_assign = time.perf_counter() - t1
        print(f"{E:>2} {len(structures):>10} {n:>12} {t_enum:>8.2f} "
              f"{t_assign:>9.2f} {t_enum + t_assign:>8.2f}")


def run_duts(noiseless: bool) -> None:
    sigma = 0.0 if noiseless else 0.005
    duts = make_duts()
    n_hit = 0
    for dut in duts:
        f = ng.synthetic.default_frequencies()
        z = dut.z_exact(f) if noiseless else ng.synthetic.measure(
            dut.network, dut.compset, f, sigma_rel=sigma, seed=7)[1]
        res = ng.identify(dut.compset, f, z)
        grid = ng.selector.make_validation_grid(f)
        tol = max(1e-3, 3.0 * (res.best.wrmse if res.best else 1e-3))
        hit = res.best is not None and ng.selector.are_equivalent(
            res.best.representative.network, dut.network, dut.compset,
            grid, tol)
        n_hit += hit
        print(ng.report.format_report(
            f"{dut.name} (E={dut.compset.n}, {'noiseless' if noiseless else '0.5% noise'})",
            res.classes, dut.compset, truth_str=dut.describe()))
        for rank, cl in enumerate(res.classes[:ng.Config().top_k], start=1):
            print(candidate_to_adjacency(cl.representative,
                                         dut.compset).format_block(label=rank))
        print(f"[{'HIT ' if hit else 'MISS'}] candidates={res.n_candidates} "
              f"kept={res.n_funnel_kept} structures={res.n_structures} "
              f"elapsed={res.elapsed:.2f}s "
              f"(funnel={res.timings['funnel']:.2f}s eval={res.timings['eval']:.2f}s "
              f"cluster={res.timings['cluster']:.2f}s)")
        print()
    print(f"== named DUTs: {n_hit}/{len(duts)} recovered ==")

    # random DUTs at E = 4
    rng = np.random.default_rng(20260903)
    cs = ng.ComponentSet.make(n_R=[1e3, 47e3], n_C=[10e-9],
                              n_L=[(330e-6, 3.0)])
    n_hit = 0
    for trial in range(3):
        net = random_network(cs, rng)
        f = ng.synthetic.default_frequencies()
        z = ng.synthetic.measure(net, cs, f, sigma_rel=sigma, seed=100 + trial)[1]
        res = ng.identify(cs, f, z)
        grid = ng.selector.make_validation_grid(f)
        tol = max(1e-3, 3.0 * res.best.wrmse)
        hit = ng.selector.are_equivalent(res.best.representative.network,
                                         net, cs, grid, tol)
        n_hit += hit
        print(f"random#{trial} truth={network_str(net, cs)}")
        print(f"[{'HIT ' if hit else 'MISS'}] best="
              f"{network_str(res.best.representative.network, cs)} "
              f"wRMSE={res.best.wrmse:.2e} elapsed={res.elapsed:.2f}s")
        print(candidate_to_adjacency(res.best.representative,
                                     cs).format_block(label=1))
    print(f"== random E=4 DUTs: {n_hit}/3 recovered ==")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--noiseless", action="store_true")
    args = ap.parse_args()
    if args.stats:
        run_stats()
    else:
        run_duts(args.noiseless)
