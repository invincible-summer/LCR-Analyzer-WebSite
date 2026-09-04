"""Unified inputs (../../INPUT_FORMAT.md): measurements + known topology.

Measurements: first line n, then n lines ``f Rz Iz`` (sec.1).
Topology: the parameter-free form of the unified adjacency matrix (sec.2.3)
-- node count V, then the upper-triangle rows of edge counts (row i holds
V-1-i integers for slots (i,i+1)..(i,V-1)), then the edge-type queue, one
R|L|C per line in slot-major order.  Reconstruction walks the slots in row
order and consumes queue entries to emit edges (i, j, kind) with i < j.

``#`` starts a comment (whole line or trailing), blank lines are ignored.
Dump uses %.17g so load(dump(x)) == x bit-for-bit.
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
# topology: adjacency count matrix + edge-type queue (sec.2.3)
# ---------------------------------------------------------------------------

def _slot_pairs(V: int) -> list[tuple[int, int]]:
    """Upper-triangle slots in row-major order: (0,1),(0,2),...,(V-2,V-1)."""
    return [(i, j) for i in range(V) for j in range(i + 1, V)]


def format_topology(edges: list) -> str:
    """Serialize an edge list [(u, v, kind), ...] in the unified form.

    V = max node label + 1.  Edges are stably ordered slot-major (same-slot
    edges keep their input order), then dumped as count rows + type queue.
    """
    if not edges:
        raise ValueError("need at least one edge")
    V = max(max(u, v) for u, v, _k in edges) + 1
    ordered = sorted(edges, key=lambda e: (min(e[0], e[1]), max(e[0], e[1])))
    counts: dict[tuple[int, int], int] = {}
    for u, v, _k in ordered:
        counts[(min(u, v), max(u, v))] = counts.get((min(u, v), max(u, v)), 0) + 1
    lines = [str(V)]
    for i in range(V - 1):
        lines.append(" ".join(str(counts.get((i, j), 0))
                              for j in range(i + 1, V)))
    for _u, _v, k in ordered:
        lines.append(k)
    return "\n".join(lines) + "\n"


def parse_topology(text: str) -> list:
    """Parse the topology file -> edges [(u, v, kind), ...] with u < v.

    Validation per INPUT_FORMAT.md sec.3; connectivity / dead parts are the
    engine's business (reduce_graph), not the loader's.
    """
    rows = _content_lines(text)
    if not rows:
        raise ValueError("empty topology input")
    try:
        V = int(rows[0])
    except ValueError:
        raise ValueError(f"first line must be integer V, got {rows[0]!r}") from None
    if V < 2:
        raise ValueError(f"V must be >= 2, got {V}")
    # matrix rows: rows[1] .. rows[V-1]
    if len(rows) < V:
        raise ValueError(f"expected V-1={V - 1} matrix rows, "
                         f"got {len(rows) - 1} lines after V")
    counts: list[int] = []
    for i in range(V - 1):
        line = rows[1 + i]
        parts = line.split()
        if len(parts) != V - 1 - i:
            raise ValueError(f"matrix row {i} needs {V - 1 - i} integers, "
                             f"got {line!r}")
        for p in parts:
            try:
                m = int(p)
            except ValueError:
                raise ValueError(f"matrix row {i}: non-integer count {p!r}") from None
            if m < 0:
                raise ValueError(f"matrix row {i}: negative count {m}")
            counts.append(m)
    E = sum(counts)
    queue = rows[1 + (V - 1):]
    if len(queue) != E:
        raise ValueError(f"edge queue needs exactly E={E} lines, got {len(queue)}")
    edges: list[tuple[int, int, str]] = []
    slots = _slot_pairs(V)
    t = 0
    for (i, j), m in zip(slots, counts):
        for _ in range(m):
            kind = queue[t]
            t += 1
            if kind not in ("R", "L", "C"):
                raise ValueError(f"edge queue entry must be R|L|C, got {kind!r}")
            edges.append((i, j, kind))
    return edges


def load_topology(path) -> list:
    """Read the topology file from disk."""
    with open(path, "r", encoding="utf-8") as fh:
        return parse_topology(fh.read())
