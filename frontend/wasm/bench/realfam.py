#!/usr/bin/env python3
"""realfam.py — the four real AUTO-SWEEP structures under resampled noise.

The four datasets in examples/ are single draws of noise + smooth systematics
on four known SP circuits.  This script regenerates that FAMILY: same four
truth structures and grids, fresh error draws (iid point noise, smooth
fixture-like systematics, occasional outliers) — so selector changes are
validated against the distribution the user actually experiences, not one
lucky/unlucky draw.

Metrics (try1, the free-identification engine the user complained about):
  * champ_ok    : rank-1 candidate's canonical topology == truth canonical
  * champ_parsi : rank-1 behaviorally matches truth (tau) AND has no more
                  devices than the truth (the "extra-R mimic" on data2/3
                  fails this)
  * truth_top3  : the exact truth structure appears in the top 3
  * junk_top4   : any of ranks 2-4 has wRMSE > 4x the rank-1 wRMSE
                  (the "R at 99% ranked #2" pathology)
  * try2/try3 behavioral hit of rank-1

usage: realfam.py [--draws 30] [--seed 21] [--jobs 16] [--tag NAME]
"""
import argparse
import concurrent.futures as cf
import json
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from suite import Leaf, Node, run_fitbench, theory_on, max_rel  # noqa: E402
from suite2 import canon, SmoothField  # noqa: E402

WORK = os.path.join(HERE, 'work')

# truth structures: recovered values from examples/ans.txt (independent scipy
# fits), not the nominal labels
def fam_trees():
    f1 = Node('par', [Leaf('R', 9.93e3), Leaf('C', 1.028e-7)])
    f2 = Node('par', [Leaf('L', 0.1009, dcr=334.7), Leaf('C', 9.49e-7)])
    f3 = Node('ser', [
        Node('par', [Leaf('L', 0.1056, dcr=355.0), Leaf('C', 1.02e-7)]),
        Leaf('C', 9.76e-7)])
    f4 = Node('ser', [
        Node('par', [Leaf('R', 138.3), Leaf('L', 8.02e-4, dcr=10.45),
                     Leaf('C', 8.93e-7)]),
        Leaf('R', 52.4)])
    return [f1, f2, f3, f4]


FAM_N = [40, 20, 40, 40]      # grid sizes of the real sweeps
FAM_TRY2_ROWS = [
    'R:9.9e3:0:1;C:1e-7:0:1',
    'L:0.1:345:1;C:1e-6:0:1',
    'L:0.1:345:1;C:1e-7:0:1;C:1e-6:0:1',
    'R:200:0:1;R:50:0:1;L:820e-6:13:1;C:1e-6:0:1',
]
FAM_TRY3_EDGES = [
    '0 1 R;0 1 C',
    '0 1 L;0 1 C',
    '0 2 L;0 2 C;2 1 C',
    '0 2 R;0 2 L;0 2 C;2 1 R',
]


