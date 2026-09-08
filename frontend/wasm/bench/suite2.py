#!/usr/bin/env python3
"""suite2.py — realism-focused randomized benchmark for the three engines.

suite.py models iid point noise + rare outliers.  The four real AUTO-SWEEP
datasets that motivated the 2026-09 campaign add a second error source the
synthetic suite never had: SMOOTH SYSTEMATICS — a slowly varying complex gain
error (fixture / front-end frequency response, sweep-order drift).  This
suite generates random SP one-ports under that mixture so the selector's
regime logic (noise vs systematics) is exercised the way real hardware
exercises it.

Error model per case:
    z_meas = z_true * (1 + eps_point) * (1 + eps_smooth)
    eps_point  ~ iid complex N(0, sigma^2) on both axes (+ optional outliers)
    eps_smooth ~ smooth complex bump field over log10 f:
                 A * sum_k a_k sin(w_k * u + p_k),  u = log10 f
                 correlation width ~ 0.4-2 decades, amplitude A in
                 {0, 0.003, 0.008, 0.015}

Scoring:
  * behavioral  pass@K: candidate theory curve matches the TRUE response
    within tau = max(3*sigma, 2.5*A_smooth, 1e-3)  (suite.py convention).
  * structural  pass@K (try1): rank-K candidate's canonical topology string
    equals the truth tree's canonical string — what a human user compares
    against the known circuit.
  * truth visibility: first rank whose canonical string matches, anywhere in
    the returned top-K (UX: the user scrolls the candidate list).

usage:
  suite2.py run --n 300 --seed 11 [--jobs 16] [--tag NAME]
  suite2.py show --tag NAME
"""
import argparse
import concurrent.futures as cf
import json
import math
import os
import random
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from suite import (Leaf, Node, tree_leaves, run_fitbench, theory_on, max_rel,
                   rand_tree)  # noqa: E402

WORK = os.path.join(HERE, 'work')


# ----------------------------------------------------------------------------
# truth-structure canonical string (mirror of rlc::canonical, circuits.cpp)
# ----------------------------------------------------------------------------
def canon(t):
    if isinstance(t, Leaf):
        return t.kind
    parts = sorted(canon(k) for k in t.kids)
    head = 'S' if t.kind == 'ser' else 'P'
    return head + '(' + ','.join(parts) + ')'


# ----------------------------------------------------------------------------
# realistic component draw
# ----------------------------------------------------------------------------
def real_leaf(rng):
    # E12-ish decade values +- one decade of jitter; L always carries DCR
    kind = rng.choice(['R', 'L', 'C', 'L', 'L'])
    if kind == 'R':
        return Leaf('R', 10 ** rng.uniform(0.5, 4.5))
    if kind == 'C':
        return Leaf('C', 10 ** rng.uniform(-8.5, -5.5))
    # real inductor: DCR correlates with L (more turns -> more copper), draw
    # so that Q0 = w0L/DCR at a ~1 kHz-ish self resonance spans ~0.2..20
    L = 10 ** rng.uniform(-4.5, -0.5)
    dcr = 10 ** rng.uniform(math.log10(L * 1e3) - 1.5, math.log10(L * 1e3) + 1.5)
    dcr = min(max(dcr, 1e-2), 1e4)
    return Leaf('L', L, dcr=dcr)


def rand_real_tree(rng, n_dev):
    """same shape sampler as suite.rand_tree but with realistic leaves"""
    for _ in range(80):
        t = rand_tree_once_real(rng, n_dev)
        if ser_degenerate(t) or not any(l.kind in ('L', 'C')
                                        for l in tree_leaves(t)):
            continue
        return t
    return t


def ser_degenerate(t):
    """mirror of the engine library's childrenOk rules (library.cpp):
    - SER: leaf kinds unique, and R must not coexist with L (folds into DCR)
    - PAR: leaf kinds unique, except multiple L branches are allowed
    Truths violating these have an exactly-equivalent simpler tree in the
    engine's library, so structural scoring would be unfair.
    """
    if isinstance(t, Leaf):
        return False
    leaf_kinds = [k.kind for k in t.kids if isinstance(k, Leaf)]
    if t.kind == 'ser':
        if len(leaf_kinds) != len(set(leaf_kinds)):
            return True
        if 'R' in leaf_kinds and 'L' in leaf_kinds:
            return True
    else:
        others = [k for k in leaf_kinds if k != 'L']
        if len(others) != len(set(others)):
            return True
    return any(ser_degenerate(k) for k in t.kids)


