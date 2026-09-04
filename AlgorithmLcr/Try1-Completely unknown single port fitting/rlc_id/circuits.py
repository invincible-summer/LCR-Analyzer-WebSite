"""Series-parallel RLC one-port topology trees (DESIGN.md section 4.1).

A topology is an immutable tree: internal nodes are SER/PAR combinations and
leaves are single devices of kind 'R', 'L' or 'C'.  An inductor is a REAL
inductor: ideal L > 0 in series with a DC resistance Rd >= Rd_min (two
parameters, ONE device -- laboratory inductors always carry a non-negligible
winding resistance, mirroring the Try2/Try3 component model).

Element *values* are not stored in the tree; a topology plus a parameter
vector ``theta`` (log10 of the physical values, one entry per parameter in
canonical traversal order: R -> 1 entry, C -> 1 entry, L -> 2 entries
[log10 L, log10 Rd]) fully defines an impedance function ``Z(jw)``.

Canonical rules (section 4.1):
  R1  parent/child node kinds must alternate (enforced at construction),
  R2  at most one leaf of each kind per node -- EXCEPT that a PAR node may
      hold several L leaves: two (L + Rd) devices in parallel form a
      second-order tank and are NOT mergeable into one inductor, while
      series L leaves do merge (both L and Rd add in series),
  R3  children are stored sorted by canonical string,
  R4  a SER node never holds both an R leaf and an L leaf: the series R
      folds into the inductor's DC resistance (R + (L, Rd) == (L, Rd + R)),
      so R + L in series is ONE device, not two.

The canonical string (``canonical``) uniquely identifies an electrical
equivalence class representative; it is used for deduplication and assertions.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

SER = "SER"
PAR = "PAR"

# Default element-value search domain (DESIGN.md section 4.3), as log10 bounds.
KIND_BOUNDS = {
    "R": (-3.0, 7.0),    # 1 mOhm .. 10 MOhm
    "L": (-10.0, 1.0),   # 100 pH  .. 10 H
    "C": (-13.0, -3.0),  # 0.1 pF  .. 1 mF
}
# Series DC resistance of an inductor (the second parameter of an L device).
# The lower bound stands in for "ideal" (0 ohm is not representable in the
# log10 parameterization); it matches the Try3 physical bound on Rd.
DCR_BOUNDS = (-6.0, 7.0)  # 1 uOhm .. 10 MOhm

# Parameters carried by each leaf kind, in theta order.
PARAM_KINDS = {
    "R": ("R",),
    "L": ("L", "Rd"),
    "C": ("C",),
}

# log10 bound lookup per PARAMETER kind (not per leaf kind).
_PARAM_BOUNDS = {
    "R": KIND_BOUNDS["R"],
    "L": KIND_BOUNDS["L"],
    "Rd": DCR_BOUNDS,
    "C": KIND_BOUNDS["C"],
}

DCR_MIN = 10.0 ** DCR_BOUNDS[0]  # linear value used for "ideal" inductors


@dataclass(frozen=True)
class Leaf:
    """Single device; kind in {'R', 'L', 'C'}; L carries (L, Rd) parameters."""

    kind: str

    def __post_init__(self):
        if self.kind not in KIND_BOUNDS:
            raise ValueError(f"bad leaf kind {self.kind!r}")


@dataclass(frozen=True)
class Node:
    """Internal node; kind in {'SER', 'PAR'}, children a tuple of trees.

    Children are always stored sorted by canonical string (R3).  Build nodes
    through :func:`make_node` (or :func:`normalize`) to guarantee this.
    """

    kind: str
    children: tuple

    def __post_init__(self):
        if self.kind not in (SER, PAR):
            raise ValueError(f"bad node kind {self.kind!r}")
        if len(self.children) < 2:
            raise ValueError("internal node needs >= 2 children")


Tree = Leaf | Node


def make_node(kind: str, children) -> Node:
    """Build a canonical node: children sorted by canonical string (R3)."""
    return Node(kind, tuple(sorted(children, key=canonical)))


def opposite(kind: str) -> str:
    return PAR if kind == SER else SER


# ---------------------------------------------------------------------------
# canonical form / normalization
# ---------------------------------------------------------------------------

def canonical(tree: Tree) -> str:
    """Canonical serialization string (unique per equivalence class)."""
    if isinstance(tree, Leaf):
        return tree.kind
    body = ",".join(sorted(canonical(c) for c in tree.children))
    return ("S(" if tree.kind == SER else "P(") + body + ")"


def normalize(tree: Tree) -> Tree:
    """Map any tree to its canonical representative (R1, R2', R3, R4).

    R1 flattens same-kind nesting (series of series = series), R2' merges
    duplicate leaf kinds within a node (keeping every parallel L leaf), R3
    sorts children by canonical string, and R4 absorbs a series R leaf into
    a sibling L leaf's DC resistance.  Every step preserves Z(s) at the
    topology level; element values play no role here (same semantic scope
    as the original R2 merge).
    """
    if isinstance(tree, Leaf):
        return tree
    flat: list[Tree] = []
    for child in tree.children:
        c = normalize(child)
        if isinstance(c, Node) and c.kind == tree.kind:  # R1: flatten
            flat.extend(c.children)
        else:
            flat.append(c)
    kept: list[Tree] = []
    seen: set[str] = set()
    for c in flat:
        if not isinstance(c, Leaf):
            kept.append(c)
            continue
        # R2': one leaf per kind, except parallel L leaves (second-order
        # tanks) which are kept as distinct devices
        if c.kind == "L" and tree.kind == PAR:
            kept.append(c)
        elif c.kind not in seen:
            seen.add(c.kind)
            kept.append(c)
    # R4: series R folds into a sibling L leaf's DC resistance
    if tree.kind == SER and "R" in seen and "L" in seen:
        kept = [c for c in kept if not (isinstance(c, Leaf) and c.kind == "R")]
    if len(kept) == 1:  # node collapsed onto its single child
        return kept[0]
    return make_node(tree.kind, kept)


# ---------------------------------------------------------------------------
# structure queries
# ---------------------------------------------------------------------------

def n_leaves(tree: Tree) -> int:
    """Number of devices (an L + DCR pair counts as ONE device)."""
    if isinstance(tree, Leaf):
        return 1
    return sum(n_leaves(c) for c in tree.children)


def n_params(tree: Tree) -> int:
    """Number of free parameters (L devices carry two: L and Rd)."""
    if isinstance(tree, Leaf):
        return len(PARAM_KINDS[tree.kind])
    return sum(n_params(c) for c in tree.children)


def leaves(tree: Tree) -> list[Leaf]:
    """Leaves in parameter order (canonical traversal = stored child order)."""
    if isinstance(tree, Leaf):
        return [tree]
    out: list[Leaf] = []
    for c in tree.children:
        out.extend(leaves(c))
    return out


def leaf_kinds(tree: Tree) -> list[str]:
    return [lf.kind for lf in leaves(tree)]


def param_kinds(tree: Tree) -> list[str]:
    """Parameter kinds in theta order, e.g. ['R', 'L', 'Rd', 'C']."""
    out: list[str] = []
    for lf in leaves(tree):
        out.extend(PARAM_KINDS[lf.kind])
    return out


def bounds(tree: Tree) -> tuple[np.ndarray, np.ndarray]:
    """Per-parameter log10 bounds (two rows per L device)."""
    pks = param_kinds(tree)
    lb = np.array([_PARAM_BOUNDS[k][0] for k in pks])
    ub = np.array([_PARAM_BOUNDS[k][1] for k in pks])
    return lb, ub


def max_internal_depth(tree: Tree) -> int:
    """Maximum nesting level of internal nodes (a bare leaf has depth 0)."""
    if isinstance(tree, Leaf):
        return 0
    return 1 + max(max_internal_depth(c) for c in tree.children)


# ---------------------------------------------------------------------------
# impedance evaluation
# ---------------------------------------------------------------------------

def evaluate(tree: Tree, theta, s: np.ndarray) -> np.ndarray:
    """Vectorized Z(s) evaluation; ``theta`` holds log10 parameter values.

    ``s`` is the (complex) Laplace array, usually ``1j * 2 * pi * f``.
    Device impedances: R -> v, L -> Rd + s*v, C -> 1/(s*v); an L device
    consumes two theta entries [log10 L, log10 Rd].
    """
    theta = np.asarray(theta)
    values = np.power(10.0, theta)
    idx = [0]  # mutable cursor over the parameter vector

    def rec(t: Tree) -> np.ndarray:
        if isinstance(t, Leaf):
            if t.kind == "R":
                v = values[idx[0]]
                idx[0] += 1
                return v + 0.0 * s
            if t.kind == "L":
                l = values[idx[0]]
                rd = values[idx[0] + 1]
                idx[0] += 2
                return rd + s * l
            v = values[idx[0]]
            idx[0] += 1
            return 1.0 / (s * v)
        zs = [rec(c) for c in t.children]
        if t.kind == SER:
            out = zs[0]
            for z in zs[1:]:
                out = out + z
            return out
        adm = 1.0 / zs[0]
        for z in zs[1:]:
            adm = adm + 1.0 / z
        return 1.0 / adm

    return rec(tree)


def evaluate_f(tree: Tree, theta, f: np.ndarray) -> np.ndarray:
    """Convenience wrapper: evaluate at frequencies f [Hz]."""
    return evaluate(tree, theta, 1j * 2.0 * np.pi * np.asarray(f, dtype=float))


# ---------------------------------------------------------------------------
# exact Jacobian by forward-mode automatic differentiation
# ---------------------------------------------------------------------------
#
# Forward AD through the tree recursion is exact to machine precision and
# costs one evaluation (see DESIGN.md section 5.2 for why the complex-step
# method is invalid on a complex-valued Z):
#
#   leaf R:   Z = v,          dZ/dv = 1
#   leaf L:   Z = rd + s v,   dZ/dv = s,     dZ/drd = 1
#   leaf C:   Z = 1/(s v),    dZ/dv = -1/(s v^2)
#   SER:      dZ = sum dZc
#   PAR:      Z = 1/Y, Y = sum 1/Zc  =>  dZ = Z^2 * sum(dZc / Zc^2)
#
# with dvalue/dtheta_i = ln(10) * value for the log10 parameterization.

_LN10 = float(np.log(10.0))


def evaluate_jac(tree: Tree, theta, s: np.ndarray
                 ) -> tuple[np.ndarray, np.ndarray]:
    """Return (Z, dZ/dtheta) with dZ shape (n_params, len(s)); exact."""
    theta = np.asarray(theta, dtype=float)
    values = np.power(10.0, theta)
    p = len(theta)
    idx = [0]

    def rec(t: Tree) -> tuple[np.ndarray, np.ndarray]:
        if isinstance(t, Leaf):
            J = np.zeros((p,) + s.shape, dtype=complex)
            if t.kind == "R":
                i = idx[0]
                idx[0] += 1
                v = values[i]
                z, dz = v + 0.0 * s, 1.0
                J[i] = dz * (_LN10 * v)
            elif t.kind == "L":
                il = idx[0]
                ird = idx[0] + 1
                idx[0] += 2
                l, rd = values[il], values[ird]
                z = rd + s * l
                J[il] = s * (_LN10 * l)
                J[ird] = 1.0 * (_LN10 * rd)
            else:
                i = idx[0]
                idx[0] += 1
                v = values[i]
                z, dz = 1.0 / (s * v), -1.0 / (s * v * v)
                J[i] = dz * (_LN10 * v)
            return z, J
        parts = [rec(c) for c in t.children]
        if t.kind == SER:
            z = parts[0][0]
            J = parts[0][1].copy()
            for zc, Jc in parts[1:]:
                z = z + zc
                J += Jc
            return z, J
        ys = [1.0 / zc for zc, _ in parts]
        Y = ys[0]
        for yc in ys[1:]:
            Y = Y + yc
        z = 1.0 / Y
        acc = None
        for (zc, Jc), yc in zip(parts, ys):
            term = Jc * (yc * yc)[None, ...]
            acc = term if acc is None else acc + term
        return z, (z * z)[None, ...] * acc

    return rec(tree)


# ---------------------------------------------------------------------------
# value-carrying construction helper
# ---------------------------------------------------------------------------

def assemble(kind: str, children) -> tuple[Tree, list[float]]:
    """Build a canonical node from ``(subtree, values)`` pairs.

    ``children`` is a list of ``(tree, value_list)`` where ``value_list``
    holds the subtree's parameter values in its own canonical order (two
    entries per L device).  Returns the canonical node and the value list
    reordered to the node's canonical leaf order, so structure and
    parameters can never drift apart.
    """
    pairs = sorted(children, key=lambda p: canonical(p[0]))
    node = make_node(kind, [p[0] for p in pairs])
    values: list[float] = []
    for _, vals in pairs:
        values.extend(vals)
    return node, values


# ---------------------------------------------------------------------------
# human-readable printing
# ---------------------------------------------------------------------------

_PREFIX = [(9, "G"), (6, "M"), (3, "k"), (0, ""), (-3, "m"), (-6, "u"),
           (-9, "n"), (-12, "p"), (-15, "f")]
_UNIT = {"R": "Ohm", "L": "H", "C": "F"}


def fmt_eng(x: float) -> str:
    """Engineering (SI prefix) formatting, e.g. 1e-8 -> '10n'."""
    if x == 0 or not np.isfinite(x):
        return str(x)
    ax = abs(x)
    for exp, pre in _PREFIX:
        if ax >= 10.0 ** exp:
            return f"{x / 10.0 ** exp:.4g}{pre}"
    return f"{x:.4g}"


def to_string(tree: Tree, theta=None) -> str:
    """Human-readable form: ``R + (L||C)``; with values ``R(100) + L(1m, Rd 5)``."""
    vals = None if theta is None else np.power(10.0, np.asarray(theta, dtype=float))
    idx = [0]

    def fmt(t: Tree) -> str:
        if isinstance(t, Leaf):
            if t.kind == "L":
                if vals is None:
                    return "L"
                l = vals[idx[0]]
                rd = vals[idx[0] + 1]
                idx[0] += 2
                return f"L({fmt_eng(l)}, Rd {fmt_eng(rd)})"
            v = vals[idx[0]] if vals is not None else None
            idx[0] += 1
            if v is None:
                return t.kind
            return f"{t.kind}({fmt_eng(v)})"
        sep = " + " if t.kind == SER else " || "
        parts = []
        for c in t.children:
            s = fmt(c)
            parts.append(f"({s})" if isinstance(c, Node) else s)
        return sep.join(parts)

    return fmt(tree)