def one_draw(args):
    seed, fi = args
    rng = random.Random(seed)
    tree = fam_trees()[fi]
    n = FAM_N[fi]
    f = [10.0 * 10 ** (3.0 * i / (n - 1)) for i in range(n)]  # 10 Hz - 10 kHz
    w = [2 * math.pi * x for x in f]
    z_true = [tree.z(x) for x in w]
    sigma = rng.choice([0.005, 0.01, 0.01, 0.02])
    A = rng.choice([0.0, 0.005, 0.01, 0.015])
    n_out = rng.choice([0, 0, 0, 1])
    fldR = SmoothField(rng, A)
    fldI = SmoothField(rng, A)
    idxs = set(rng.sample(range(n), n_out)) if n_out else set()
    z_meas = []
    for i, z in enumerate(z_true):
        d = complex(rng.gauss(0, sigma), rng.gauss(0, sigma))
        if i in idxs:
            d *= rng.uniform(10, 30)
        eps = complex(fldR(math.log10(f[i])), fldI(math.log10(f[i])))
        z_meas.append(z * (1.0 + d + eps))
    tau = max(3.0 * sigma, 2.5 * A, 1e-3)

    os.makedirs(WORK, exist_ok=True)
    csv_path = os.path.join(WORK, f'fam_{seed}.csv')
    with open(csv_path, 'w') as fh:
        for a, z in zip(f, z_meas):
            fh.write(f'{a:.10g},{z.real:.10g},{z.imag:.10g}\n')

    tcanon = canon(tree)
    out = {'fam': fi + 1, 'seed': seed, 'sigma': sigma, 'A': A,
           'n_out': n_out, 'canon': tcanon, 'ndev': tree_leaves_count(tree)}

    r1 = run_fitbench(csv_path, 'try1', ['--topk', '8'])
    if not r1.get('ok'):
        out['err1'] = r1.get('error', '')[:100]
        os.remove(csv_path)
        return out
    cands = r1['candidates']
    c1 = cands[0]
    zt1 = theory_on(c1, f)
    behav1 = zt1 is not None and max_rel(zt1, z_true) < tau
    out['champ_ok'] = 1 if c1.get('topology') == tcanon else 0
    out['champ_parsi'] = 1 if (behav1 and c1.get('devices', 99) <= out['ndev']) else 0
    out['champ_behav'] = 1 if behav1 else 0
    out['champ_dev'] = c1.get('devices')
    out['truth_top3'] = 0
    for c in cands[:3]:
        if c.get('topology') == tcanon:
            out['truth_top3'] = 1
            break
    w1 = c1.get('wrmse', 0) or 1e-9
    out['junk_top4'] = 0
    for c in cands[1:4]:
        if c.get('wrmse', 0) > 4.0 * w1:
            out['junk_top4'] = 1
            break
    # ordering inversions: a candidate ranked above a clearly-better one
    # (wrmse_i > 4x wrmse_j for i < j).  Non-zero => garbage sits above a
    # good candidate — the user-visible ordering pathology.
    out['inversions'] = 0
    top = cands[:8]
    for i in range(len(top)):
        for jj in range(i + 1, len(top)):
            if top[i].get('wrmse', 0) > 4.0 * (top[jj].get('wrmse', 0) or 1e-9):
                out['inversions'] += 1
                break

    r2 = run_fitbench(csv_path, 'try2', ['--rows', FAM_TRY2_ROWS[fi], '--topk', '3'])
    if r2.get('ok') and r2.get('candidates'):
        zt2 = theory_on(r2['candidates'][0], f)
        out['try2_hit'] = 1 if (zt2 is not None and max_rel(zt2, z_true) < tau) else 0
    else:
        out['try2_hit'] = 0
    r3 = run_fitbench(csv_path, 'try3', ['--edges', FAM_TRY3_EDGES[fi], '--topk', '1'])
    if r3.get('ok') and r3.get('candidates'):
        zt3 = theory_on(r3['candidates'][0], f)
        out['try3_hit'] = 1 if (zt3 is not None and max_rel(zt3, z_true) < tau) else 0
    else:
        out['try3_hit'] = 0
    os.remove(csv_path)
    return out


def tree_leaves_count(t):
    if isinstance(t, Leaf):
        return 1
    return sum(tree_leaves_count(k) for k in t.kids)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--draws', type=int, default=30)
    ap.add_argument('--seed', type=int, default=21)
    ap.add_argument('--jobs', type=int, default=16)
    ap.add_argument('--tag', default='fam')
    a = ap.parse_args()
    jobs = []
    for d in range(a.draws):
        for fi in range(4):
            jobs.append((a.seed * 7919 + d * 4 + fi, fi))
    with cf.ProcessPoolExecutor(max_workers=a.jobs) as ex:
        results = list(ex.map(one_draw, jobs, chunksize=1))
    tag = a.tag
    with open(os.path.join(WORK, f'realfam_{tag}.json'), 'w') as fh:
        json.dump(results, fh)

    print(f'== realfam {tag}: {len(results)} draws ==')
    for fi in (1, 2, 3, 4):
        sub = [r for r in results if r.get('fam') == fi]
        if not sub:
            continue
        m = len(sub)
        def pct(k):
            return sum(r.get(k, 0) for r in sub) / m
        print(f'  data{fi}-family (n={m}): champOK={pct("champ_ok"):.0%} '
              f'parsi={pct("champ_parsi"):.0%} behav={pct("champ_behav"):.0%} '
              f'truthTop3={pct("truth_top3"):.0%} junkTop4={pct("junk_top4"):.0%} '
              f'inversions={pct("inversions"):.0%} '
              f'try2={pct("try2_hit"):.0%} try3={pct("try3_hit"):.0%}')
    allr = results
    m = len(allr)
    print(f'  TOTAL champOK={sum(r.get("champ_ok",0) for r in allr)/m:.0%} '
          f'parsi={sum(r.get("champ_parsi",0) for r in allr)/m:.0%} '
          f'junkTop4={sum(r.get("junk_top4",0) for r in allr)/m:.0%} '
          f'inversions={sum(r.get("inversions",0) for r in allr)/m:.0%}')


if __name__ == '__main__':
    main()
