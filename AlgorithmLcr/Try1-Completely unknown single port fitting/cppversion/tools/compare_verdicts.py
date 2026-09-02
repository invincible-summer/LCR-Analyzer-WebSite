#!/usr/bin/env python3
"""Compare the top-1 verdicts of the C++ and Python engines on the exact
same replayed sweep data (cpp_verdict.txt vs py_verdict.txt, both written
as "truth | top1 | wrmse" lines)."""
import sys


def load(path):
    rows = []
    for line in open(path):
        name, top1, wrmse = [p.strip() for p in line.split("|")]
        rows.append((name, top1, float(wrmse)))
    return rows


def main():
    cpp = load(sys.argv[1])
    py = load(sys.argv[2])
    assert len(cpp) == len(py), (len(cpp), len(py))
    same_top = same_truth = close_wrmse = 0
    diffs = []
    for (cn, ct, cw), (pn, pt, pw) in zip(cpp, py):
        assert cn == pn
        if ct == pt:
            same_top += 1
        else:
            diffs.append((cn, ct, cw, pt, pw))
        if ct == cn:
            same_truth += 1
        if abs(cw - pw) <= 0.05 * max(abs(cw), abs(pw), 1e-12):
            close_wrmse += 1
    n = len(cpp)
    print(f"cases: {n}")
    print(f"cpp top-1 == py top-1 : {same_top}/{n}")
    print(f"top-1 == truth (either): {same_truth}/{n}")
    print(f"wrmse within 5%       : {close_wrmse}/{n}")
    for cn, ct, cw, pt, pw in diffs:
        print(f"DIFF [{cn}]\n  cpp: {ct}  wrmse={cw:.3e}\n  py : {pt}  wrmse={pw:.3e}")


if __name__ == "__main__":
    main()
