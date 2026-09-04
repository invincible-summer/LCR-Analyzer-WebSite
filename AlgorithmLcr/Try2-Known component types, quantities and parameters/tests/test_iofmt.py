"""Tests for iofmt.py: unified inputs (../../INPUT_FORMAT.md).

Measurement round-trip/validation, component-queue parsing and round-trip,
and the end-to-end smoke: text files -> loaders -> identify -> HIT.
"""

from __future__ import annotations

import numpy as np
import pytest

from netgraph_id import identify
from netgraph_id.components import ComponentSet
from netgraph_id.iofmt import (format_components, format_measurements,
                               parse_components, parse_measurements)
from netgraph_id.selector import are_equivalent, make_validation_grid
from netgraph_id.synthetic import make_duts, measure

SPEC_EXAMPLE = """\
# components.txt
R 1.0e+03
C 1.0e-07
L 1.0e-03 5.0
"""


class TestMeasurements:

    def test_round_trip_bit_exact(self):
        rng = np.random.default_rng(0)
        f = np.logspace(1.0, 7.0, 30)
        z = rng.standard_normal(30) + 1j * rng.standard_normal(30)
        f2, z2 = parse_measurements(format_measurements(f, z))
        assert f2.tobytes() == f.astype(float).tobytes()
        assert z2.tobytes() == z.astype(complex).tobytes()

    @pytest.mark.parametrize("text", [
        "", "x\n1 2 3\n", "0\n", "3\n1 2 3\n4 5 6\n",
        "1\n1 2\n", "1\n1 two 3\n", "1\n0 1 2\n", "1\ninf 1 2\n",
    ])
    def test_validation_errors(self, text):
        with pytest.raises(ValueError):
            parse_measurements(text)


class TestComponents:

    def test_spec_example(self):
        cs = parse_components(SPEC_EXAMPLE)
        assert cs.n == 3
        assert [c.kind for c in cs.components] == ["C", "L", "R"]  # canonical
        by_kind = {c.kind: c for c in cs.components}
        assert by_kind["R"].value == 1e3
        assert by_kind["C"].value == 100e-9
        assert (by_kind["L"].value, by_kind["L"].dcr) == (1e-3, 5.0)

    def test_dcr_defaults_to_zero(self):
        cs = parse_components("L 1.0e-03\n")
        assert cs.components[0].dcr == 0.0

    def test_multiset_duplicates_and_order(self):
        a = parse_components("R 10\nR 10\nC 1e-9\n")
        b = parse_components("C 1e-9\nR 10\nR 10\n")
        assert a == b and a.n == 3

    def test_round_trip(self):
        cs = ComponentSet.make(n_R=[1e3, 47e3], n_C=[10e-9],
                               n_L=[(330e-6, 3.0), (1e-3, 0.0)])
        assert parse_components(format_components(cs)) == cs

    @pytest.mark.parametrize("text", [
        "", "X 1\n", "R\n", "R 1 2\n", "C 1 0\n", "R 0\n", "R -1\n",
        "L 1 -2\n", "L 1 2 3\n", "R one\n",
    ])
    def test_validation_errors(self, text):
        with pytest.raises(ValueError):
            parse_components(text)


class TestEndToEnd:

    def test_text_inputs_identify_hit(self):
        """Spec sec.2.2 smoke: dump dut1's inputs to text, reload, identify."""
        dut = {d.name: d for d in make_duts()}["dut1_series_RL"]
        f, z = measure(dut.network, dut.compset, seed=7)
        f2, z2 = parse_measurements(format_measurements(f, z))
        cs2 = parse_components(format_components(dut.compset))
        res = identify(cs2, f2, z2)
        grid = make_validation_grid(f2)
        tol = max(1e-3, 3.0 * res.best.wrmse)
        assert are_equivalent(res.best.representative.network, dut.network,
                              dut.compset, grid, tol)
