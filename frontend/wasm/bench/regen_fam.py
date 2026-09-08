#!/usr/bin/env python3
"""regen one realfam draw to a CSV for probing (data4 family, seed given)."""
import math
import random
import sys

sys.path.insert(0, '.')
from suite2 import SmoothField  # noqa: E402
from suite import Leaf, Node  # noqa: E402


def z_of(t, w):
    if isinstance(t, Leaf):
        if t.kind == 'R':
            return complex(t.value, 0.0)
        if t.kind == 'L':
            return complex(t.dcr, w * t.value)
        return complex(0.0, -1.0 / (w * t.value))
    if t.kind == 'ser':
        return sum(z_of(k, w) for k in t.kids)
    y = sum(1.0 / z_of(k, w) for k in t.kids)
    return 1.0 / y


def main():
    seed = int(sys.argv[1])
    out = sys.argv[2]
    rng = random.Random(seed)
    tree = Node('ser', [
        Node('par', [Leaf('R', 138.3), Leaf('L', 8.02e-4, dcr=10.45),
                     Leaf('C', 8.93e-7)]),
        Leaf('R', 52.4)])
    n = 40
    f = [10.0 * 10 ** (3.0 * i / (n - 1)) for i in range(n)]
    w = [2 * math.pi * x for x in f]
    z_true = [tree.z(x) for x in w]
    sigma = rng.choice([0.005, 0.01, 0.01, 0.02])
    A = rng.choice([0.0, 0.005, 0.01, 0.015])
    n_out = rng.choice([0, 0, 0, 1])
    fldR = SmoothField(rng, A)
    fldI = SmoothField(rng, A)
    idxs = set(rng.sample(range(n), n_out)) if n_out else set()
    with open(out, 'w') as fh:
        for i, fq in enumerate(f):
            z = z_true[i]
            d = complex(rng.gauss(0, sigma), rng.gauss(0, sigma))
            if i in idxs:
                d *= rng.uniform(10, 30)
            eps = complex(fldR(math.log10(fq)), fldI(math.log10(fq)))
            z = z * (1.0 + d + eps)
            fh.write(f'{fq:.10g},{z.real:.10g},{z.imag:.10g}\n')
    print(f'seed={seed} sigma={sigma} A={A} out={sorted(idxs)}')


if __name__ == '__main__':
    main()
