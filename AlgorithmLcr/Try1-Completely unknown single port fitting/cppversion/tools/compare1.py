#!/usr/bin/env python3
"""Compare py_result.json vs cpp_result.json for Try1 cases.

Verdict levels: the same semantics as the Try1 REPORT cross-validation:
counts of EXACT top-1 (identical canonical), EQUIV (top-1 differs but the
two top-1 models are electrically equivalent on the validation grid), and
MISS (divergence; further classified by whether each side recovered the
TRUTH topology).

Usage: python compare1.py <cases_root>
"""
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

import numpy as np  # noqa: E402

from rlc_id.circuits import evaluate_f  # noqa: E402
from rlc_id.iofmt import load_measurements  # noqa: E402
from rlc_id.selector import make_validation_grid  # noqa: E402


def load(path):
    with open(path) as fh:
        return json.load(fh)


class Lib:
    """canonical-string -> tree (built once from the library)."""

    def __init__(self):
        from rlc_id.circuits import canonical
        from rlc_id.library import get_library, trees_of_size
        self.map = {}
        for n in range(1, 6):
            for t in trees_of_size(n):
                self.map.setdefault(canonical(t), t)
        for t in get_library(5):
            self.map.setdefault(canonical(t), t)

    def tree(self, canon_str):
        return self.map[canon_str]


def main():
    root = sys.argv[1]
    cases = sorted(d for d in os.listdir(root) if d.startswith("case_"))
    lib = Lib()
    stats = {"EXACT": 0, "EQUIV": 0, "MISS_PY_BETTER": 0, "MISS_CPP_BETTER": 0,
             "MISS_BOTH_WRONG_DIFFERENT": 0, "ERROR": 0, "THETA_DIFF": 0}
    misses = []
    theta_diffs = []
    for d in cases:
        case = os.path.join(root, d)
        try:
            py = load(os.path.join(case, "py_result.json"))
            cp = load(os.path.join(case, "cpp_result.json"))
            tr = load(os.path.join(case, "truth.json"))
        except Exception:
            stats["ERROR"] += 1
            continue
        if not py["classes"] or not cp["classes"]:
            stats["ERROR"] += 1
            misses.append((d, "empty classes"))
            continue
        p1, c1 = py["classes"][0], cp["classes"][0]
        if p1["canonical"] == c1["canonical"]:
            stats["EXACT"] += 1
            # py-vs-cpp parameter comparison, permutation-aware over
            # interchangeable sibling blocks (same semantics as the Try1
            # REPORT cross-validation compare)
            try:
                from rlc_id.synthetic import DUT as _D, max_param_error
                tree = lib.tree(p1["canonical"])
                ref = _D(name="t", group="t", tree=tree,
                         values=np.power(10.0, np.asarray(p1["theta"])))
                dmax = max_param_error(np.asarray(c1["theta"]), ref)
                tol = 0.05 if tr["sigma"] > 0 else 1e-3
                if dmax > tol:
                    stats["THETA_DIFF"] += 1
                    theta_diffs.append((d, dmax))
            except Exception as exc:
                theta_diffs.append((d, repr(exc)))
            continue
        # different topologies: equivalent on the expanded grid?
        try:
            t1 = lib.tree(p1["canonical"])
            t2 = lib.tree(c1["canonical"])
            f = load_measurements(os.path.join(case, "measurements.txt"))[0]
            grid = make_validation_grid(f)
            z1 = evaluate_f(t1, np.asarray(p1["theta"]), grid)
            z2 = evaluate_f(t2, np.asarray(c1["theta"]), grid)
            rel = float(np.max(np.abs(z1 - z2) / np.maximum(np.abs(z1), 1e-300)))
            tol = max(1e-3, 3.0 * min(p1["wrmse"], c1["wrmse"]))
            if rel < tol:
                stats["EQUIV"] += 1
                continue
        except Exception:
            pass
        # true divergence: which side recovered the truth?
        py_truth = p1["canonical"] == tr["canonical"]
        cp_truth = c1["canonical"] == tr["canonical"]
        if py_truth and not cp_truth:
            stats["MISS_PY_BETTER"] += 1
        elif cp_truth and not py_truth:
            stats["MISS_CPP_BETTER"] += 1
        else:
            stats["MISS_BOTH_WRONG_DIFFERENT"] += 1
        misses.append((d, f"py={p1['canonical'][:40]} cpp={c1['canonical'][:40]} "
                         f"truth={tr['canonical'][:40]}"))
    print(f"cases: {len(cases)}")
    for k, v in stats.items():
        print(f"  {k:26s}: {v}")
    if theta_diffs:
        print(f"theta diffs (first 10): {theta_diffs[:10]}")
    if misses:
        print(f"misses ({len(misses)}, first 15):")
        for d, why in misses[:15]:
            print(f"  {d}: {why}")
    return 0 if stats["ERROR"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
