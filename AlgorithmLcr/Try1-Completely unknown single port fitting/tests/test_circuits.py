"""Tests for circuits.py: normalization (R1-R3), evaluation, Jacobian."""

import numpy as np
import pytest

from rlc_id.circuits import (SER, PAR, Leaf, assemble, canonical, evaluate,
                             evaluate_jac, make_node, n_leaves, normalize,
                             to_string)


class TestCanonicalNormalization:
    """R1/R2/R3: electrically equivalent trees map to one canonical form."""

    def test_r1_flattens_same_kind_nesting(self):
        # SER(R, SER(L, C)) == SER(R, L, C)
        t1 = make_node(SER, [Leaf("R"), make_node(SER, [Leaf("L"), Leaf("C")])])
        t2 = make_node(SER, [Leaf("R"), Leaf("L"), Leaf("C")])
        assert canonical(normalize(t1)) == canonical(t2)

    def test_r2_merges_duplicate_leaf_kinds(self):
        # SER(R, R, L) == SER(R, L)  (two series R merge into one)
        t1 = make_node(SER, [Leaf("R"), Leaf("R"), Leaf("L")])
        t2 = make_node(SER, [Leaf("R"), Leaf("L")])
        assert canonical(normalize(t1)) == canonical(t2)

    def test_r3_child_order_irrelevant(self):
        t1 = make_node(SER, [Leaf("R"), Leaf("L")])
        t2 = make_node(SER, [Leaf("L"), Leaf("R")])
        assert canonical(t1) == canonical(t2)

    def test_normalize_idempotent(self):
        t = make_node(PAR, [Leaf("C"),
                            make_node(SER, [Leaf("R"), Leaf("L")])])
        assert canonical(normalize(normalize(t))) == canonical(normalize(t))

    def test_single_child_collapses(self):
        t = normalize(make_node(SER, [Leaf("R"), Leaf("R")]))
        assert isinstance(t, Leaf) and t.kind == "R"


class TestEvaluation:
    """Z(s) evaluation against hand-computed values."""

    def test_single_elements(self):
        s = 1j * 2 * np.pi * np.array([1e3])
        assert evaluate(Leaf("R"), [2.0], s) == pytest.approx([100.0])
        assert evaluate(Leaf("L"), [-3.0], s) == pytest.approx(
            [1j * 2 * np.pi * 1e3 * 1e-3])
        assert evaluate(Leaf("C"), [-6.0], s) == pytest.approx(
            [1.0 / (1j * 2 * np.pi * 1e3 * 1e-6)])

    def test_series_parallel(self):
        # R + (L || C): hand value at w=1e5 (below the 1e6 resonance)
        w = 1e5
        s = 1j * w * np.ones(1)
        R, L, C = 50.0, 1e-3, 1e-9
        tree, vals = assemble(SER, [(Leaf("R"), [R]),
                                    assemble(PAR, [(Leaf("L"), [L]),
                                                   (Leaf("C"), [C])])])
        theta = np.log10(vals)
        zl = 1j * w * L
        zc = 1.0 / (1j * w * C)
        expected = R + zl * zc / (zl + zc)
        assert evaluate(tree, theta, s) == pytest.approx([expected])

    def test_evaluate_jac_matches_fd(self):
        # forward-AD Jacobian vs central finite difference
        tree, vals = assemble(PAR, [(Leaf("R"), [1e3]),
                                    assemble(SER, [(Leaf("L"), [1e-5]),
                                                   (Leaf("C"), [1e-10])])])
        theta = np.log10(vals)
        f = np.geomspace(1e2, 1e7, 25)
        s = 1j * 2 * np.pi * f
        _, J_ad = evaluate_jac(tree, theta, s)
        h = 1e-6
        max_rel = 0.0
        for i in range(len(theta)):
            tp = theta.copy(); tp[i] += h
            tm = theta.copy(); tm[i] -= h
            fd = (evaluate(tree, tp, s) - evaluate(tree, tm, s)) / (2 * h)
            denom = np.maximum(np.abs(fd), 1e-6 * np.max(np.abs(fd)))
            max_rel = max(max_rel,
                          float(np.max(np.abs(J_ad[i] - fd) / denom)))
        # FD reference itself carries O(h^2) truncation error near the
        # resonance; AD is exact, so a 1e-3 bound is a true FD-precision test
        assert max_rel < 1e-3


class TestPrinting:
    def test_to_string_roundtrip_shapes(self):
        tree, vals = assemble(SER, [(Leaf("R"), [100.0]),
                                    assemble(PAR, [(Leaf("C"), [1e-8]),
                                                   (Leaf("R"), [1e3])])])
        text = to_string(tree, np.log10(vals))
        assert "R(100)" in text and "||" in text and "+" in text
