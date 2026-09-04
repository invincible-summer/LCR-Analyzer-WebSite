"""Tests for iofmt.py: unified inputs (../../INPUT_FORMAT.md).

Measurement round-trip/validation, topology matrix+queue parsing and
round-trip, and the end-to-end smoke: text files -> loaders -> identify
with parameters near truth.
"""

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import numpy as np
import pytest

from topofit_id import FitConfig, identify
from topofit_id.iofmt import (format_measurements, format_topology,
                              parse_measurements, parse_topology)
from topofit_id.synthetic import make_duts, measure

DUTS = {d.name: d for d in make_duts()}

LADDER_EXAMPLE = """\
# topology.txt
3
0 1
2
L
C
R
"""

ALL_PARALLEL_EXAMPLE = """\
2
3
R
C
L
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


class TestTopology:

    def test_spec_ladder_example(self):
        edges = parse_topology(LADDER_EXAMPLE)
        assert edges == [(0, 2, "L"), (1, 2, "C"), (1, 2, "R")]

    def test_spec_parallel_multiedge_example(self):
        edges = parse_topology(ALL_PARALLEL_EXAMPLE)
        assert edges == [(0, 1, "R"), (0, 1, "C"), (0, 1, "L")]

    def test_round_trip_slot_major(self):
        # already slot-major input -> exact order round-trip
        edges = [(0, 2, "L"), (1, 2, "C"), (1, 2, "R")]
        assert parse_topology(format_topology(edges)) == edges

    def test_round_trip_reorders_to_slot_major(self):
        # scrambled input order -> same wiring, canonical slot-major order
        # (stable: within a slot the input order R-then-C is preserved)
        edges = [(2, 1, "R"), (0, 2, "L"), (2, 1, "C")]
        assert parse_topology(format_topology(edges)) == \
            [(0, 2, "L"), (1, 2, "R"), (1, 2, "C")]

    def test_round_trip_idempotent(self):
        edges = parse_topology(format_topology(DUTS["bridge"].edges))
        assert parse_topology(format_topology(edges)) == edges

    def test_all_dut_wirings_round_trip(self):
        for dut in DUTS.values():
            rebuilt = parse_topology(format_topology(dut.edges))
            # same wiring: same slot-major (u, v, kind) multiset
            assert sorted(rebuilt) == sorted(
                (min(u, v), max(u, v), k) for u, v, k in dut.edges)

    def test_sparse_labels(self):
        # nested_red labels nodes 0/1/4/5 -> V=6 in the file; the dangling C
        # edge IS part of the input wiring (dropping it is reduce_graph's job)
        text = format_topology(DUTS["nested_red"].edges)
        assert text.splitlines()[0] == "6"
        edges = parse_topology(text)
        labels = {n for u, v, _ in edges for n in (u, v)}
        assert max(labels) == 5
        assert 2 not in labels and 3 not in labels

    @pytest.mark.parametrize("text", [
        "", "1\n", "x\n",                    # empty / V too small / V not int
        "3\n0 1\n2\n",                        # queue missing entirely
        "3\n0 1\n2\nL\nC\nX\n",               # bad edge type
        "3\n0 1 2\n2\nL\nC\nR\n",             # row 0 wrong length
        "3\n0 1\n2 3\nL\nC\nR\n",             # row 1 wrong length
        "3\n0 1\n-2\nL\nC\nR\n",              # negative count
        "3\n0 1\n2\nL\nC\n",                  # queue too short
        "3\n0 1\n2\nL\nC\nR\nR\n",            # queue too long
        "2\nx\nR\n",                          # non-integer count
    ])
    def test_validation_errors(self, text):
        with pytest.raises(ValueError):
            parse_topology(text)


class TestEndToEnd:

    def test_text_inputs_identify(self):
        """Spec sec.2.3 smoke: dump ser_rc's inputs to text, reload, fit."""
        dut = DUTS["ser_rc"]
        f, z = measure(dut, sigma_rel=0.005, seed=0)
        f2, z2 = parse_measurements(format_measurements(f, z))
        edges2 = parse_topology(format_topology(dut.edges))
        r = identify(f2, z2, edges2, FitConfig(seed=1))
        assert r.ok
        assert r.wrmse < 0.012                    # noise floor
        vals = {g.kind: g.value for g in r.groups}
        assert vals["R"][1] == pytest.approx(1e3, rel=2e-3)
        assert vals["C"][1] == pytest.approx(100e-9, rel=2e-3)
