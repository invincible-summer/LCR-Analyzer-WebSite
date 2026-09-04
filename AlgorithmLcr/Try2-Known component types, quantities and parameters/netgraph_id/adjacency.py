"""Unified adjacency-matrix output (../../OUTPUT_FORMAT.md).

Expands a candidate ``Network`` (structure + component assignment) into the
canonical upper-triangle adjacency matrix: ``rows[i][j]`` (i < j) is the
vector of all edges directly connecting nodes i and j.  Nodes 0 and 1 are
the one-port terminals.

Edge follows the root spec: (type, parameter, dcr).  Try2 components carry
their known values; inductors contribute their series DCR, R/C edges carry
dcr = 0.  The expansion is lossless: ``Structure.mult`` is already the
flattened upper triangle (slot order = row-major), and edge instances are
contiguous per slot (``Structure.slot_of_instances``).
"""

from __future__ import annotations

from dataclasses import dataclass

from .components import ComponentSet
from .graph import Network, slot_list
from .selector import Candidate


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


def network_to_adjacency(network: Network, compset: ComponentSet) -> Adjacency:
    """Expand a candidate network into the unified adjacency matrix.

    Slot order (row-major upper triangle) and in-slot edge order (instance
    order) are both deterministic; the mapping is one-to-one.
    """
    V = network.structure.V
    slots = slot_list(V)
    soi = network.structure.slot_of_instances()
    comps = compset.components
    adj = Adjacency(V)
    for t, comp_idx in enumerate(network.assign):
        i, j = slots[soi[t]]
        comp = comps[comp_idx]
        adj.add(i, j, Edge(comp.kind, comp.value, comp.dcr))
    return adj


def candidate_to_adjacency(cand: Candidate, compset: ComponentSet) -> Adjacency:
    """Adjacency matrix of a ranked candidate (spec sec.4: per representative)."""
    return network_to_adjacency(cand.network, compset)
