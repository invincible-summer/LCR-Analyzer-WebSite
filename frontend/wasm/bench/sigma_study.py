#!/usr/bin/env python3
"""sigma_study.py — measure estimateRelativeNoise accuracy (R2/R12 groundwork).

Exact Python port of rlc::estimateRelativeNoise (selector.cpp).  Used to
answer: how close is the data-driven noise floor to the *effective* floor
(noise + smooth systematics) on the four real datasets and on synthetic
draws?  The consistency gate in championFromRss currently derives its noise
level from the best candidate's RSS (circular, downward-biased); switching it
to this estimate is only justified if the estimator tracks the effective
floor reasonably (bias and spread measured here).
"""
import math
import sys


def estimate_relative_noise(z):
    m = len(z)
    if m < 7:
        return -1.0
    y = [math.log(max(abs(v), 1e-300)) for v in z]
    ph = [math.atan2(v.imag, v.real) for v in z]

    def robust_sigma(v):
        n = len(v)
        # 5-point quadratic detrend SSE (2 dof)
        s2 = []
        for i in range(2, n - 2):
            win = v[i - 2:i + 3]
            p1 = (-2, -1, 0, 1, 2)
            p2 = (2, -1, -2, -1, 2)
            yy = a0 = a1 = a2 = 0.0
            for k in range(5):
                yy += win[k] * win[k]
                a0 += win[k]
                a1 += win[k] * p1[k]
                a2 += win[k] * p2[k]
            s2.append((yy - a0 * a0 / 5.0 - a1 * a1 / 10.0 - a2 * a2 / 14.0) / 2.0)
        if len(s2) < 3:
            return 0.0
        s2.sort()
        s5 = s2[len(s2) // 2]
        # 3-point second-difference MAD
        d = [v[i - 1] - 2.0 * v[i] + v[i + 1] for i in range(1, n - 1)]
        ds = sorted(d)
        med = ds[len(ds) // 2]
        ad = sorted(abs(x - med) for x in d)
        s3 = ad[len(ad) // 2] * 1.4826 / math.sqrt(6.0)
        s3 *= s3
        s2min = min(s5, s3)
        if not s2min > 0.0:
            return 0.0
        return math.sqrt(s2min)

    s_ln = robust_sigma(y)
    s_phi = robust_sigma(ph)
    s = math.sqrt(s_ln * s_ln + s_phi * s_phi)
    if not math.isfinite(s):
        return -1.0
    return min(max(s, 1e-9), 0.5)


def load_csv(path):
    f, re_, im = [], [], []
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            p = line.split(',')
            f.append(float(p[0]))
            re_.append(float(p[1]))
            im.append(float(p[2]))
    return f, [complex(a, b) for a, b in zip(re_, im)]


if __name__ == '__main__':
    base = sys.argv[1] if len(sys.argv) > 1 else '../../examples'
    refs = {1: 0.0092, 2: 0.019, 3: 0.0213, 4: 0.005}  # reachable wRMSE
    for i in (1, 2, 3, 4):
        _, z = load_csv(f'{base}/data{i}.csv')
        s = estimate_relative_noise(z)
        print(f'data{i}: sigma_hat={s:.4%}  reachable_ref={refs[i]:.2%}  '
              f'ratio={s/refs[i]:.2f}')
