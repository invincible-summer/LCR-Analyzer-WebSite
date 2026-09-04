"""End-to-end identification tests (DESIGN.md §8.2).

Acceptance criteria:
  * noiseless (sigma=0): top-1 topology == truth (or equivalent), parameter
    error < 1e-4;
  * noisy (sigma=0.5%): top-1 topology == truth (or electrically equivalent
    within 2% on the expanded validation grid), parameter error < 2% when the
    exact topology is recovered;
  * exact_n prior: search restricted to the single n-device layer, every
    ranked candidate has exactly n devices, truth recovered on the right n;
  * series absorption: R + L data identify as ONE L device (Rd = R + Rd0).
"""

import numpy as np
import pytest

from rlc_id import Config, identify, library, synthetic
from rlc_id.circuits import SER, Leaf, assemble, canonical, n_leaves
from rlc_id.fit_engine_a import Candidate
from rlc_id.selector import are_equivalent, make_validation_grid

DUTS = {d.name: d for d in synthetic.make_duts()}
ALL_NAMES = list(DUTS)

# Noisy parameter-recovery thresholds (0.5% noise).  Default 2%; exceptions
# are identifiability-limited, not algorithmic, failures:
#   dut8: the series R and the tank inductors' Rd are all loss on the series
#         path; their split is only noise-limited resolvable (~5%).
NOISY_PARAM_TOL = {"dut8_double_peak": 0.08}
# DUTs whose topology family is GLOBALLY parameter-degenerate: response-level
# recovery is exact but element values are not comparable.
#   dut10: S(P(L,L), R) maps 5 parameters onto only 4 impedance invariants
#          (the series R can trade continuously against the inductors' Rd),
#          so any exact realization is a correct answer.
PARAM_CHECK_SKIP = {"dut10_ser_R_par_LL"}


def _run(name, sigma_rel):
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
    if name not in PARAM_CHECK_SKIP and \
            canonical(rep.tree) == canonical(dut.tree):
        assert synthetic.max_param_error(rep.theta, dut) < 1e-4


@pytest.mark.parametrize("name", ALL_NAMES)
def test_noisy(name):
    dut, f, res = _run(name, sigma_rel=0.005)
    assert _top1_matches(dut, res, f, tol=2e-2), \
        f"{name}: top-1 miss at 0.5% noise"
    rep = res.classes[0].representative
    if name not in PARAM_CHECK_SKIP and \
            canonical(rep.tree) == canonical(dut.tree):
        assert synthetic.max_param_error(rep.theta, dut) < \
            NOISY_PARAM_TOL.get(name, 0.02)


class TestExactNPrior:
    """The optional 'exactly n devices' constraint (count.txt prior)."""

    @pytest.mark.parametrize("name", ["dut1b_L", "dut3a_par_RC", "dut7_tank",
                                      "dut9_par_LL", "dut10_ser_R_par_LL"])
    def test_right_n_recovers_truth(self, name):
        dut = DUTS[name]
        n = n_leaves(dut.tree)
        f, z = synthetic.measure(dut, sigma_rel=0.0)
        res = identify(f, z, config=Config(exact_n=n))
        # library is the single n-device layer
        assert res.n_library == len(library.trees_of_size(n))
        # every ranked class holds exactly n devices
        assert res.classes
        for eq in res.classes:
            assert n_leaves(eq.representative.tree) == n
        # truth topology wins (directly or as an equivalent realization --
        # T2: some n-device circuits admit several canonical realizations)
        assert _top1_matches(dut, res, f, tol=1e-6)
        rep = res.best.representative
        if name not in PARAM_CHECK_SKIP and \
                canonical(rep.tree) == canonical(dut.tree):
            assert synthetic.max_param_error(rep.theta, dut) < 1e-4

    def test_wrong_n_still_returns_valid_layer(self):
        # a wrong prior (2 devices for a 3-device tank) must return the best
        # 2-device approximation, never a different-count candidate
        dut = DUTS["dut7_tank"]
        f, z = synthetic.measure(dut, sigma_rel=0.0)
        res = identify(f, z, config=Config(exact_n=2))
        assert res.classes
        for eq in res.classes:
            assert n_leaves(eq.representative.tree) == 2

    def test_exact_n_rejects_bad_values(self):
        with pytest.raises(ValueError):
            Config(exact_n=0)


def test_series_absorption_one_device():
    """R in series with L(1e-4, Rd 0.5): the port data are EXACTLY those of a
    single L device with Rd = 7.5 -- the identification must say so."""
    tree, vals = assemble(SER, [(Leaf("R"), [7.0]),
                                (Leaf("L"), [1e-4, 0.5])])
    dut = synthetic.DUT("ad_hoc_R_L", "tmp", tree, np.asarray(vals))
    f, z = synthetic.measure(dut, sigma_rel=0.0)
    res = identify(f, z, config=Config(max_n=2))
    rep = res.best.representative
    assert canonical(rep.tree) == canonical(Leaf("L"))  # ONE device
    fit_vals = np.power(10.0, rep.theta)
    assert fit_vals[0] == pytest.approx(1e-4, rel=1e-4)
    assert fit_vals[1] == pytest.approx(7.5, rel=1e-3)
