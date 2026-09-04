#pragma once
// Isomorph-free two-phase enumeration -- port of netgraph_id/enumerate.py
// (DESIGN.md section 4).
//
// Phase 1 (structure layer): for V = 2..E+1 enumerate every multiset of E
// edges over the S(V) node-pair slots (combinations with replacement, the
// itertools lexicographic order), keep connected R0-clean structures,
// canonicalize and dedup.  Phase 2 (assignment layer): for each structure,
// enumerate component permutations in itertools order, dedup by the
// canonical per-slot serialization minimized over the structure automorphism
// group (see graph.hpp).

#include "components.hpp"
#include "graph.hpp"

#include <functional>
#include <vector>

namespace ng {

// All admissible 2-terminal multigraph structures with exactly E edges,
// sorted by (V, mult).  Cached per (E, allow_dead); thread-safe.
const std::vector<Structure>& enumerateStructures(int E, bool allowDead = false);

// Stream every distinct component assignment (component indices per edge
// instance, instance order) for one structure, one per wiring orbit.
void iterAssignments(const Structure& structure, const ComponentSet& compset,
                     const std::function<void(const std::vector<int>&)>&& yield);

int countAssignments(const Structure& structure, const ComponentSet& compset);

}  // namespace ng
