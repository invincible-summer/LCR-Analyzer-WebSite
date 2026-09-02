"""Multigraph model of a 2-terminal RLC network (DESIGN.md section 3).

A network is a connected undirected multigraph:
  * nodes 0 and 1 are the two port terminals, nodes 2..V-1 are internal
    junctions;
  * edges are components -- parallel edges (same node pair) are legal and
    are exactly "components in parallel"; self loops never occur (a
    component whose leads are joined to the same node carries no external
    current);
  * every edge is an independent object (never keyed by its node pair).

Two representations (DESIGN.md section 3.2):
  * structure layer: symmetric multiplicity matrix M[i][j] = m_ij (zero
    diagonal, sum = E) stored as a flat tuple over the canonical slot list;
    this carries enumeration / canonicalization / dedup;
  * assignment layer: an edge list assigning concrete components to the
    edge instances of a structure.

Reduction rule R0 (dead-part exclusion, DESIGN.md section 4.3): any
connected component of G - c (c any node) that contains no terminal is
electrically dead -- summing KCL over its nodes shows the net current
flowing into it through c is identically zero -- so structures containing
such parts are excluded from the enumeration.
"""

from __future__ import annotations

from dataclasses import dataclass
from itertools import permutations
from typing import Iterator

# ---------------------------------------------------------------------------
# slots and multiplicity vectors
# ---------------------------------------------------------------------------

_SLOT_CACHE: dict[int, list[tuple[int, int]]] = {}
_SLOT_INDEX_CACHE: dict[int, dict[tuple[int, int], int]] = {}


def slot_list(V: int) -> list[tuple[int, int]]:
    """Canonical slot list [(0,1),(0,2),...,(1,2),...,(V-2,V-1)]."""
    if V not in _SLOT_CACHE:
        _SLOT_CACHE[V] = [(i, j) for i in range(V) for j in range(i + 1, V)]
    return _SLOT_CACHE[V]


def slot_index(V: int) -> dict[tuple[int, int], int]:
    if V not in _SLOT_INDEX_CACHE:
        _SLOT_INDEX_CACHE[V] = {ij: k for k, ij in enumerate(slot_list(V))}
    return _SLOT_INDEX_CACHE[V]


def n_slots(V: int) -> int:
    return V * (V - 1) // 2


def empty_mult(V: int) -> list[int]:
    return [0] * n_slots(V)


def mult_degree(V: int, mult) -> list[int]:
    """Multigraph degree (parallel edges counted) of every node."""
    deg = [0] * V
    for (i, j), m in zip(slot_list(V), mult):
        deg[i] += m
        deg[j] += m
    return deg


# ---------------------------------------------------------------------------
# connectivity and the R0 dead-part rule
# ---------------------------------------------------------------------------

def is_connected(V: int, mult) -> bool:
    """True iff all V nodes lie in one connected component (union-find)."""
    parent = list(range(V))

    def find(a: int) -> int:
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for (i, j), m in zip(slot_list(V), mult):
        if m > 0:
            ri, rj = find(i), find(j)
            if ri != rj:
                parent[ri] = rj
    root = find(0)
    return all(find(v) == root for v in range(V))


def has_dead_part(V: int, mult) -> bool:
    """R0 test: does some connected component of G - c contain no terminal?

    Such a component hangs off the rest of the network through the single
    node c; summing KCL over its nodes proves the net current entering it
    is identically zero, so it is electrically invisible (DESIGN.md 4.3).
    A single pass suffices: any dead part P attached only via c must be a
    *full* component of G - c, because a proper sub-part of a component of
    G - c would have an additional attachment inside that component.
    """
    if V <= 2:
        return False
    adj: list[list[int]] = [[] for _ in range(V)]
    for (i, j), m in zip(slot_list(V), mult):
        if m > 0:
            adj[i].append(j)
            adj[j].append(i)
    for c in range(V):
        seen = [False] * V
        for start in range(V):
            if start == c or seen[start]:
                continue
            # BFS the component of start in G - c
            stack = [start]
            seen[start] = True
            has_terminal = False
            while stack:
                x = stack.pop()
                if x in (0, 1):
                    has_terminal = True
                for y in adj[x]:
                    if y != c and not seen[y]:
                        seen[y] = True
                        stack.append(y)
            if not has_terminal:
                return True
    return False


def structure_ok(V: int, mult, *, allow_dead: bool = False) -> bool:
    """Admissibility of a uniform structure (connectivity + R0)."""
    if not is_connected(V, mult):
        return False
    if not allow_dead and has_dead_part(V, mult):
        return False
    return True


# ---------------------------------------------------------------------------
# node permutations, canonical form, automorphisms
# ---------------------------------------------------------------------------

def perm_group(V: int) -> Iterator[tuple[int, ...]]:
    """The relabeling group G = {terminal swap} x Sym(internal nodes).

    Node 0/1 are distinguished as the port pair but exchanging them leaves
    the driving-point impedance unchanged, so both labellings are identified;
    internal nodes 2..V-1 are freely permutable.
    """
    internal = range(2, V)
    for swap in (False, True):
        for pi in permutations(internal):
            p = [0] * V
            p[0], p[1] = (1, 0) if swap else (0, 1)
            for k, v in enumerate(pi):
                p[v] = k + 2
            yield tuple(p)


def permute_mult(V: int, mult, p) -> tuple[int, ...]:
    """Relabel nodes by p (image of node i is p[i]) and rebuild the slot vector."""
    si = slot_index(V)
    out = [0] * n_slots(V)
    for (i, j), m in zip(slot_list(V), mult):
        a, b = p[i], p[j]
        if a > b:
            a, b = b, a
        out[si[(a, b)]] += m
    return tuple(out)