def rand_tree_once_real(rng, n_dev):
    if n_dev == 1:
        return real_leaf(rng)
    kind = rng.choice(['ser', 'par'])
    parts = rng.choice([2, 2, 2, 3]) if n_dev >= 3 else 2
    parts = min(parts, n_dev)
    cuts = sorted(rng.sample(range(1, n_dev), parts - 1)) if parts > 1 else []
    sizes, prev = [], 0
    for c in cuts + [n_dev]:
        sizes.append(c - prev)
        prev = c
    kids = [rand_real_tree(rng, s) if s > 1 else real_leaf(rng) for s in sizes]
    return Node(kind, kids)


# ----------------------------------------------------------------------------
# smooth systematic field over log10 f
# ----------------------------------------------------------------------------
class SmoothField:
    """eps(u) = A * (sum_k a_k sin(w_k u + p_k)) / norm, u = log10 f

    2-3 Fourier components with periods 0.8-6 decades -> band-limited error
    that no SP circuit can express, like fixture response ripple.  Independent
    instances for the two axes (re / im); a shared complex rotation mixes them
    into |Z| and phase error alike.
    """

    def __init__(self, rng, amp):
        self.amp = amp
        self.comp = []
        for _ in range(rng.choice([2, 3])):
            self.comp.append((rng.uniform(0.5, 2.5) * rng.choice([1, 2]),
                              rng.uniform(0, 2 * math.pi), rng.gauss(0, 1)))
        norm = math.sqrt(sum(c[2] ** 2 for c in self.comp)) or 1.0
        self.comp = [(w, p, a / norm) for w, p, a in self.comp]

    def __call__(self, u):
        s = 0.0
        for w, p, a in self.comp:
            s += a * math.sin(w * u + p)
        return self.amp * s


def make_case(rng, cid):
    n_dev = rng.choice([1, 2, 2, 3, 3, 3, 4, 4])
    tree = rand_real_tree(rng, n_dev)
    sigma = rng.choice([0.0, 0.002, 0.005, 0.005, 0.01, 0.02])
    A = rng.choice([0.0, 0.003, 0.008, 0.015]) if sigma < 0.02 else \
        rng.choice([0.0, 0.003, 0.008])
    n_out = rng.choice([0, 0, 0, 1]) if sigma > 0 else 0

    # grid: include the sparse real-data shapes (20 pts / 3 decades)
    n = rng.choice([10, 16, 20, 20, 30, 40, 60])
    fmin = rng.choice([10.0, 50.0, 100.0, 1000.0])
    decades = rng.choice([2.0, 3.0, 3.0, 4.0])
    f = [fmin * 10 ** (decades * i / (n - 1)) for i in range(n)]
    w = [2 * math.pi * x for x in f]
    z_true = [tree.z(x) for x in w]

    fldR = SmoothField(rng, A)
    fldI = SmoothField(rng, A)
    u = [math.log10(x) for x in f]
    idxs = set(rng.sample(range(n), n_out)) if n_out else set()
    z_meas = []
    for i, z in enumerate(z_true):
        d = complex(rng.gauss(0, sigma), rng.gauss(0, sigma))
        if i in idxs:
            d *= rng.uniform(10, 30)
        eps = complex(fldR(u[i]), fldI(u[i]))
        z_meas.append(z * (1.0 + d + eps))

    tau = max(3.0 * sigma, 2.5 * A, 1e-3)
    os.makedirs(WORK, exist_ok=True)
    csv_path = os.path.join(WORK, f'case2_{cid}.csv')
    with open(csv_path, 'w') as fh:
        fh.write(f'# case2 {cid} sigma={sigma} A={A} outliers={n_out} ndev={n_dev}\n')
        for a, z in zip(f, z_meas):
            fh.write(f'{a:.10g},{z.real:.10g},{z.imag:.10g}\n')

    truth = {'id': cid, 'csv': csv_path, 'f': f, 'z_true': z_true, 'tau': tau,
             'sigma': sigma, 'A': A, 'n_outliers': n_out, 'n_dev': n_dev,
             'n_pts': n, 'canon': canon(tree),
             'leaves': [(l.kind, l.value, l.dcr) for l in tree_leaves(tree)]}
    build_try2_rows(rng, truth)
    build_try3_edges(tree, truth)
    return truth


