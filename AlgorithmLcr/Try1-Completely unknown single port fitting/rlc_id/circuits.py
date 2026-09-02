"""Series-parallel RLC one-port topology trees (DESIGN.md section 4.1).

A topology is an immutable tree: internal nodes are SER/PAR combinations and
leaves are single elements of kind 'R', 'L' or 'C'.  Element *values* are not
stored in the tree; a topology plus a parameter vector ``theta`` (log10 of the
element values, one entry per leaf in canonical traversal order) fully defines
an impedance function ``Z(jw)``.

Canonical rules (section 4.1):
  R1  parent/child node kinds must alternate (enforced at construction),
  R2  at most one leaf of each kind per node (leaves only),
  R3  children are stored sorted by canonical string.

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


@dataclass(frozen=True)
class Leaf:
    """Single element; kind in {'R', 'L', 'C'}."""

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
    """Map any tree to its canonical representative (R1, R2, R3).

    R1 flattens same-kind nesting (series of series = series), R2 merges
    duplicate leaf kinds within a node (topology level: values play no role),
    R3 sorts children by canonical string.  Every step preserves Z(s).
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
    leaves: dict[str, Leaf] = {}
    subs: list[Tree] = []
    for c in flat:
        if isinstance(c, Leaf):
            leaves.setdefault(c.kind, c)  # R2: keep one leaf per kind
        else:
            subs.append(c)
    children = list(leaves.values()) + subs
    if len(children) == 1:  # node collapsed onto its single child
        return children[0]
    return make_node(tree.kind, children)


# ---------------------------------------------------------------------------
# structure queries
# ---------------------------------------------------------------------------

def n_leaves(tree: Tree) -> int:
    """Number of elements (= number of free parameters)."""
    if isinstance(tree, Leaf):
        return 1
    return sum(n_leaves(c) for c in tree.children)


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


def bounds(tree: Tree) -> tuple[np.ndarray, np.ndarray]:
    """Per-parameter log10 bounds from KIND_BOUNDS."""
    lb = np.array([KIND_BOUNDS[k][0] for k in leaf_kinds(tree)])
    ub = np.array([KIND_BOUNDS[k][1] for k in leaf_kinds(tree)])
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
    """Vectorized Z(s) evaluation; ``theta`` holds log10 element values.

    ``s`` is the (complex) Laplace array, usually ``1j * 2 * pi * f``.  The
    evaluation is analytic in ``theta`` so a complex-step perturbation of any
    parameter yields an exact derivative through the imaginary part.
    """
    theta = np.asarray(theta)
    values = np.power(10.0, theta)
    idx = [0]  # mutable cursor over the parameter vector

    def rec(t: Tree) -> np.ndarray:
        if isinstance(t, Leaf):
            v = values[idx[0]]
            idx[0] += 1
            if t.kind == "R":
                return v + 0.0 * s
            if t.kind == "L":
                return s * v
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
# DESIGN.md section 5.2 originally proposed the complex-step method.  That
# method requires a REAL-valued base function (Im F(theta) = 0), which fails
# here because Z is complex at real theta: Im[Z(theta + i h)]/h then divides
# the O(1) imaginary part of Z itself by h, returning garbage ~1/h.  Forward
# AD through the same tree recursion is equally exact (machine precision) and
# costs one evaluation:
#
#   leaf R:  Z = v,          dZ/dv = 1
#   leaf L:  Z = s v,        dZ/dv = s
#   leaf C:  Z = 1/(s v),    dZ/dv = -1/(s v^2)
#   SER:     dZ = sum dZc
#   PAR:     Z = 1/Y, Y = sum 1/Zc  =>  dZ = Z^2 * sum(dZc / Zc^2)
#
# with dv/dtheta_i = ln(10) * v for the log10 parameterization.

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
            i = idx[0]
            idx[0] += 1
            v = values[i]
            if t.kind == "R":
                z, dz = v + 0.0 * s, 1.0
            elif t.kind == "L":
                z, dz = s * v, s
            else:
                z, dz = 1.0 / (s * v), -1.0 / (s * v * v)
            J = np.zeros((p,) + s.shape, dtype=complex)
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

    ``children`` is a list of ``(tree, value_list)`` where ``value_list`` holds
    the subtree's element values in its own canonical leaf order.  Returns the
    canonical node and the value list reordered to the node's canonical leaf
    order, so structure and parameters can never drift apart.
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
    """Human-readable form: ``R + (R||C)``; with values ``R(100) + ...``."""
    vals = None if theta is None else np.power(10.0, np.asarray(theta, dtype=float))
    idx = [0]

    def fmt(t: Tree) -> str:
        if isinstance(t, Leaf):
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
