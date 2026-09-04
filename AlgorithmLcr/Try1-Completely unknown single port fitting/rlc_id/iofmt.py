"""Unified measurement input (../../INPUT_FORMAT.md section 1).

Text form: first line n, then exactly n lines ``f Rz Iz`` (frequency [Hz],
Re(Z) [ohm], Im(Z) [ohm]).  ``#`` starts a comment (whole line or trailing),
blank lines are ignored, fields are whitespace separated.  Dump uses %.17g
so load(dump(x)) == x bit-for-bit.
"""

from __future__ import annotations

import numpy as np


def _content_lines(text: str) -> list[str]:
    """Strip comments and blank lines (INPUT_FORMAT.md sec.0)."""
    out = []
    for raw in text.splitlines():
        s = raw.split("#", 1)[0].strip()
        if s:
            out.append(s)
    return out


def format_measurements(f, z) -> str:
    """Serialize (f, z) to the unified text form."""
    f = np.asarray(f, dtype=float)
    z = np.asarray(z, dtype=complex)
    if len(f) != len(z):
        raise ValueError(f"f has {len(f)} points, z has {len(z)}")
    if len(f) == 0:
        raise ValueError("need at least one measurement point")
    lines = [str(len(f))]
    for fk, zk in zip(f, z):
        if not (np.isfinite(fk) and fk > 0.0):
            raise ValueError(f"frequency must be positive and finite, got {fk}")
        if not (np.isfinite(zk.real) and np.isfinite(zk.imag)):
            raise ValueError(f"impedance parts must be finite, got {zk}")
        lines.append(f"{fk:.17g} {zk.real:.17g} {zk.imag:.17g}")
    return "\n".join(lines) + "\n"


def parse_measurements(text: str) -> tuple[np.ndarray, np.ndarray]:
    """Parse the unified text form -> (f, z).  Validation per sec.3."""
    rows = _content_lines(text)
    if not rows:
        raise ValueError("empty measurement input")
    try:
        n = int(rows[0])
    except ValueError:
        raise ValueError(f"first line must be integer n, got {rows[0]!r}") from None
    if n < 1:
        raise ValueError(f"n must be a positive integer, got {n}")
    data = rows[1:]
    if len(data) != n:
        raise ValueError(f"n={n} but {len(data)} data lines follow")
    fs, reals, imags = [], [], []
    for k, line in enumerate(data, start=2):  # 1-based line no. incl. n-line
        parts = line.split()
        if len(parts) != 3:
            raise ValueError(f"line {k}: expected 'f Rz Iz', got {line!r}")
        try:
            fv, rz, iz = (float(x) for x in parts)
        except ValueError:
            raise ValueError(f"line {k}: non-numeric field in {line!r}") from None
        if not np.isfinite([fv, rz, iz]).all():
            raise ValueError(f"line {k}: non-finite value in {line!r}")
        if fv <= 0.0:
            raise ValueError(f"line {k}: frequency must be > 0, got {fv}")
        fs.append(fv)
        reals.append(rz)
        imags.append(iz)
    return (np.asarray(fs, dtype=float),
            np.asarray(reals) + 1j * np.asarray(imags))


def load_measurements(path) -> tuple[np.ndarray, np.ndarray]:
    """Read the unified measurement file from disk."""
    with open(path, "r", encoding="utf-8") as fh:
        return parse_measurements(fh.read())