def build_try2_rows(rng, truth):
    rows = []
    for kind, val, dcr in truth['leaves']:
        found = False
        for r in rows:
            if r[0] == kind and abs(math.log10(r[1]) - math.log10(val)) < 0.03 and \
               abs(math.log10(max(r[2], 1e-9)) - math.log10(max(dcr, 1e-9))) < 0.05:
                r[3] += 1
                found = True
                break
        if not found:
            rows.append([kind, val, dcr, 1])
    t2rows = []
    for kind, val, dcr, cnt in rows:
        v = val * (10 ** rng.gauss(0, 0.021))
        d = (dcr * (10 ** rng.gauss(0, 0.1))) if dcr > 1e-5 else 0.0
        t2rows.append((kind, v, d, cnt))
    truth['try2_rows'] = t2rows


def build_try3_edges(tree, truth):
    edges = []
    counter = [2]

    def emit(t, a, b):
        if isinstance(t, Leaf):
            edges.append((a, b, t.kind))
            return
        if t.kind == 'par':
            for k in t.kids:
                emit(k, a, b)
        else:
            prev = a
            ints = [counter[0] + i for i in range(len(t.kids) - 1)]
            counter[0] += len(t.kids) - 1
            for k, nxt in zip(t.kids, ints + [b]):
                emit(k, prev, nxt)
                prev = nxt

    emit(tree, 0, 1)
    truth['try3_edges'] = list(edges)


def score_try1(truth, resp):
    if not resp.get('ok'):
        return {'hit': 0, 'rank': None, 'srank': None, 'err': resp.get('error', '')[:120]}
    cands = resp.get('candidates', [])
    rank = None
    prank = None  # parsimonious-behavioral: behaviorally matches AND no more
    #              devices than the truth tree (Cauer/Foster re-wirings of the
    #              same Z(s) count as correct; a mimic with an extra device
    #              does not — that is what a human user checks)
    srank = None
    for i, c in enumerate(cands):
        if srank is None and c.get('topology') == truth['canon']:
            srank = i + 1
        zt = theory_on(c, truth['f'])
        if zt is not None and max_rel(zt, truth['z_true']) < truth['tau']:
            if rank is None:
                rank = i + 1
            if prank is None and c.get('devices', 99) <= truth['n_dev']:
                prank = i + 1
    return {'hit': 1 if rank else 0, 'rank': rank,
            'phit': 1 if prank else 0, 'prank': prank,
            'shit': 1 if srank else 0, 'srank': srank,
            'top1_canon': cands[0].get('topology') if cands else None,
            'top1_dev': cands[0].get('devices') if cands else None,
            'top1_wrmse': cands[0].get('wrmse') if cands else None,
            'n_cand': len(cands)}


def score_try2(truth, resp):
    if not resp.get('ok'):
        return {'hit': 0, 'rank': None, 'err': resp.get('error', '')[:120]}
    cands = resp.get('candidates', [])
    rank = None
    for i, c in enumerate(cands):
        zt = theory_on(c, truth['f'])
        if zt is not None and max_rel(zt, truth['z_true']) < truth['tau']:
            rank = i + 1
            break
    return {'hit': 1 if rank else 0, 'rank': rank,
            'wrmse1': cands[0]['wrmse'] if cands else None}


def score_try3(truth, resp):
    if not resp.get('ok') or not resp.get('candidates'):
        return {'hit': 0, 'wrmse1': resp.get('error', '')[:120] if not resp.get('ok') else None}
    cands = resp['candidates']
    zt = theory_on(cands[0], truth['f'])
    hit = zt is not None and max_rel(zt, truth['z_true']) < truth['tau']
    return {'hit': 1 if hit else 0, 'wrmse1': cands[0]['wrmse']}


def one_case(seed_cid):
    seed, cid = seed_cid
    rng = random.Random(seed)
    truth = make_case(rng, cid)
    out = {'id': cid, 'sigma': truth['sigma'], 'A': truth['A'],
           'n_outliers': truth['n_outliers'], 'n_dev': truth['n_dev'],
           'n_pts': truth['n_pts'], 'canon': truth['canon']}
    t2 = truth['try2_rows']
    t3 = truth['try3_edges']
    t0 = time.time()
    out['try1'] = score_try1(truth, run_fitbench(truth['csv'], 'try1', ['--topk', '8']))
    out['t_try1'] = time.time() - t0
    rows_arg = ';'.join(f'{k}:{v:.6g}:{d:.6g}:{c}' for k, v, d, c in t2)
    out['try2'] = score_try2(truth, run_fitbench(truth['csv'], 'try2',
                                                 ['--rows', rows_arg, '--topk', '8']))
    edges_arg = ';'.join(f'{a} {b} {k}' for a, b, k in t3)
    out['try3'] = score_try3(truth, run_fitbench(truth['csv'], 'try3',
                                                 ['--edges', edges_arg, '--topk', '1']))
    os.remove(truth['csv'])
    return out


