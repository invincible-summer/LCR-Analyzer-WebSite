#!/usr/bin/env python3
"""make_outlier_cases.py — reproduce endpoint-outlier F2 mis-pruning."""
import math
import os
import random
import sys

def leaf(kind, v, dcr=0.0):
    return (kind, v, dcr)

def z_of(tree, w):
    kind = tree[0]
    if kind == 'R':
        return complex(tree[1], 0.0)
    if kind == 'L':
        return complex(tree[2], w * tree[1])
    if kind == 'C':
        return complex(0.0, -1.0 / (w * tree[1]))
    kids = tree[1]
    if kind == 'S':
        return sum(z_of(k, w) for k in kids)
    y = sum(1.0 / z_of(k, w) for k in kids)
    return 1.0 / y

def write_case(path, tree, n, fmin, decades, sigma, outlier_idx, outlier_mult, seed):
    rng = random.Random(seed)
    f = [fmin * 10 ** (decades * i / (n - 1)) for i in range(n)]
    rows = []
    for i, fq in enumerate(f):
        w = 2 * math.pi * fq
        z = z_of(tree, w)
        d = complex(rng.gauss(0, sigma), rng.gauss(0, sigma))
        if i == outlier_idx:
            d += complex(rng.gauss(0, outlier_mult), rng.gauss(0, outlier_mult))
        z = z * (1.0 + d)
        rows.append((fq, z.real, z.imag))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as fh:
        fh.write(f'# outlier case: idx={outlier_idx} mult={outlier_mult} sigma={sigma}\n')
        for a, r_, i_ in rows:
            fh.write(f'{a:.10g},{r_:.10g},{i_:.10g}\n')
    print(f'wrote {path}')

if __name__ == '__main__':
    # data2-like tank, endpoint outlier
    tree = ('P', [leaf('L', 0.1009, 334.7), leaf('C', 9.49e-7)])
    write_case('work/out_hi.csv', tree, 20, 10.0, 3.0, 0.005, 19, 0.25, 101)
    write_case('work/out_lo.csv', tree, 20, 10.0, 3.0, 0.005, 0, 0.25, 102)
    # R+L+C series with endpoint outlier at high f (inductive end)
    tree2 = ('S', [leaf('R', 50.0), leaf('L', 1e-3, 2.0), leaf('C', 1e-6)])
    write_case('work/out_hi2.csv', tree2, 30, 100.0, 3.0, 0.005, 29, 0.3, 103)
