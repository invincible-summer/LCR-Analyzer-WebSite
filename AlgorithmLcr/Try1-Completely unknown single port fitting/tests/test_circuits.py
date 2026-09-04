"""Tests for circuits.py: normalization (R1-R4), evaluation, Jacobian."""

import numpy as np
import pytest

from rlc_id.circuits import (SER, PAR, DCR_BOUNDS, Leaf, assemble, bounds,
                             canonical, evaluate, evaluate_jac, make_node,
                             n_leaves, n_params, normalize, param_kinds,
                             to_string)


class TestCanonicalNormalization:
    """R1/R2'/R3/R4: electrically equivalent trees map to one canonical form."""

    def test_r1_flattens_same_kind_nesting(self):
        # SER(R, SER(L, C)) == SER(L, C): after flattening, the series R is
        # absorbed into the L device's DCR (R4) -- one device fewer
        t1 = make_node(SER, [Leaf("R"), make_node(SER, [Leaf("L"), Leaf("C")])])
        t2 = make_node(SER, [Leaf("L"), Leaf("C")])
        assert canonical(normalize(t1)) == canonical(t2)

    def test_r2_merges_duplicate_leaf_kinds(self):
        # SER(R, R, L): both series R merge (R2'), then the survivor folds
        # into the L's DCR (R4) -> a single L device
        t1 = make_node(SER, [Leaf("R"), Leaf("R"), Leaf("L")])
        assert normalize(t1) == Leaf("L")

    def test_r4_absorbs_series_r_into_l(self):
        # R + (L, Rd) == (L, Rd + R): ONE device
        t = make_node(SER, [Leaf("R"), Leaf("L")])
        assert normalize(t) == Leaf("L")

    def test_r4_deeper_series_chain(self):
        # S(R, S(L, C)) flattens to S(R, L, C), R folds into the L's DCR
        t = make_node(SER, [Leaf("R"), make_node(SER, [Leaf("L"), Leaf("C")])])
        assert canonical(normalize(t)) == "S(C,L)"

    def test_r4_not_applied_in_parallel(self):
        # R || L stays two devices (absorption is a series-only identity)
        t = make_node(PAR, [Leaf("R"), Leaf("L")])
        assert canonical(normalize(t)) == "P(L,R)"

    def test_r4_not_applied_across_par_child(self):
        # S(R, P(L, C)): R is in series with the tank, not with the L alone
        t = make_node(SER, [Leaf("R"),
                            make_node(PAR, [Leaf("L"), Leaf("C")])])
        assert canonical(normalize(t)) == "S(P(C,L),R)"

    def test_r2prime_keeps_parallel_l_leaves(self):
        # two (L + Rd) devices in parallel form a second-order tank: kept
        t = make_node(PAR, [Leaf("L"), Leaf("L")])
        assert canonical(normalize(t)) == "P(L,L)"

    def test_r2prime_merges_parallel_r_and_c(self):
        assert normalize(make_node(PAR, [Leaf("R"), Leaf("R")])) == Leaf("R")
        assert normalize(make_node(PAR, [Leaf("C"), Leaf("C")])) == Leaf("C")

    def test_r2prime_merges_series_l_leaves(self):
        # series L devices merge (both L and Rd add in series)
        assert normalize(make_node(SER, [Leaf("L"), Leaf("L")])) == Leaf("L")

    def test_r3_child_order_irrelevant(self):
        t1 = make_node(SER, [Leaf("R"), Leaf("C")])
        t2 = make_node(SER, [Leaf("C"), Leaf("R")])
        assert canonical(t1) == canonical(t2)

    def test_normalize_idempotent(self):
        t = make_node(PAR, [Leaf("L"),
                            make_node(SER, [Leaf("L"), Leaf("R")]),
                            make_node(PAR, [Leaf("L"), Leaf("C")])])
        n1 = normalize(t)
        assert canonical(normalize(n1)) == canonical(n1)

    def test_single_child_collapses(self):
        t = normalize(make_node(SER, [Leaf("R"), Leaf("R")]))
        assert isinstance(t, Leaf) and t.kind == "R"


class TestParameterLayout:
    """L devices carry two parameters (L and Rd); device count differs."""

    def test_param_counts(self):
        tree, _ = assemble(PAR, [(Leaf("R"), [1e3]),
                                 (Leaf("L"), [1e-3, 5.0]),
                                 (Leaf("C"), [1e-8])])
        assert n_leaves(tree) == 3
        assert n_params(tree) == 4
        assert param_kinds(tree) == ["C", "L", "Rd", "R"]

    def test_param_kinds_multi_l(self):
        tree, _ = assemble(PAR, [(Leaf("L"), [1e-3, 5.0]),
                                 (Leaf("L"), [1e-5, 0.5])])
        assert param_kinds(tree) == ["L", "Rd", "L", "Rd"]
        assert n_leaves(tree) == 2 and n_params(tree) == 4

    def test_bounds_rows(self):
        lb, ub = bounds(Leaf("L"))
        assert lb[0] == -10.0 and ub[0] == 1.0
        assert (lb[1], ub[1]) == DCR_BOUNDS


