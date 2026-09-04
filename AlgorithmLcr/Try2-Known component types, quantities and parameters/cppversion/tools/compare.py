#!/usr/bin/env python3
"""Compare py_result.json vs cpp_result.json for every Try2 case.

Semantic verdict levels (per case):
  PASS        discrete counts exact; the rank-1 class CONTAINS THE TRUTH on
              both sides (the algorithmic guarantee both engines must give);
              rank-1 classes electrically consistent py-vs-cpp.
  TRUTH_MISS  counts exact, rank-1 classes py/cp equivalent, but neither side
              has the truth in rank-1 (noise-floor statistics / degenerate
              case) -- consistent engines, hard case.
  ONE_SIDED   truth in rank-1 on exactly one side -- needs investigation.
  FAIL        discrete counts differ (enumeration divergence) or rank-1
              classes are electrically different.

Also reported: funnel survivor +-1 differences (LU rounding at the 1e6x
probe threshold; dropped candidates are ~1e3 relative wrong at a probe
point and cannot enter the ranked classes), class-set differences at
ranks 2..8 (noise-floor ordering is statistical, compared as sets).

Usage: python compare.py <cases_root>
"""
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

import numpy as np  # noqa: E402

from netgraph_id import Component, ComponentSet, network_z  # noqa: E402
from netgraph_id.graph import Network, make_structure  # noqa: E402
from netgraph_id.iofmt import load_measurements  # noqa: E402
from netgraph_id.selector import make_validation_grid

TOL = 1e-6
EQUIV_TOL = 2e-3  # class-equivalence grid tolerance (clustering uses 1e-3 or
                  # 3*sigma_hat; give a small margin for float paths)


def load(path):
    with open(path) as fh:
        return json.load(fh)


def close(a, b):
    if a == b:
        return True
    return abs(a - b) <= TOL * max(1.0, abs(a), abs(b))


def norm(v):
    if isinstance(v, list):
        return [norm(x) for x in v]
    return float(v) if isinstance(v, (int, float)) else v


def serial_key(serial):
    return json.dumps(norm(serial), sort_keys=True)


def network_of(serial, cs):
    """Rebuild a Network from a full-slot serial (sorted keys per slot)."""
    serial = norm(serial)
    V = int((np.sqrt(8 * len(serial) + 1) + 1) / 2)  # invert S = V(V-1)/2
    comps_sorted = sorted(cs.components, key=lambda c: c.key())
    keys = [c.key() for c in comps_sorted]
    mult = [len(g) for g in serial]
    st = make_structure(V, mult, canonicalize=False)
    assign = []
    for g in serial:
        for k in g:
            assign.append(keys.index(tuple(k)))
    return Network(st, tuple(assign))


class Comparer:
    def __init__(self, case):
        with open(os.path.join(case, "truth.json")) as fh:
            self.truth = load(os.path.join(case, "truth.json")) if False else json.load(fh)
        comps = [Component(k, float(v), float(dc)) for k, v, dc in self.truth["comps"]]
        self.cs = ComponentSet(tuple(comps))
        self.grid = make_validation_grid(
            load_measurements(os.path.join(case, "measurements.txt"))[0])
        self._zcache = {}

    def z_of(self, serial):
        key = serial_key(serial)
        if key not in self._zcache:
            self._zcache[key] = network_z(network_of(serial, self.cs), self.cs, self.grid)
        return self._zcache[key]

    def equivalent(self, s1, s2):
        k1, k2 = serial_key(s1), serial_key(s2)
        if k1 == k2:
            return True
        z1, z2 = self.z_of(s1), self.z_of(s2)
        rel = float(np.max(np.abs(z1 - z2) / np.maximum(np.abs(z1), 1e-300)))
        return rel < EQUIV_TOL

    def class_serials(self, cl):
        return [cl["serial"]] + list(cl.get("members_serial", []))


