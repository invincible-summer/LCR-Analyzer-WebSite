"""End-to-end: every named DUT under the standard 0.5% noise protocol."""

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import numpy as np
import pytest

from topofit_id import FitConfig, identify
from topofit_id.metric import curve_max_rel_floored, matched_group_errors
from topofit_id.graph import eval_group, reduce_graph
from topofit_id.synthetic import default_frequencies, make_duts, measure

F = default_frequencies()
DUTS = {d.name: d for d in make_duts()}

# curve tolerance: per-point noise 0.5% (Re/Im each), max over 100 points
CURVE_TOL = 0.04
# parameter tolerance only applies to identifiable DUTs (bridge is not)
PARAM_DUTS = [n for n in DUTS if n != "bridge"]


@pytest.mark.parametrize("sigma,seed", [(0.0, 0), (0.005, 0)])
@pytest.mark.parametrize("name", sorted(DUTS))
def test_named_dut_end_to_end(name, sigma, seed):
    dut = DUTS[name]
    f, z = measure(dut, sigma_rel=sigma, seed=seed)
    r = identify(f, z, dut.edges, FitConfig(seed=seed + 1))

    # 1) goodness of fit at / below the noise floor
    floor = 0.0075 if sigma > 0 else 1e-8
    assert r.wrmse < floor, f"{name}: wRMSE {r.wrmse}"

    # 2) dense in-band curve close to truth
    fd = np.logspace(np.log10(10.0), np.log10(10e6), 100)
    zt = dut.z_exact(fd)
    rel = curve_max_rel_floored(zt, r.z_model(fd))
    assert rel < (0.02 if sigma > 0 else 1e-4), f"{name}: curve {rel}"

    # 3) parameter recovery for identifiable DUTs
    if name in PARAM_DUTS:
        red = reduce_graph(dut.edges)
        tv = [eval_group(g.expr, dut.values) for g in red.edges]
        tol = 0.15 if sigma > 0 else 1e-4
        errs, labels = matched_group_errors(r.group_values(), tv, red.edges)
        weak_classes = set()
        for g in r.groups:
            for nm in g.weak_params:
                weak_classes.add(((min(g.u, g.v), max(g.u, g.v), g.kind), nm))
        kept = [e for e, lab in zip(errs, labels) if lab not in weak_classes]
        # noisy tol 0.15: damping parameters (series Rd at a resonance
        # notch) are CRLB-limited; the campaign quantifies the distribution
        if kept:
            assert max(kept) < tol, f"{name}: param err {max(kept)}"


def test_all_duts_noiseless_summary():
    """Sanity: noiseless runs must all be at machine precision on the curve."""
    for name, dut in DUTS.items():
        z = dut.z_exact(F)
        r = identify(F, z, dut.edges, FitConfig(seed=1))
        assert r.wrmse < 1e-8, name
