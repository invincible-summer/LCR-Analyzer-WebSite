"""Isomorph-free two-phase enumeration of 2-terminal networks (DESIGN.md 4).

Phase 1 (structure layer): for V = 2..E+1 enumerate every multiset of E
edges over the S(V) = V(V-1)/2 node-pair slots (combinations with
replacement), keep connected R0-clean structures, canonicalize (minimum
over the relabeling group) and deduplicate.  Every isomorphism class of
admissible uniform structures is emitted exactly once (completeness proof
in DESIGN.md 4.2).

Phase 2 (assignment layer): for each structure, assign the concrete known
component multiset to the edge instances.  Components are applied in the
canonical sorted order; equal components are interchangeable; permutations
are deduplicated by the canonical per-slot serialization minimized over the
structure automorphism group.
"""

from __future__ import annotations

from functools import lru_cache
from itertools import combinations_with_replacement, permutations

from .components import ComponentSet
from .graph import (Structure, Network, canonical_mult, empty_mult, is_connected,
                    has_dead_part, make_structure, n_slots, permute_slot_keys,
                    slot_list)


@lru_cache(maxsize=64)
def enumerate_structures(E: int, *, allow_dead: bool = False) -> tuple[Structure, ...]:
    """All admissible 2-terminal multigraph structures with exactly E edges.

    Result is cached per (E, allow_dead); sorted by (V, mult) for
    determinism.
    """
    if E < 1:
        raise ValueError("E must be >= 1")
    out: list[Structure] = []
    for V in range(2, E + 2):
        S = n_slots(V)
        seen: set[tuple[int, ...]] = set()
        for combo in combinations_with_replacement(range(S), E):
            mult = empty_mult(V)
            for k in combo:
                mult[k] += 1
            if not is_connected(V, mult):
                continue
            if not allow_dead and has_dead_part(V, mult):
                continue
            cm = canonical_mult(V, mult)
            if cm in seen:
                continue
            seen.add(cm)
            out.append(make_structure(V, cm, canonicalize=False))
    out.sort(key=lambda st: (st.V, st.mult))
    return tuple(out)


def iter_assignments(structure: Structure, compset: ComponentSet):
    """Yield all distinct component assignments (tuples of component indices
    per edge instance) for one structure, one per wiring-equivalence orbit.

    Dedup keys: per-slot sorted component-key tuples over the FULL slot
    layout (empty slots are ()), minimized over Aut when |Aut| > 1.
    Parallel-edge orderings collapse via the within-slot sort.
    """
    comps = compset.components
    E = structure.n_edges
    V = structure.V
    if E != len(comps):
        raise ValueError("structure edge count != component count")
    keys = [c.key() for c in comps]

    inst_by_slot: list[list[int]] = [[] for _ in range(n_slots(V))]
    for t, slot in enumerate(structure.slot_of_instances()):
        inst_by_slot[slot].append(t)

    aut = structure.aut if len(structure.aut) > 1 else ()
    seen: set = set()
    for perm in permutations(range(E)):
        ser = tuple(tuple(sorted(keys[perm[t]] for t in occ)) if occ else ()
                    for occ in inst_by_slot)
        if aut:
            ser = min(permute_slot_keys(V, ser, p) for p in aut)
        if ser in seen:
            continue
        seen.add(ser)
        yield perm


def count_assignments(structure: Structure, compset: ComponentSet) -> int:
    """Number of distinct assignments (consumes the iterator)."""
    return sum(1 for _ in iter_assignments(structure, compset))


def iter_networks(compset: ComponentSet, structures) :
    """Stream all candidate Networks for a component set."""
    for st in structures:
        for assign in iter_assignments(st, compset):
            yield Network(structure=st, assign=assign)
