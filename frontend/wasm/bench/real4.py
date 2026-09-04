#!/usr/bin/env python3
"""real4.py — acceptance gate on the four real AUTO SWEEP datasets.

Runs the standard 12 cases (4 datasets x {try1 free, try2, try3}) and prints a
compact table.  Truth structures and reach-able reference wRMSE come from
examples/ans.txt (independent scipy fits).

usage: real4.py [--csv-prefix ../../examples/data]
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from digest import run  # noqa: E402

# scipy-fitted truth structures + parameters (examples/ans.txt) — behavioral
# matching only, canonical-string matching is fragile
W = 2 * math.pi


def z1(w):
    return 1.0 / (1.0 / 9933.61 + 1j * w * 1.02769e-7)


def z2(w):
    return 1.0 / (1.0 / (334.728 + 1j * w * 0.100888) + 1j * w * 9.49327e-7)


def z3(w):
    return (1.0 / (1.0 / (355.033 + 1j * w * 0.105633) + 1j * w * 1.02158e-7)
            + 1.0 / (1j * w * 9.75639e-7))


def z4(w):
    tank = 1.0 / (1.0 / 138.271 + 1.0 / (10.4511 + 1j * w * 8.01974e-4)
                  + 1j * w * 8.93474e-7)
    return 52.416 + tank


TRUTH = {
    1: {'z': z1, 'ref': 9.23e-3, 'rows': 'R:9.9e3:0:1;C:1e-7:0:1',
        'edges': '0 1 R;0 1 C'},
    2: {'z': z2, 'ref': 1.90e-2, 'rows': 'L:0.1:345:1;C:1e-6:0:1',
        'edges': '0 1 L;0 1 C'},
    3: {'z': z3, 'ref': 2.13e-2, 'rows': 'L:0.1:345:1;C:1e-7:0:1;C:1e-6:0:1',
        'edges': '0 2 L;0 2 C;2 1 C'},
    4: {'z': z4, 'ref': 5.04e-3, 'rows': 'R:200:0:1;R:50:0:1;L:820e-6:13:1;C:1e-6:0:1',
        'edges': '0 2 R;0 2 L;0 2 C;2 1 R'},
}


def theory_on(cand, fgrid):
    th = cand.get('theory') or {}
    ff, re, im = th.get('f'), th.get('re'), th.get('im')
    if not ff or len(ff) < 2:
        return None
    import bisect
    lg = [math.log10(x) for x in ff]
    out = []
    for x in fgrid:
        lx = math.log10(x)
        j = min(max(bisect.bisect_left(lg, lx), 1), len(lg) - 1)
        t = (lx - lg[j - 1]) / max(lg[j] - lg[j - 1], 1e-300)
        out.append(complex(re[j - 1] + t * (re[j] - re[j - 1]),
                           im[j - 1] + t * (im[j] - im[j - 1])))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--csv-prefix', default='../../examples/data')
    a = ap.parse_args()

    ok = True
    for i in (1, 2, 3, 4):
        csv = f'{a.csv_prefix}{i}.csv'
        t = TRUTH[i]
        print(f'--- data{i} (reachable wRMSE {t["ref"]:.2%}) ---')
        f, re, im = [], [], []
        with open(csv) as fh:
            for line in fh:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                p = line.split(',')
                f.append(float(p[0]))
                re.append(float(p[1]))
                im.append(float(p[2]))
        f.sort()
        zt = [t['z'](W * x) for x in f]

        def wrmse_vs_truth(cand):
            zc = theory_on(cand, f)
            if zc is None:
                return None
            s2 = sum(abs((a - b) / b) ** 2 for a, b in zip(zc, zt)) / len(zt)
            return math.sqrt(s2)

        r1 = run(csv, 'try1', '--topk', '12')
        rank1, w1 = None, None
        if r1.get('ok'):
            for c in r1['candidates']:
                w = wrmse_vs_truth(c)
                if w is not None and w <= 1.35 * t['ref']:
                    rank1, w1 = c['rank'], w
                    break
            top1 = r1['candidates'][0] if r1['candidates'] else {}
            ws = f'{w1:.3%}' if w1 is not None else 'n/a'
            print(f'  try1: truth rank={rank1} wRMSE={ws} | #1 dev={top1.get("devices")}'
                  f' {top1.get("topology", "")[:44]} wRMSE={top1.get("wrmse", -1):.3%}')
            if rank1 != 1:
                ok = False
        else:
            print('  try1 ERROR', r1.get('error'))
            ok = False

        r2 = run(csv, 'try2', '--rows', t['rows'], '--topk', '5')
        if r2.get('ok') and r2['candidates']:
            c = r2['candidates'][0]
            good = c['wrmse'] <= max(1.35 * t['ref'], 0.02)
            print(f'  try2: #1 wRMSE={c["wrmse"]:.3%} (ref {t["ref"]:.1%}) {"OK" if good else "BAD"}')
            ok = ok and good
        else:
            print('  try2 ERROR', r2.get('error'))
            ok = False

        r3 = run(csv, 'try3', '--edges', t['edges'])
        if r3.get('ok') and r3['candidates']:
            c = r3['candidates'][0]
            good = c['wrmse'] <= max(1.25 * t['ref'], 0.02)
            print(f'  try3: wRMSE={c["wrmse"]:.3%} (ref {t["ref"]:.1%}) {"OK" if good else "BAD"}')
            ok = ok and good
        else:
            print('  try3 ERROR', r3.get('error'))
            ok = False
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
