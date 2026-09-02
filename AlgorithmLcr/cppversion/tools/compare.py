"""Compare the JSON dumps produced by apps/dump.cpp and tools/dump_python.py.

Usage (from cppversion/, python >= 3.8, no numpy needed):
    python tools/compare.py cpp_results.json py_results.json

For each (dut, noise) case the two engines were fed bit-identical input data,
so on success the outputs should agree to optimizer tolerance.  The report
groups disagreements by kind so a porting bug is distinguishable from a
benign tie (same topology, slightly different theta on a flat valley).
"""

import json
import sys


def load(path):
    with open(path) as fh:
        return {(r["dut"], r["noise"]): r for r in json.load(fh)}


def rel(a, b):
    if a == b:
        return 0.0
    m = max(abs(a), abs(b))
    return abs(a - b) / m if m > 0 else 0.0


def theta_gap(a, b):
    """Absolute log10-space distance between two fitted parameters.

    theta entries are log10(component value), so an absolute gap of 0.01
    means the component values differ by ~2.3%.  Absolute (not relative)
    distance is required: a parameter whose true value is log10(R) = 0
    returns machine-zero noise from both engines and a ratio between two
    machine zeros is meaningless.
    """
    return abs(a - b)


def main():
    cpp = load(sys.argv[1])
    py = load(sys.argv[2])
    keys = sorted(set(cpp) & set(py))
    if len(cpp) != len(py) or len(keys) != len(cpp):
        print(f"WARNING: key mismatch cpp={len(cpp)} py={len(py)} shared={len(keys)}")

    same_status = 0
    same_top = 0
    theta_close = 0
    disagreements = []
    speed_cpp = speed_py = 0.0

    for k in keys:
        c, p = cpp[k], py[k]
        speed_cpp += c["seconds"]
        speed_py += p["seconds"]
        st = c["status"] == p["status"]
        tp = c["top1"] == p["top1"]
        if st:
            same_status += 1
        if tp:
            same_top += 1
        th_ok = True
        if tp and len(c["theta"]) == len(p["theta"]) and c["theta"]:
            worst = max(
                theta_gap(a, b) for a, b in zip(c["theta"], p["theta"])
            )
            # 0.005 in log10 = 1.2% component difference; noiseless runs
            # land near 1e-11, noisy runs differ by optimizer path only.
            th_ok = worst < 5e-3
            if th_ok:
                theta_close += 1
        if not (st and tp and th_ok):
            disagreements.append((k, c, p))

    n = len(keys)
    print(f"cases: {n}")
    print(f"status identical : {same_status:3d}/{n}")
    print(f"top-1 identical  : {same_top:3d}/{n}")
    print(f"theta |dlog10|<5e-3: {theta_close:3d}/{n}  (among identical topologies)")
    print(f"total time cpp={speed_cpp:.2f}s  python={speed_py:.2f}s  "
          f"speedup={speed_py / max(speed_cpp, 1e-9):.1f}x")

    if disagreements:
        print("\n--- disagreements ---")
        for (name, noise), c, p in disagreements:
            tag = []
            if c["status"] != p["status"]:
                tag.append(f"status {c['status']}(cpp) vs {p['status']}(py)")
            if c["top1"] != p["top1"]:
                tag.append("topology differs")
            elif not (len(c["theta"]) == len(p["theta"]) and c["theta"]):
                tag.append("theta length differs / empty")
            else:
                worst = max(
                    theta_gap(a, b) for a, b in zip(c["theta"], p["theta"])
                )
                tag.append(f"max theta |dlog10| {worst:.2e}")
            print(f"[{name} noise={noise}] " + "; ".join(tag))
            print(f"    cpp: {c['top1'] or '(none)'}  wrmse={c['wrmse']:.3g}")
            print(f"    py : {p['top1'] or '(none)'}  wrmse={p['wrmse']:.3g}")


if __name__ == "__main__":
    main()
