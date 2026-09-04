#pragma once
// Batched nodal analysis of multigraph networks -- port of
// netgraph_id/nodal.py (DESIGN.md section 5).
//
// Stamping: edge (u, v) with admittance y contributes Y[u,u] += y,
// Y[v,v] += y, Y[u,v] -= y, Y[v,u] -= y; parallel edges land in the same
// entries so their admittances add.  Node 0 is grounded; with unit current
// injected at terminal node 1 the driving-point impedance is
// Z = (Y_red^-1)[0, 0] over the (V-1)x(V-1) reduced matrix of nodes 1..V-1.

#include "components.hpp"
#include "graph.hpp"

#include <complex>
#include <vector>

namespace ng {

using Complex = std::complex<double>;

// LU factorization with partial pivoting for a small complex matrix
// (row-major n x n).  Returns false when exactly singular.
bool luSolveComplex(std::vector<Complex>& A, int n, std::vector<Complex>& b);

struct StructureStamps {
    Structure structure;
    std::vector<char> kinds;         // per component
    std::vector<double> vals;
    std::vector<double> dcrs;
    std::vector<std::pair<int, int>> pairNodes;  // occupied slots, slot order
    std::vector<int> groupStarts;    // instance-order boundaries per pair
    std::vector<std::pair<int, int>> diagTargets;    // (pair, reduced node)
    std::vector<std::tuple<int, int, int>> offdiag;  // (pair, ri, rj)

    static StructureStamps build(const Structure& structure, const ComponentSet& compset);
    // same, from an arbitrary ordered component vector (R4 value refinement:
    // refined candidates carry per-candidate values in compset order)
    static StructureStamps build(const Structure& structure,
                                 const std::vector<Component>& comps);

    // Z at one frequency for a batch of assignments (each E component indices).
    std::vector<Complex> zBatch(const std::vector<std::vector<int>>& assigns, Complex s) const;
    // Z at all frequencies; out[candidate][freq].
    std::vector<std::vector<Complex>> zFull(const std::vector<std::vector<int>>& assigns,
                                            const std::vector<Complex>& sArray) const;
};

// Exact Z(f) of a single network (reports, synthetic data).
std::vector<Complex> networkZ(const Network& network, const ComponentSet& compset,
                              const std::vector<double>& f);
// Z(f) with an explicit component vector (R4: refined values; order must match
// the assignment indices, i.e. the canonical compset order).
std::vector<Complex> networkZValues(const Network& network,
                                    const std::vector<Component>& comps,
                                    const std::vector<double>& f);

// Z(0) ("dc") or Z(inf) ("hf") asymptotic invariants (DESIGN.md 5.3).
// dc: ideal inductors are shorts, capacitors open; hf: capacitors short,
// inductors open.  Returns inf when the resistive remainder is open.
double asymptoteImpedance(const Network& network, const ComponentSet& compset, bool dc);

}  // namespace ng
