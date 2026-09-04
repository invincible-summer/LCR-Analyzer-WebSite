"""Unified inputs (../../INPUT_FORMAT.md): measurements + component queue.

Measurements: first line n, then n lines ``f Rz Iz`` (sec.1).
Components: one Edge per line ``type parameter [dcr]`` with no node
information (sec.2.2) -- the wiring is what the search must find.

``#`` starts a comment (whole line or trailing), blank lines are ignored.
Dump uses %.17g so load(dump(x)) == x bit-for-bit.
"""

from __future__ import annotations

import numpy as np

from .components import Component, ComponentSet


def _content_lines(text: str) -> list[str]:
    """Strip comments and blank lines (INPUT_FORMAT.md sec.0)."""
    out = []
    for raw in text.splitlines():
        s = raw.split("#", 1)[0].strip()
        if s:
            out.append(s)
    return out


# ---------------------------------------------------------------------------
# measurements (sec.1)
# ---------------------------------------------------------------------------

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


# ---------------------------------------------------------------------------
# component queue (sec.2.2)
# ---------------------------------------------------------------------------

def format_components(compset: ComponentSet) -> str:
    """Serialize the known component multiset, one Edge per line.

    Line order is the ComponentSet canonical order; L lines always carry
    dcr, R/C lines have exactly two fields.
    """
    lines = []
    for c in compset.components:
        if c.kind == "L":
            lines.append(f"L {c.value:.17g} {c.dcr:.17g}")
        else:
            lines.append(f"{c.kind} {c.value:.17g}")
    return "\n".join(lines) + "\n"


def parse_components(text: str) -> ComponentSet:
    """Parse the component queue into a ComponentSet (multiset semantics:
    duplicates allowed, line order irrelevant -- the constructor sorts)."""
    rows = _content_lines(text)
    if not rows:
        raise ValueError("need at least one component line")
    comps: list[Component] = []
    for k, line in enumerate(rows, start=1):
        parts = line.split()
        kind = parts[0]
        if kind not in ("R", "L", "C"):
            raise ValueError(f"line {k}: type must be R|L|C, got {kind!r}")
        if kind in ("R", "C") and len(parts) != 2:
            raise ValueError(f"line {k}: {kind} line needs exactly "
                             f"'type parameter', got {line!r}")
        if kind == "L" and len(parts) not in (2, 3):
            raise ValueError(f"line {k}: L line needs 'type parameter [dcr]', "
                             f"got {line!r}")
        try:
            value = float(parts[1])
            dcr = float(parts[2]) if kind == "L" and len(parts) == 3 else 0.0
        except (IndexError, ValueError):
            raise ValueError(f"line {k}: non-numeric field in {line!r}") from None
        try:
            comps.append(Component(kind, value, dcr))
        except ValueError as exc:
            raise ValueError(f"line {k}: {exc}") from None
    return ComponentSet(tuple(comps))


def load_components(path) -> ComponentSet:
    """Read the component queue file from disk."""
    with open(path, "r", encoding="utf-8") as fh:
        return parse_components(fh.read())
