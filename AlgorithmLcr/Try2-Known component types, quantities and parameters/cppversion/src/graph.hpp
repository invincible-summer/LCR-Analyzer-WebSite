#pragma once
// Multigraph model of a 2-terminal RLC network -- port of
// netgraph_id/graph.py (DESIGN.md section 3).
//
// A network is a connected undirected multigraph: nodes 0 and 1 are the two
// port terminals, nodes 2..V-1 are internal junctions; edges are components,
// parallel edges are legal, self loops never occur.  Two representations:
//   * structure layer: multiplicity vector over the canonical slot list
//     (row-major upper triangle) -- enumeration / canonicalization / dedup;
//   * assignment layer: edge instances carrying concrete components.
//
// R0 dead-part rule (DESIGN.md 2.5/4.2): a connected component of G - c
// containing no terminal is electrically dead (net KCL gives zero current
// through c), so such structures are excluded.

#include "components.hpp"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ng {

// Canonical slot list [(0,1),(0,2),...,(1,2),...,(V-2,V-1)] (cached per V).
const std::vector<std::pair<int, int>>& slotList(int V);
int slotIndex(int V, int i, int j);  // index of slot (i<j) in the row-major order
int nSlots(int V);
std::vector<int> emptyMult(int V);
std::vector<int> multDegree(int V, const std::vector<int>& mult);

bool isConnected(int V, const std::vector<int>& mult);
bool hasDeadPart(int V, const std::vector<int>& mult);
bool structureOk(int V, const std::vector<int>& mult, bool allowDead);

// Relabeling group G = {terminal swap} x Sym(internal nodes 2..V-1);
// p[i] = image of node i.
std::vector<std::vector<int>> permGroup(int V);
std::vector<int> permuteMult(int V, const std::vector<int>& mult,
                             const std::vector<int>& p);
// Canonical representative = lexicographic minimum over the group.
std::vector<int> canonicalMult(int V, const std::vector<int>& mult);
// All group elements fixing the (canonical) mult vector.
std::vector<std::vector<int>> structureAutomorphisms(int V, const std::vector<int>& mult);

struct Structure {
    int V = 2;
    std::vector<int> mult;
    std::vector<std::vector<int>> aut;

    int nEdges() const { int s = 0; for (int m : mult) s += m; return s; }
    int nInternal() const { return V - 2; }
    std::string key() const;                 // "V:k,k,k..."
    std::vector<int> degrees() const { return multDegree(V, mult); }
    std::vector<int> slotOfInstances() const;  // slot of each edge instance
    std::string serialize() const;             // human form "V3:0,1,2"
};

Structure makeStructure(int V, std::vector<int> mult, bool canonicalize = true);

// Move per-slot content (sorted component-key lists) along a node
// permutation; used to canonicalize component assignments.
std::vector<std::vector<Component>> permuteSlotKeys(
    int V, const std::vector<std::vector<Component>>& keysPerSlot,
    const std::vector<int>& p);

struct Network {
    Structure structure;
    std::vector<int> assign;  // component index per edge instance (instance order)

    // Canonical serialization: per-slot sorted component keys over the FULL
    // slot layout, minimized over the structure automorphism group.
    std::vector<std::vector<Component>> serialize(
        const std::vector<Component>& comps) const;
};

// Valdes-Tarjan-Puech two-terminal series-parallel recognition.
bool isSeriesParallel(int V, const std::vector<int>& mult);

}  // namespace ng
