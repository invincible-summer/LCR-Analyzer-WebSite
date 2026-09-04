"""Tests for iofmt.py: unified measurement input (../../INPUT_FORMAT.md)."""

import numpy as np
import pytest

from rlc_id.iofmt import format_measurements, parse_measurements

SPEC_EXAMPLE = """\
# measurements.txt
2
1.0e+03  9.98e+02  -1.2e-01
1.0e+04  6.13e+02  -4.88e+02
"""


class TestMeasurements:

    def test_spec_example(self):
        f, z = parse_measurements(SPEC_EXAMPLE)
        np.testing.assert_array_equal(f, [1.0e3, 1.0e4])
        np.testing.assert_array_equal(z, [998.0 - 0.12j, 613.0 - 488.0j])

    def test_round_trip_bit_exact(self):
        rng = np.random.default_rng(0)
        f = np.logspace(1.0, 7.0, 30)
        z = rng.standard_normal(30) + 1j * rng.standard_normal(30)
        f2, z2 = parse_measurements(format_measurements(f, z))
        assert f2.tobytes() == f.astype(float).tobytes()   # bit-for-bit
        assert z2.tobytes() == z.astype(complex).tobytes()

    def test_comments_and_blank_lines_ignored(self):
        f, z = parse_measurements("\n# lead comment\n1\n\n5  1  2  # trailing\n")
        assert f[0] == 5.0 and z[0] == 1.0 + 2.0j

    @pytest.mark.parametrize("text,why", [
        ("", "empty input"),
        ("x\n1 2 3\n", "n not an integer"),
        ("0\n", "n must be positive"),
        ("3\n1 2 3\n4 5 6\n", "line count mismatch"),
        ("1\n1 2\n", "wrong field count"),
        ("1\n1 two 3\n", "non-numeric field"),
        ("1\n0 1 2\n", "frequency must be > 0"),
        ("1\ninf 1 2\n", "non-finite value"),
    ])
    def test_validation_errors(self, text, why):
        with pytest.raises(ValueError):
            parse_measurements(text)

    def test_dump_validates_too(self):
        with pytest.raises(ValueError):
            format_measurements([-1.0], [1.0 + 2.0j])
