"""End-to-end identification tests (DESIGN.md §8.2).

Acceptance criteria:
  * noiseless (sigma=0): top-1 topology == truth (or equivalent), parameter
    error < 1e-4;
  * noisy (sigma=0.5%): top-1 topology == truth (or electrically equivalent
    within 2% on the expanded validation grid), parameter error < 2% when the
    exact topology is recovered.
"""

import numpy as np
import pytest

from rlc_id import Config, identify, synthetic
from rlc_id.circuits import canonical
from rlc_id.fit_engine_a import Candidate
from rlc_id.selector import are_equivalent, make_validation_grid

DUTS = {d.name: d for d in synthetic.make_duts()}
ALL_NAMES = list(DUTS)


def _run(name, sigma_rel):
    from rlc_id.circuits import n_leaves
    dut = DUTS[name]
    f, z = synthetic.measure(dut, sigma_rel=sigma_rel)
    cfg = Config(max_n=5 if n_leaves(dut.tree) > 4 else 4)
    return dut, f, identify(f, z, config=cfg)


def _truth_candidate(dut):
    return Candidate(tree=dut.tree, theta=dut.theta, rss=0.0, aicc_val=0.0,
                     wrmse=0.0, max_rel_err=0.0)


def _top1_matches(dut, res, f, tol):
    best_cls = res.classes[0]
    truth = _truth_candidate(dut)
    fg = make_validation_grid(f)
    if canonical(best_cls.representative.tree) == canonical(dut.tree):
        return True
    return any(are_equivalent(m, truth, fg, tol=tol)
               for m in best_cls.members)


@pytest.mark.parametrize("name", ALL_NAMES)
def test_noiseless(name):
    dut, f, res = _run(name, sigma_rel=0.0)
    assert _top1_matches(dut, res, f, tol=1e-6), \
        f"{name}: top-1 miss on noiseless data"
    rep = res.classes[0].representative
    if canonical(rep.tree) == canonical(dut.tree):
        assert synthetic.max_param_error(rep.theta, dut) < 1e-4


@pytest.mark.parametrize("name", ALL_NAMES)
def test_noisy(name):
    dut, f, res = _run(name, sigma_rel=0.005)
    assert _top1_matches(dut, res, f, tol=2e-2), \
        f"{name}: top-1 miss at 0.5% noise"
    rep = res.classes[0].representative
    if canonical(rep.tree) == canonical(dut.tree):
        assert synthetic.max_param_error(rep.theta, dut) < 0.02
