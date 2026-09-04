"""Tests for fit_engine_b.py: SK rational fit + Foster I/II synthesis."""

import numpy as np
import pytest

from rlc_id import synthetic
from rlc_id.circuits import canonical, evaluate, normalize
from rlc_id.fit_engine_a import default_weights
from rlc_id.fit_engine_b import (RationalModel, conservative_energy_bound,
                                 foster_candidates, sk_rational_fit)

DUTS = {d.name: d for d in synthetic.make_duts()}


def _fit(dut, sigma_rel=0.0, max_order=4):
    f, z = synthetic.measure(dut, sigma_rel=sigma_rel)
    w = 2 * np.pi * f
    return foster_candidates(w, z, default_weights(z), max_order=max_order)


class TestRationalFit:
    def test_rc_pole_recovery(self):
        """R||C has one real pole at -1/(RC); SK must find it."""
        dut = DUTS["dut3a_par_RC"]
        _, zm, _ = _fit(dut, sigma_rel=0.0)
        assert len(zm.poles) == 1
        p_true = -1.0 / (1e3 * 1e-8)
        assert abs(zm.poles[0].real - p_true) / abs(p_true) < 1e-3

    def test_tank_pole_pair(self):
        """R||L||C has a complex pole pair at the tank resonance."""
        dut = DUTS["dut7_tank"]
        _, zm, _ = _fit(dut, sigma_rel=0.0)
        assert len(zm.poles) == 2
        w0 = 1.0 / np.sqrt(1e-5 * 1e-10)
        assert abs(abs(zm.poles[0]) - w0) / w0 < 1e-2

    def test_parsimony_rejects_overfit(self):
        """Order scan must not select a higher order than needed (D10).
        A real inductor Rd + sL has no finite poles at all."""
        dut = DUTS["dut1b_L"]
        _, zm, _ = _fit(dut, sigma_rel=0.005)
        assert len(zm.poles) == 0


class TestFosterSynthesis:
    def test_foster1_rc_values(self):
        """Foster I on R||C: series C=1/rho, R=rho/a  (§6.2 table)."""
        dut = DUTS["dut3a_par_RC"]
        cands, zm, _ = _fit(dut, sigma_rel=0.0)
        f1 = [c for c in cands if not c.skipped and "Foster-I" in c.note]
        assert f1, "Foster-I should not be skipped for R||C"
        assert f1[0].canonical == canonical(dut.tree)
        assert synthetic.max_param_error(f1[0].theta, dut) < 1e-3

    def test_foster2_lossy_inductor_is_one_device(self):
        """Foster II on a real inductor: the Y real pole maps to a SINGLE
        L device carrying [L, Rd] (v2 R4 absorption), not R + L leaves."""
        dut = DUTS["dut1b_L"]  # 1 mH, 5 ohm winding
        cands, _, _ = _fit(dut, sigma_rel=0.0)
        ok = [c for c in cands if not c.skipped]
        assert ok, "at least one Foster candidate must succeed for Rd+sL"
        best = min(ok, key=lambda c: c.wrmse)
        assert best.canonical == "L"          # ONE device, two parameters
        assert best.wrmse < 1e-8
        assert synthetic.max_param_error(best.theta, dut) < 1e-3

    def test_foster2_lossy_branch_carries_dcr(self):
        """DUT4 (L||C with winding Rd): Foster must emit an L device whose
        dcr is nonzero when the data demand it."""
        dut = DUTS["dut4_ind_parasitic"]
        cands, _, _ = _fit(dut, sigma_rel=0.0)
        ok = [c for c in cands if not c.skipped and c.wrmse < 1e-6]
        assert ok
        vals = ok[0].values
        # theta layout [.., log L, log Rd, ..]: every Rd entry finite > 0
        from rlc_id.circuits import param_kinds
        rds = [v for v, k in zip(vals, param_kinds(ok[0].tree)) if k == "Rd"]
        assert rds and all(r > 0 for r in rds)

    def test_foster1_lossy_tank_family(self):
        """dut8 (double lossy tanks): Foster-I must synthesize the truth
        canonical S(P(C,L(Rd)),P(C,L(Rd)),R) via the v2 lossy-tank family
        (conjugate pairs with c = 2*alpha*rho_r) -- this candidate is also
        the start that lets engine A converge on this multimodal DUT."""
        dut = DUTS["dut8_double_peak"]
        cands, _, _ = _fit(dut, sigma_rel=0.0)
        ok = [c for c in cands if not c.skipped]
        assert ok, "Foster-I must not skip the lossy double tank"
        best = min(ok, key=lambda c: c.wrmse)
        assert best.canonical == canonical(dut.tree)
        assert best.wrmse < 1e-8
        assert synthetic.max_param_error(best.theta, dut) < 1e-3

    def test_no_negative_elements(self):
        """Decision D8: any emitted candidate must have positive values."""
        for name in ["dut1a_R", "dut3b_par_RL", "dut7_tank"]:
            cands, _, _ = _fit(DUTS[name], sigma_rel=0.005)
            for c in cands:
                if not c.skipped:
                    assert np.all(np.isfinite(c.theta))
                    assert np.all(c.values > 0)

    def test_candidates_are_canonical(self):
        """Emitted trees already satisfy R2'/R4 (no SER(R, L) leaves)."""
        for dut in DUTS.values():
            cands, _, _ = _fit(dut, sigma_rel=0.005)
            for c in cands:
                if not c.skipped:
                    assert canonical(normalize(c.tree)) == c.canonical, \
                        f"non-canonical Foster tree for {dut.name}"


def test_conservative_energy_bound():
    """F3 bound must never exceed the true reactive count (robustness)."""
    dut = DUTS["dut3a_par_RC"]  # 1 capacitor -> bound must be <= 1
    f, z = synthetic.measure(dut, sigma_rel=0.005)
    w = 2 * np.pi * f
    s = 1j * w
    zm = sk_rational_fit(w, z, default_weights(z))
    assert conservative_energy_bound(zm, s) <= 1