def canonical_mult(V: int, mult) -> tuple[int, ...]:
    """Canonical representative = lexicographic minimum over G."""
    return min(permute_mult(V, mult, p) for p in perm_group(V))


def structure_automorphisms(V: int, mult) -> tuple[tuple[int, ...], ...]:
    """All elements of G that fix the (canonical) mult vector."""
    return tuple(p for p in perm_group(V) if permute_mult(V, mult, p) == tuple(mult))


# ---------------------------------------------------------------------------
# structure container
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Structure:
    """One uniform-edge 2-terminal multigraph structure (canonical form).

    mult[k] = number of parallel edges in slot_list(V)[k]; aut = node
    permutations (from G) fixing the structure, used to dedup component
    assignments.
    """

    V: int
    mult: tuple[int, ...]
    aut: tuple[tuple[int, ...], ...]

    @property
    def n_edges(self) -> int:
        return sum(self.mult)

    @property
    def n_internal(self) -> int:
        return self.V - 2

    @property
    def key(self) -> tuple:
        return (self.V, self.mult)

    def degrees(self) -> list[int]:
        return mult_degree(self.V, self.mult)

    def slot_of_instances(self) -> tuple[int, ...]:
        """Edge-instance order: slots in canonical order, instances within a
        slot consecutive.  Length = E."""
        out: list[int] = []
        for k, m in enumerate(self.mult):
            out.extend([k] * m)
        return tuple(out)

    def instance_pairs(self) -> list[tuple[int, int]]:
        slots = slot_list(self.V)
        soi = self.slot_of_instances()
        return [slots[k] for k in soi]

    def serialize(self) -> str:
        return f"V{self.V}:" + ",".join(str(m) for m in self.mult)


def make_structure(V: int, mult, *, canonicalize: bool = True) -> Structure:
    """Build a Structure, optionally mapping to its canonical form first."""
    mult = tuple(mult)
    if len(mult) != n_slots(V):
        raise ValueError(f"mult has {len(mult)} slots, expected {n_slots(V)}")
    if canonicalize:
        mult = canonical_mult(V, mult)
    return Structure(V=V, mult=mult, aut=structure_automorphisms(V, mult))


# ---------------------------------------------------------------------------
# networks (structure + component assignment) and SP recognition
# ---------------------------------------------------------------------------

def permute_slot_keys(V: int, keys_per_slot: tuple, p) -> tuple:
    """Move per-slot content (sorted component-key tuples) along a node
    permutation; used to canonicalize component assignments."""
    si = slot_index(V)
    out: list[list] = [[] for _ in range(len(keys_per_slot))]
    for (i, j), keys in zip(slot_list(V), keys_per_slot):
        if not keys:
            continue
        a, b = p[i], p[j]
        if a > b:
            a, b = b, a
        out[si[(a, b)]].extend(keys)
    return tuple(tuple(sorted(g)) for g in out)


@dataclass(frozen=True)
class Network:
    """A candidate network: structure + assignment of components to edges.

    assign[t] = index into the canonical (sorted) component tuple, for edge
    instance t (instance order = Structure.slot_of_instances()).
    """

    structure: Structure
    assign: tuple[int, ...]

    def serialize(self, comp_keys: list[tuple]) -> tuple:
        """Canonical serialization: per-slot sorted component keys, minimized
        over the structure automorphism group.  Two networks of the same
        structure are electrically-identical-as-wirings iff equal serial."""
        V = self.structure.V
        soi = self.structure.slot_of_instances()
        per_slot: list[list[tuple]] = [[] for _ in range(n_slots(V))]
        for t, comp_idx in enumerate(self.assign):
            per_slot[soi[t]].append(comp_keys[comp_idx])
        base = tuple(tuple(sorted(g)) for g in per_slot)
        if len(self.structure.aut) <= 1:
            return base
        return min(permute_slot_keys(V, base, p) for p in self.structure.aut)


def is_series_parallel(V: int, mult) -> bool:
    """Valdes-Tarjan-Puech two-terminal series-parallel recognition.

    Repeatedly (a) merge parallel edges, (b) contract non-terminal degree-2
    nodes (series); the graph is two-terminal SP iff it reduces to the
    single edge (0,1).
    """
    if V < 2:
        return False
    counts = {(i, j): m for (i, j), m in zip(slot_list(V), mult) if m > 0}

    alive = set(range(V))
    while True:
        # (a) parallel reduction: collapse every multi-edge to a single edge
        for key in [k for k, m in counts.items() if m > 1]:
            counts[key] = 1
        # (b) one series reduction per pass (indices stay consistent);
        #     reductions are confluent, so the order does not matter
        deg = [0] * V
        inc: list[list[tuple[int, int]]] = [[] for _ in range(V)]
        for (a, b), m in counts.items():
            deg[a] += m
            deg[b] += m
            for _ in range(m):
                inc[a].append((a, b))
                inc[b].append((a, b))
        target = next((x for x in alive
                       if x not in (0, 1) and deg[x] == 2), None)
        if target is None:
            break
        x = target
        (a1, b1), (a2, b2) = inc[x][0], inc[x][1]
        for key in ((a1, b1), (a2, b2)):
            counts[key] -= 1
        for key in ((a1, b1), (a2, b2)):
            if counts.get(key, 0) <= 0:
                counts.pop(key, None)
        alive.discard(x)
        others = {a1, b1, a2, b2} - {x}
        if len(others) == 2:
            u, v = sorted(others)
            key = (u, v)
            counts[key] = counts.get(key, 0) + 1
        # len(others) == 1: both edges went to the same partner -> dead pair
    return counts.get((0, 1), 0) == 1 and len(counts) == 1