class TestEvaluation:
    """Z(s) evaluation against hand-computed values."""

    def test_single_elements(self):
        s = 1j * 2 * np.pi * np.array([1e3])
        assert evaluate(Leaf("R"), [2.0], s) == pytest.approx([100.0])
        # L device: [log10 L, log10 Rd] -> Z = Rd + s L
        theta_l = [np.log10(1e-3), np.log10(5.0)]
        assert evaluate(Leaf("L"), theta_l, s) == pytest.approx(
            [5.0 + 1j * 2 * np.pi * 1e3 * 1e-3])
        assert evaluate(Leaf("C"), [-6.0], s) == pytest.approx(
            [1.0 / (1j * 2 * np.pi * 1e3 * 1e-6)])

    def test_series_absorption_equivalence(self):
        # R + L(dcr) has EXACTLY the same Z as L(dcr + R): one device
        w = 2 * np.pi * np.logspace(2, 6, 33)
        s = 1j * w
        t_two, v_two = assemble(SER, [(Leaf("R"), [7.0]),
                                      (Leaf("L"), [1e-4, 0.5])])
        t_one = Leaf("L")
        theta_one = np.log10([1e-4, 7.5])
        assert evaluate(t_two, np.log10(v_two), s) == \
            pytest.approx(evaluate(t_one, theta_one, s), rel=1e-12)

    def test_series_parallel(self):
        # R + (L || C): hand value at w=1e5 (below the 1e6 resonance)
        w = 1e5
        s = 1j * w * np.ones(1)
        R, L, Rd, C = 50.0, 1e-3, 0.2, 1e-9
        tree, vals = assemble(SER, [(Leaf("R"), [R]),
                                    assemble(PAR, [(Leaf("L"), [L, Rd]),
                                                   (Leaf("C"), [C])])])
        theta = np.log10(vals)
        zl = Rd + 1j * w * L
        zc = 1.0 / (1j * w * C)
        expected = R + zl * zc / (zl + zc)
        assert evaluate(tree, theta, s) == pytest.approx([expected])

    def test_parallel_ll_is_second_order(self):
        # two (L + Rd) in parallel are NOT one inductor: the apparent
        # inductance Im(Z)/w varies across the band (a single real RL device
        # has Im(Z)/w ~ const), here by a large factor
        w = 2 * np.pi * np.logspace(2, 6, 200)
        s = 1j * w
        tree, vals = assemble(PAR, [(Leaf("L"), [1e-3, 5.0]),
                                    (Leaf("L"), [1e-5, 0.5])])
        z = evaluate(tree, np.log10(vals), s)
        lw = np.imag(z) / w
        assert lw.max() / lw.min() > 1.5  # apparent L varies: 2nd-order tank

    def test_evaluate_jac_matches_fd(self):
        # forward-AD Jacobian vs central finite difference (Rd included)
        tree, vals = assemble(PAR, [(Leaf("R"), [1e3]),
                                    assemble(SER, [(Leaf("L"), [1e-5, 0.3]),
                                                   (Leaf("C"), [1e-10])])])
        theta = np.log10(vals)
        assert len(theta) == 4
        f = np.geomspace(1e2, 1e7, 25)
        s = 1j * 2 * np.pi * f
        _, J_ad = evaluate_jac(tree, theta, s)
        scale = float(np.max(np.abs(J_ad)))
        h = 1e-6
        max_rel = 0.0
        for i in range(len(theta)):
            tp = theta.copy(); tp[i] += h
            tm = theta.copy(); tm[i] -= h
            fd = (evaluate(tree, tp, s) - evaluate(tree, tm, s)) / (2 * h)
            # only entries resolvable by FD (above its cancellation floor)
            # are compared relatively; below it, AD must simply be small
            mask = np.abs(fd) > 1e-4 * scale
            if mask.any():
                rel = np.abs(J_ad[i] - fd)[mask] / np.abs(fd)[mask]
                max_rel = max(max_rel, float(rel.max()))
            rest = np.abs(J_ad[i])[~mask] if (~mask).any() else np.array([0.0])
            assert float(rest.max()) < 1e-3 * scale
        # FD reference itself carries O(h^2) truncation error near the
        # resonance; AD is exact, so a 1e-3 bound is a true FD-precision test
        assert max_rel < 1e-3


class TestPrinting:
    def test_to_string_roundtrip_shapes(self):
        tree, vals = assemble(SER, [(Leaf("R"), [100.0]),
                                    assemble(PAR, [(Leaf("C"), [1e-8]),
                                                   (Leaf("L"), [1e-5, 2.0])])])
        text = to_string(tree, np.log10(vals))
        assert "R(100)" in text and "||" in text and "+" in text
        assert "Rd" in text  # L devices print their DC resistance
