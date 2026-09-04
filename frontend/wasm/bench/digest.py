#!/usr/bin/env python3
"""digest.py — run fitbench on a CSV and print a compact result digest.

usage: digest.py <csv> try1|try2|try3 [pass-through args...]
"""
import json
import subprocess
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, '..', 'build-native', 'fitbench')


def run(csv, cmd, *args, timeout=600):
    p = subprocess.run([BIN, csv, cmd, *args], capture_output=True, text=True,
                       timeout=timeout)
    if p.returncode != 0:
        return {'ok': False, 'error': p.stderr.strip()[:300]}
    try:
        return json.loads(p.stdout)
    except json.JSONDecodeError as e:
        return {'ok': False, 'error': f'json: {e}: {p.stdout[:200]}'}


def digest(d, show=8):
    if not d.get('ok'):
        print('  ERROR', d.get('code'), d.get('error', '')[:200])
        return
    print('  elapsed %.2fs stats %s' % (d.get('elapsed', -1), d.get('stats')))
    for c in d.get('candidates', [])[:show]:
        row = f"  #{c['rank']} dev={c['devices']} p={c['n_params']} wRMSE={c['wrmse']:.4g} max={c['max_rel']:.3g}"
        if 'topology' in c:
            row += ' ' + c['topology'][:76]
        if 'structure' in c:
            row += ' ' + str(c['structure'])[:60]
        print(row)
    t3 = d.get('try3')
    if t3 is not None:
        print('  try3 diag: ok=%s jac_rank=%s n_passes=%s notes=%s'
              % (t3.get('ok'), t3.get('jac_rank'), t3.get('n_passes'),
                 t3.get('notes', [])[:4]))


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    csv, cmd = sys.argv[1], sys.argv[2]
    d = run(csv, cmd, *sys.argv[3:])
    digest(d)
