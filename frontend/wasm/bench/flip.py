#!/usr/bin/env python3
"""flip.py — compare two suite result files, list try1 flips with detail."""
import json
import math
import sys

a = json.load(open(sys.argv[1]))  # old (good)
b = json.load(open(sys.argv[2]))  # new
amap = {r['id']: r for r in a}


def sig_est(f, z):
    """replica of the 5-point quadratic estimator on sorted-by-f data"""
    order = sorted(range(len(f)), key=lambda i: f[i])

    def robust(v):
        n = len(v)
        s2 = []
        p1 = [-2, -1, 0, 1, 2]
        p2 = [2, -1, -2, -1, 2]
        for i in range(2, n - 2):
            w = v[i - 2:i + 3]
            yy = sum(x * x for x in w)
            a0 = sum(w)
            a1 = sum(x * p for x, p in zip(w, p1))
            a2 = sum(x * p for x, p in zip(w, p2))
            s2.append((yy - a0 * a0 / 5 - a1 * a1 / 10 - a2 * a2 / 14) / 2)
        if len(s2) < 3:
            return 0.0
        s2.sort()
        med = s2[len(s2) // 2]
        return math.sqrt(med) if med > 0 else 0.0

    y = [math.log(abs(complex(z[i][0], z[i][1]))) for i in order]
    ph = [math.atan2(z[i][1], z[i][0]) for i in order]
    return math.hypot(robust(y), robust(ph))


flips_down = [r for r in b if r['try1'].get('rank') and
              amap.get(r['id'], {}).get('try1', {}).get('rank') in (1,) and r['try1']['rank'] != 1]
flips_up = [r for r in b if r['try1'].get('rank') == 1 and
            amap.get(r['id'], {}).get('try1', {}).get('rank') not in (1, None)]
print(f'flips 1->X: {len(flips_down)}   X->1: {len(flips_up)}')
for r in flips_down[:12]:
    o = amap[r['id']]
    print(f"  id={r['id']} sigma={r['sigma']} ndev={r['n_dev']} "
          f"old_rank={o['try1']['rank']} new_rank={r['try1']['rank']} "
          f"new_wrmse1={r['try1'].get('wrmse1'):.4g}")
