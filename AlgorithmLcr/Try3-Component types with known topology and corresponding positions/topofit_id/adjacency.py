"""Unified adjacency-matrix output (../../OUTPUT_FORMAT.md).

Places the fitted parameter groups of a known-topology identification into
the canonical upper-triangle adjacency matrix: ``rows[i][j]`` (i < j) is the
vector of all edges directly connecting nodes i and j.  Nodes 0 and 1 are
the one-port terminals; node labels keep their original (input-graph)
numbering, so nodes removed by reduction survive as empty slots.

Edge follows the root spec: (type, parameter, dcr).  Fitting happens on
reduced groups (F1-F4), so the matrix carries **aggregate** values: a
parallel-merged group is one edge on its slot, a series-merged group is one
edge across its outer nodes (intermediate nodes stay empty), an F4-absorbed
resistor lives on in the inductor group's dcr, and F1-dropped edges are
omitted and reported through ``adjacency_notes`` instead.
"""

from __future__ import annotations

from dataclasses import dataclass

from .fit import FitResult


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


def _node_span(res: FitResult) -> int:
    """V = max label + 1 over fitted groups and the original input edges."""
    labels = {n for g in res.groups for n in (g.u, g.v)}
    labels.update(n for (u, v, _k) in res.edges for n in (u, v))
    return max(labels) + 1


def fitresult_to_adjacency(res: FitResult) -> Adjacency:
    """Adjacency matrix of a fitted topology: one aggregate Edge per group,
    placed on the group's original node labels."""
    adj = Adjacency(_node_span(res))
    for g in res.groups:
        dcr = g.value[2] if g.kind == "L" else 0.0
        adj.add(g.u, g.v, Edge(g.kind, g.value[1], dcr))
    return adj


def adjacency_notes(res: FitResult) -> list[str]:
    """Annotation lines for merged/dropped original edges (spec sec.5.3)."""
    notes = []
    for er in res.edges_out:
        if er.status == "merged":
            notes.append(f"e{er.index} ({er.kind}) merged: {er.note}")
        elif er.status == "dropped":
            notes.append(f"e{er.index} ({er.kind}) dropped: {er.note}")
    return notes
