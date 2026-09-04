"""Unified adjacency-matrix output (../../OUTPUT_FORMAT.md).

Converts a fitted series-parallel tree + theta into the canonical
upper-triangle adjacency matrix: ``rows[i][j]`` (i < j) is the vector of all
edges directly connecting nodes i and j.  Nodes 0 and 1 are the one-port
terminals; internal chain nodes are numbered from 2 in emitter order.

Edge follows the root spec: (type, parameter, dcr).  Try1 inductors are
ideal (no series resistance), so dcr is always 0.0 here.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .circuits import SER, Leaf, Tree, n_leaves
from .fit_engine_a import Candidate


@dataclass(frozen=True)
class Edge:
    """One 2-terminal element; see OUTPUT_FORMAT.md section 1."""

    type: str          # "R" | "L" | "C"
    parameter: float   # R[ohm] / L[H] / C[F]
    dcr: float = 0.0   # series DC resistance of L; 0 unless type == "L"


def _fmt_edge(e: Edge) -> str:
    if e.type == "L" and e.dcr != 0.0:
        return f"L {e.parameter:.3e} dcr {e.dcr:.3e}"
    return f"{e.type} {e.parameter:.3e}"


class Adjacency:
    """Strict upper-triangle adjacency matrix of vector<Edge> (spec sec.2)."""

    def __init__(self, V: int):
        if V < 2:
            raise ValueError("a one-port needs at least the 2 terminal nodes")
        self.V = V
        self.rows = [[[] for _ in range(V - 1 - i)] for i in range(V)]

    def slot(self, i: int, j: int) -> list[Edge]:
        """All edges directly connecting i and j; requires i < j."""
        if not (0 <= i < j < self.V):
            raise ValueError(f"slot ({i},{j}) outside upper triangle of V={self.V}")
        return self.rows[i][j - i - 1]

    def add(self, i: int, j: int, edge: Edge) -> None:
        """Append an undirected edge; {i, j} order is normalized."""
        if i == j:
            raise ValueError("self loops are not part of the format")
        if i > j:
            i, j = j, i
        self.slot(i, j).append(edge)

    @property
    def n_edges(self) -> int:
        return sum(len(cell) for row in self.rows for cell in row)

    def occupied(self) -> list[tuple[int, int, list[Edge]]]:
        """Non-empty slots in row-major upper-triangle order (spec sec.4)."""
        out = []
        for i in range(self.V):
            for k, cell in enumerate(self.rows[i]):
                if cell:
                    out.append((i, i + k + 1, cell))
        return out

    def format_block(self, label: int | str | None = None,
                     extra_lines: list[str] | None = None) -> str:
        """Unified print form (spec sec.4); label = candidate rank when given."""
        head = f"adjacency[{label}] " if label is not None else "adjacency "
        lines = [f"{head}V={self.V} (ports 0,1):"]
        for i, j, edges in self.occupied():
            lines.append(f"  ({i},{j}): " + " | ".join(_fmt_edge(e) for e in edges))
        if extra_lines:
            lines.extend("  " + s for s in extra_lines)
        return "\n".join(lines)


# ---------------------------------------------------------------------------
# series-parallel tree -> two-terminal graph (spec sec.5.1)
# ---------------------------------------------------------------------------

def _n_chain_nodes(tree: Tree) -> int:
    """Internal nodes the emitter allocates: k-1 per SER node."""
    if isinstance(tree, Leaf):
        return 0
    own = len(tree.children) - 1 if tree.kind == SER else 0
    return own + sum(_n_chain_nodes(c) for c in tree.children)


def tree_to_adjacency(tree: Tree, theta) -> Adjacency:
    """Realize a canonical SP tree between terminals 0 and 1.

    Deterministic numbering (C++ porting locks this rule): children are
    visited in stored canonical order; each SER node chains its children from
    the port-0 side, allocating k-1 fresh internal nodes (counter from 2);
    PAR children share the same terminal pair, forming multi-edges.  Values
    are consumed in ``leaves(tree)`` order, i.e. the theta convention.
    """
    values = np.power(10.0, np.asarray(theta, dtype=float))
    if len(values) != n_leaves(tree):
        raise ValueError(f"theta has {len(values)} entries, "
                         f"tree has {n_leaves(tree)} leaves")
    adj = Adjacency(2 + _n_chain_nodes(tree))
    idx = [0]
    counter = [2]

    def emit(t: Tree, a: int, b: int) -> None:
        if isinstance(t, Leaf):
            adj.add(a, b, Edge(t.kind, float(values[idx[0]])))
            idx[0] += 1
            return
        if t.kind == SER:
            chain = [a]
            for _ in range(len(t.children) - 1):
                chain.append(counter[0])
                counter[0] += 1
            chain.append(b)
            for child, u, v in zip(t.children, chain, chain[1:]):
                emit(child, u, v)
        else:  # PAR: every child spans the same terminal pair
            for child in t.children:
                emit(child, a, b)

    emit(tree, 0, 1)
    return adj


def candidate_to_adjacency(cand: Candidate) -> Adjacency:
    """Adjacency matrix of a fitted candidate (tree + theta)."""
    return tree_to_adjacency(cand.tree, cand.theta)