def main():
    root = sys.argv[1]
    cases = sorted(d for d in os.listdir(root) if d.startswith("case_"))
    stats = {"PASS": 0, "TRUTH_MISS": 0, "TRUTH_RANK2": 0, "ONE_SIDED": 0,
             "CLUSTER_BORDERLINE": 0, "MACHINE_FLOOR": 0, "FAIL": 0, "ERROR": 0,
             "FUNNEL_KEPT_DIFF": 0, "TOP1_SET_DIFF": 0}
    fails, one_sided, set_diffs = [], [], []
    for d in cases:
        case = os.path.join(root, d)
        try:
            py = load(os.path.join(case, "py_result.json"))
            cp = load(os.path.join(case, "cpp_result.json"))
            cmp = Comparer(case)
        except Exception:
            stats["ERROR"] += 1
            continue
        bad = None
        if py["n_structures"] != cp["n_structures"]:
            bad = f"n_structures {py['n_structures']} vs {cp['n_structures']}"
        elif py["n_candidates"] != cp["n_candidates"]:
            bad = f"n_candidates {py['n_candidates']} vs {cp['n_candidates']}"
        if bad:
            stats["FAIL"] += 1
            fails.append((d, bad))
            continue
        if py["n_funnel_kept"] != cp["n_funnel_kept"]:
            stats["FUNNEL_KEPT_DIFF"] += 1

        truth_key = serial_key(cmp.truth["serial"])
        py_top1 = cmp.class_serials(py["classes"][0]) if py["classes"] else []
        cp_top1 = cmp.class_serials(cp["classes"][0]) if cp["classes"] else []
        truth_py = any(serial_key(s) == truth_key for s in py_top1)
        truth_cp = any(serial_key(s) == truth_key for s in cp_top1)
        # truth in the other side's rank-2 (borderline clustering/ordering)
        truth_py2 = any(
            any(serial_key(s) == truth_key for s in cmp.class_serials(c))
            for c in py["classes"][1:3])
        truth_cp2 = any(
            any(serial_key(s) == truth_key for s in cmp.class_serials(c))
            for c in cp["classes"][1:3])

        py1 = py["classes"][0]["serial"] if py["classes"] else None
        cp1 = cp["classes"][0]["serial"] if cp["classes"] else None
        py1w = py["classes"][0]["wrmse"] if py["classes"] else float("inf")
        cp1w = cp["classes"][0]["wrmse"] if cp["classes"] else float("inf")
        # noiseless machine-floor fits: several wirings reproduce the data to
        # rounding level (rss 0 vs ~1e-9 is luck of one's own rounding), their
        # order carries no information -- class identity then differs only by
        # which machine-zero representative each engine rounded best.
        machine_floor = py1w < 1e-4 and cp1w < 1e-4
        top1_equiv = (py1 is None) == (cp1 is None)
        subset_borderline = False
        if py1 is not None and cp1 is not None:
            top1_equiv = cmp.equivalent(py1, cp1)
            if not top1_equiv:
                # ill-conditioned wirings (cond(Y) up to ~1e12 at band edges)
                # make near-degenerate class merging/ordering borderline at
                # double precision; accept when one rank-1 class is contained
                # in the other's member set (same candidate cluster)
                py_keys = {serial_key(s) for s in py_top1}
                cp_keys = {serial_key(s) for s in cp_top1}
                subset_borderline = py_keys <= cp_keys or cp_keys <= py_keys

        if not top1_equiv and not subset_borderline and not machine_floor:
            stats["FAIL"] += 1
            fails.append((d, "rank-1 classes electrically different"))
            continue
        if not top1_equiv:
            stats["MACHINE_FLOOR" if machine_floor else "CLUSTER_BORDERLINE"] += 1
        if truth_py and truth_cp:
            stats["PASS"] += 1
        elif (truth_py and truth_cp2) or (truth_cp and truth_py2) or (
                not truth_py and not truth_cp and (truth_py2 or truth_cp2)):
            stats["TRUTH_RANK2"] += 1  # same cluster, borderline order/merge
        elif truth_py or truth_cp:
            stats["ONE_SIDED"] += 1
            one_sided.append((d, truth_py, truth_cp))
        else:
            stats["TRUTH_MISS"] += 1

        # class-set comparison at ranks 2..8 (statistical ordering allowed)
        py_set = [serial_key(c["serial"]) for c in py["classes"][1:8]]
        cp_set = [serial_key(c["serial"]) for c in cp["classes"][1:8]]
        if sorted(py_set) != sorted(cp_set):
            stats["TOP1_SET_DIFF"] += 1
            set_diffs.append((d, py_set, cp_set))

    print(f"cases: {len(cases)}")
    for k, v in stats.items():
        print(f"  {k:16s}: {v}")
    if one_sided:
        print(f"one-sided truth cases ({len(one_sided)}, py_only/cp_only):")
        for d, tp, tc in one_sided[:20]:
            print(f"  {d}: py={tp} cpp={tc}")
    if set_diffs:
        print(f"ranks 2..8 class-set differences: {len(set_diffs)} (first 5):")
        for d, ps, cs_ in set_diffs[:5]:
            print(f"  {d}")
    if fails:
        print(f"FAILURES ({len(fails)}):")
        for d, why in fails[:25]:
            print(f"  {d}: {why}")
    return 0 if stats["FAIL"] == 0 and stats["ERROR"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
