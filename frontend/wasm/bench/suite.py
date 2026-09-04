#!/usr/bin/env python3
"""suite.py — randomized benchmark for the three Try engines (native ABI).

Generates random SP one-ports with known ground truth, adds calibrated noise
(+ optional outliers), runs fitbench (try1/try2/try3) and scores:

  * structural recovery: some candidate's theory curve matches the TRUE
    (noiseless) response within tau = max(3*sigma_axis, 1e-3) relative
    (behavioral equivalence — topology isomorphism is NOT required);
  * rank of the first matching candidate (1 = champion);
  * value recovery for try3 (per-group L/DCR/R/C accuracy);
  * runtime.

usage:
  suite.py run --n 400 [--seed 1] [--trys 1,2,3] [--jobs 20] [--tag NAME]
  suite.py show [--tag NAME]
"""
import argparse
import concurrent.futures as cf
import csv
import json
import math
import os
import random
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, '..', 'build-native', 'fitbench')
WORK = os.path.join(HERE, 'work')

# ----------------------------------------------------------------------------
# SP-tree model (generator side)
# ----------------------------------------------------------------------------


class Leaf:
    def __init__(self, kind, value, dcr=0.0):
        self.kind, self.value, self.dcr = kind, value, dcr

    def z(self, w):
        if self.kind == 'R':
            return complex(self.value, 0.0)
        if self.kind == 'L':
            return complex(self.dcr, w * self.value)
        return complex(0.0, -1.0 / (w * self.value))


class Node:
    def __init__(self, kind, kids):  # kind: 'ser' | 'par'
        self.kind, self.kids = kind, kids

    def z(self, w):
        if self.kind == 'ser':
            return sum((k.z(w) for k in self.kids), complex(0.0))
        y = sum(1.0 / k.z(w) for k in self.kids)
        return 1.0 / y


def tree_leaves(t):
    if isinstance(t, Leaf):
        return [t]
    out = []
    for k in t.kids:
        out += tree_leaves(k)
    return out


def has_storage(t):
    return any(l.kind in ('L', 'C') for l in tree_leaves(t))


def rand_tree(rng, n_dev):
    for _ in range(80):
        t = rand_tree_once(rng, n_dev)
        if ser_degenerate(t) or not has_storage(t):
            continue
        return t
    return t


def ser_degenerate(t):
    """a SER node with two same-kind leaf children is ill-posed (values merge)"""
    if isinstance(t, Leaf):
        return False
    if t.kind == 'ser':
        kinds = [k.kind for k in t.kids if isinstance(k, Leaf)]
        if len(kinds) != len(set(kinds)):
            return True
    return any(ser_degenerate(k) for k in t.kids)


def rand_tree_once(rng, n_dev):
    if n_dev == 1:
        return rand_leaf(rng)
    kind = rng.choice(['ser', 'par'])
    # split into 2..min(3, n-1) parts for bushy trees
    parts = rng.choice([2, 2, 2, 3]) if n_dev >= 3 else 2
    parts = min(parts, n_dev)
    cuts = sorted(rng.sample(range(1, n_dev), parts - 1)) if parts > 1 else []
    sizes, prev = [], 0
    for c in cuts + [n_dev]:
        sizes.append(c - prev)
        prev = c
    kids = [rand_tree(rng, s) if s > 1 else rand_leaf(rng) for s in sizes]
    if kind == 'ser' and len(kids) > 2:
        rng.shuffle(kids)
    return Node(kind, kids)


def rand_leaf(rng):
    kind = rng.choice(['R', 'L', 'C', 'L'])  # bias L: DCR semantics matter
    if kind == 'R':
        return Leaf('R', 10 ** rng.uniform(0, 6))
    if kind == 'C':
        return Leaf('C', 10 ** rng.uniform(-12, -5))
    dcr = 10 ** rng.uniform(-6, 2) if rng.random() < 0.7 else 10 ** rng.uniform(-3, 2)
    return Leaf('L', 10 ** rng.uniform(-6, 0), dcr=dcr)