def summarize(results, tag):
    n = len(results)
    print(f'== suite2 {tag}: {n} cases ==')

    def rate(key, pred):
        rs = [r for r in results if key in r and r[key]]
        return f'{len(rs)/n:.1%}'

    for key in ('try1', 'try2', 'try3'):
        rs = [r for r in results if key in r]
        if not rs:
            continue
        m = len(rs)
        if key == 'try1':
            p1 = sum(1 for r in rs if r[key].get('rank') == 1) / m
            p8 = sum(1 for r in rs if r[key].get('rank')) / m
            pp1 = sum(1 for r in rs if r[key].get('prank') == 1) / m
            pp8 = sum(1 for r in rs if r[key].get('prank')) / m
            sp1 = sum(1 for r in rs if r[key].get('srank') == 1) / m
            sp8 = sum(1 for r in rs if r[key].get('srank')) / m
            sp3 = sum(1 for r in rs if r[key].get('srank') and r[key]['srank'] <= 3) / m
            ts = [r['t_try1'] for r in rs]
            ts.sort()
            print(f'  try1: behav pass@1={p1:.1%} pass@8={p8:.1%} | '
                  f'parsimon pass@1={pp1:.1%} pass@8={pp8:.1%} | '
                  f'struct pass@1={sp1:.1%} pass@3={sp3:.1%} pass@8={sp8:.1%} '
                  f'med_t={ts[m//2]:.2f}s')
        elif key == 'try2':
            p1 = sum(1 for r in rs if r[key].get('rank') == 1) / m
            p8 = sum(1 for r in rs if r[key].get('rank')) / m
            print(f'  try2: behav pass@1={p1:.1%} pass@8={p8:.1%}')
        else:
            hit = sum(r[key].get('hit', 0) for r in rs)
            print(f'  try3: exact-hit {hit}/{m} = {hit/m:.1%}')
    # slices
    for label, pred in [
            ('sigma=0,A=0', lambda r: r['sigma'] == 0 and r['A'] == 0),
            ('sigma=0,A>0', lambda r: r['sigma'] == 0 and r['A'] > 0),
            ('sigma>0,A=0', lambda r: r['sigma'] > 0 and r['A'] == 0),
            ('sigma>0,A>0', lambda r: r['sigma'] > 0 and r['A'] > 0),
            ('outliers>0', lambda r: r['n_outliers'] > 0),
            ('npts<=20', lambda r: r['n_pts'] <= 20),
            ('ndev=1', lambda r: r['n_dev'] == 1),
            ('ndev=2', lambda r: r['n_dev'] == 2),
            ('ndev=3', lambda r: r['n_dev'] == 3),
            ('ndev=4', lambda r: r['n_dev'] == 4)]:
        sub = [r for r in results if pred(r)]
        if not sub:
            continue
        m = len(sub)
        p1 = sum(1 for r in sub if r['try1'].get('rank') == 1) / m
        pp1 = sum(1 for r in sub if r['try1'].get('prank') == 1) / m
        sp1 = sum(1 for r in sub if r['try1'].get('srank') == 1) / m
        sp8 = sum(1 for r in sub if r['try1'].get('srank')) / m
        print(f'    {label:<14} n={m:>3} behav@1={p1:.1%} parsim@1={pp1:.1%} struct@1={sp1:.1%} struct@8={sp8:.1%}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['run', 'show'])
    ap.add_argument('--n', type=int, default=200)
    ap.add_argument('--seed', type=int, default=11)
    ap.add_argument('--jobs', type=int, default=16)
    ap.add_argument('--tag', default='s2')
    ap.add_argument('--offset', type=int, default=0)
    a = ap.parse_args()
    if a.cmd == 'show':
        with open(os.path.join(WORK, f'results2_{a.tag}.json')) as fh:
            summarize(json.load(fh), a.tag)
        return
    seeds = [(a.seed * 1_000_003 + i, a.offset + i) for i in range(a.n)]
    results = []
    with cf.ProcessPoolExecutor(max_workers=a.jobs) as ex:
        for i, r in enumerate(ex.map(one_case, seeds, chunksize=2)):
            results.append(r)
            if (i + 1) % 50 == 0:
                print(f'  ... {i+1}/{a.n}', flush=True)
    os.makedirs(WORK, exist_ok=True)
    with open(os.path.join(WORK, f'results2_{a.tag}.json'), 'w') as fh:
        json.dump(results, fh)
    summarize(results, a.tag)


if __name__ == '__main__':
    main()