def freq_grid(rng):
    n = rng.choice([16, 20, 30, 40, 60])
    fmin = rng.choice([10.0, 100.0, 1000.0])
    decades = rng.choice([2.0, 3.0, 3.0])
    return [fmin * 10 ** (decades * i / (n - 1)) for i in range(n)]


def noisy_z(rng, z_true, sigma, n_outliers):
    out = []
    idxs = set(rng.sample(range(len(z_true)), n_outliers)) if n_outliers else set()
    for i, z in enumerate(z_true):
        d = complex(rng.gauss(0, sigma), rng.gauss(0, sigma))
        if i in idxs:
            d *= rng.uniform(10, 30)
        out.append(z * (1.0 + d))
    return out


# ----------------------------------------------------------------------------
# scoring helpers
# ----------------------------------------------------------------------------


def max_rel(za, zb):
    mx = 0.0
    for a, b in zip(za, zb):
        mx = max(mx, abs(a - b) / max(abs(a), 1e-300))
    return mx


def parse_csv(path):
    f, re, im = [], [], []
    with open(path) as fh:
        for row in csv.reader(fh):
            line = ','.join(row).strip()
            if not line or line.startswith('#'):
                continue
            parts = [p for p in row if p.strip()]
            if len(parts) != 3:
                continue
            f.append(float(parts[0]))
            re.append(float(parts[1]))
            im.append(float(parts[2]))
    order = sorted(range(len(f)), key=lambda i: f[i])
    return ([f[i] for i in order], [re[i] for i in order], [im[i] for i in order])


def run_fitbench(csv_path, cmd, extra, timeout=900):
    try:
        p = subprocess.run([BIN, csv_path, cmd, *extra], capture_output=True,
                           text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return {'ok': False, 'error': 'timeout'}
    if p.returncode != 0:
        return {'ok': False, 'error': p.stderr.strip()[:200]}
    try:
        return json.loads(p.stdout)
    except json.JSONDecodeError as e:
        return {'ok': False, 'error': f'json {e}'}


def theory_on(d, fgrid):
    th = d.get('theory') or {}
    if not th.get('f'):
        return None
    import bisect
    ff, re, im = th['f'], th['re'], th['im']
    if len(ff) < 2:
        return None
    out = []
    lg = [math.log10(x) for x in ff]
    for x in fgrid:
        lx = math.log10(x)
        j = min(max(bisect.bisect_left(lg, lx), 1), len(lg) - 1)
        t = (lx - lg[j - 1]) / max(lg[j] - lg[j - 1], 1e-300)
        out.append(complex(re[j - 1] + t * (re[j] - re[j - 1]),
                           im[j - 1] + t * (im[j] - im[j - 1])))
    return out


def make_case(rng, cid):
    n_dev = rng.choice([1, 2, 2, 3, 3, 3, 4, 4, 5])
    tree = rand_tree(rng, n_dev)
    sigma = rng.choice([0.0, 0.003, 0.003, 0.01, 0.01, 0.03])
    n_out = rng.choice([0, 0, 0, 1, 2]) if sigma > 0 else 0
    f = freq_grid(rng)
    w = [2 * math.pi * x for x in f]
    z_true = [tree.z(x) for x in w]
    z_meas = noisy_z(rng, z_true, sigma, n_out)
    tau = max(3.0 * sigma, 1e-3)

    os.makedirs(WORK, exist_ok=True)
    csv_path = os.path.join(WORK, f'case_{cid}.csv')
    with open(csv_path, 'w') as fh:
        fh.write(f'# case {cid} sigma={sigma} outliers={n_out} ndev={n_dev}\n')
        for a, z in zip(f, z_meas):
            fh.write(f'{a:.10g},{z.real:.10g},{z.imag:.10g}\n')

    truth = {'id': cid, 'csv': csv_path, 'f': f, 'z_true': z_true, 'tau': tau,
             'sigma': sigma, 'n_outliers': n_out, 'n_dev': n_dev,
             'leaves': [(l.kind, l.value, l.dcr) for l in tree_leaves(tree)]}

    # try2 input: user-typed nominal values (±5% lognormal) of the true multiset
    rows = []  # entries: [kind, value, dcr, count]
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
        v = val * (10 ** rng.gauss(0, 0.021))  # user types value within ~±5%
        d = (dcr * (10 ** rng.gauss(0, 0.1))) if dcr > 1e-5 else 0.0
        t2rows.append((kind, v, d, cnt))
    truth['try2_rows'] = t2rows

    # try3 input: truth topology edges.  Mapping: parallel node -> shared
    # junction pair; series node -> chain of internal nodes; every edge carries
    # exactly one element.  Ports are nodes 0 and 1.
    edges = []
    counter = [2]

    def emit(t, a, b):
        if isinstance(t, Leaf):
            edges.append((a, b, t.kind))
            return
        if t.kind == 'par':
            for k in t.kids:
                emit(k, a, b)
        else:  # series: chain through internal nodes
            prev = a
            ints = [counter[0] + i for i in range(len(t.kids) - 1)]
            counter[0] += len(t.kids) - 1
            for k, nxt in zip(t.kids, ints + [b]):
                emit(k, prev, nxt)
                prev = nxt

    emit(tree, 0, 1)
    truth['try3_edges'] = list(edges)
    return truth


def score_try1(truth, resp):
    if not resp.get('ok'):
        return {'hit': 0, 'rank': None, 'n_cand': 0, 'err': resp.get('error', '')[:120]}
    cands = resp.get('candidates', [])
    rank = None
    for i, c in enumerate(cands):
        zt = theory_on(c, truth['f'])
        if zt is not None and max_rel(zt, truth['z_true']) < truth['tau']:
            rank = i + 1
            break
    return {'hit': 1 if rank else 0, 'rank': rank, 'n_cand': len(cands),
            'wrmse1': cands[0]['wrmse'] if cands else None}


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
    if not resp.get('ok'):
        return {'hit': 0, 'werr': None, 'err': resp.get('error', '')[:120]}
    cands = resp.get('candidates', [])
    if not cands:
        return {'hit': 0, 'werr': None}
    zt = theory_on(cands[0], truth['f'])
    hit = zt is not None and max_rel(zt, truth['z_true']) < truth['tau']
    # value recovery: compare adjacency values with truth leaves per kind
    werr = None
    adj = cands[0].get('adjacency') or {}
    got = []
    for slot in adj.get('slots', []):
        for e in slot.get('edges', []):
            got.append((e['t'], e.get('p', 0.0), e.get('d', 0.0)))
    if hit and got:
        errs = []
        used = set()
        for kind, p, d in got:
            best, bi = None, None
            for i, (k2, v2, d2) in enumerate(truth['leaves']):
                if i in used or k2 != kind:
                    continue
                e = abs(math.log10(max(p, 1e-300)) - math.log10(v2))
                if best is None or e < best:
                    best, bi = e, i
            if best is not None:
                used.add(bi)
                errs.append(best)
        werr = 10 ** (sum(errs) / len(errs)) if errs else None
    return {'hit': 1 if hit else 0, 'werr': werr, 'wrmse1': cands[0]['wrmse']}


def one_case(seed_cid):
    seed, cid = seed_cid
    rng = random.Random(seed)
    truth = make_case(rng, cid)
    out = {'id': cid, 'sigma': truth['sigma'], 'n_outliers': truth['n_outliers'],
           'n_dev': truth['n_dev']}
    t2 = truth['try2_rows']
    t3 = truth['try3_edges']
    t0 = time.time()
    maxn = os.environ.get('SUITE_MAXN', '')
    extra1 = ['--topk', '8'] + (['--maxn', maxn] if maxn else [])
    out['try1'] = score_try1(truth, run_fitbench(truth['csv'], 'try1', extra1))
    out['t_try1'] = time.time() - t0
    rows_arg = ';'.join(f'{k}:{v:.6g}:{d:.6g}:{c}' for k, v, d, c in t2)
    t0 = time.time()
    out['try2'] = score_try2(truth, run_fitbench(truth['csv'], 'try2',
                                                 ['--rows', rows_arg, '--topk', '8']))
    out['t_try2'] = time.time() - t0
    edges_arg = ';'.join(f'{a} {b} {k}' for a, b, k in t3)
    t0 = time.time()
    out['try3'] = score_try3(truth, run_fitbench(truth['csv'], 'try3',
                                                 ['--edges', edges_arg, '--topk', '1']))
    out['t_try3'] = time.time() - t0
    os.remove(truth['csv'])
    return out


def summarize(results, tag):
    def agg(key, rs):
        rs = [r for r in rs if key in r]
        n = len(rs)
        if not n:
            return {}
        hits = sum(r[key].get('hit', 0) for r in rs)
        p1 = sum(1 for r in rs if r[key].get('rank') == 1) / n
        ranks = [r[key]['rank'] for r in rs if r[key].get('rank')]
        out = {'n': n, 'pass_any': hits / n, 'pass_1': p1,
               'med_rank': sorted(ranks)[len(ranks) // 2] if ranks else None}
        if key == 'try3':
            werrs = [r[key]['werr'] for r in rs if r[key].get('werr')]
            out['med_valerr'] = sorted(werrs)[len(werrs) // 2] if werrs else None
        if key == 'try1':
            t = [r['t_try1'] for r in rs]
            out['med_t'] = sorted(t)[len(t) // 2]
        return out

    print(f'== suite {tag}: {len(results)} cases ==')
    for key in ('try1', 'try2', 'try3'):
        s = agg(key, results)
        if not s:
            continue
        line = (f"{key}: pass@1={s['pass_1']:.1%} pass@8={s['pass_any']:.1%}"
                f" med_rank={s['med_rank']}")
        if 'med_valerr' in s:
            line += f" med_valerr={s['med_valerr']:.3f}x"
        if 'med_t' in s:
            line += f" med_t={s['med_t']:.2f}s"
        print(' ', line)
        for sig in (0.0, 0.003, 0.01, 0.03):
            sub = [r for r in results if abs(r['sigma'] - sig) < 1e-9]
            if sub:
                ss = agg(key, sub)
                extra = f" med_valerr={ss['med_valerr']:.3f}x" if 'med_valerr' in ss else ''
                print(f"    sigma={sig:<6} n={ss['n']:>3} pass@1={ss['pass_1']:.1%}"
                      f" pass@8={ss['pass_any']:.1%}{extra}")
        for nd in (1, 2, 3, 4, 5):
            sub = [r for r in results if r['n_dev'] == nd]
            if sub:
                ss = agg(key, sub)
                print(f"    ndev={nd} n={ss['n']:>3} pass@1={ss['pass_1']:.1%}"
                      f" pass@8={ss['pass_any']:.1%}")
    fails = [r for r in results if r['try1'].get('err')]
    if fails:
        print(f'  try1 errors: {len(fails)} e.g. {fails[0]["try1"]["err"]}')
    # try3 has a single candidate: pass@1 == hit rate
    hits3 = sum(r['try3'].get('hit', 0) for r in results)
    print(f'  try3 exact-hit: {hits3}/{len(results)} = {hits3/len(results):.1%}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['run', 'show'])
    ap.add_argument('--n', type=int, default=200)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--jobs', type=int, default=20)
    ap.add_argument('--tag', default='baseline')
    ap.add_argument('--offset', type=int, default=0)
    a = ap.parse_args()
    if a.cmd == 'show':
        with open(os.path.join(WORK, f'results_{a.tag}.json')) as fh:
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
    with open(os.path.join(WORK, f'results_{a.tag}.json'), 'w') as fh:
        json.dump(results, fh)
    summarize(results, a.tag)


if __name__ == '__main__':
    main()
