#pragma once
// Unified adjacency-matrix output — port of rlc_id/adjacency.py implementing
// ../../OUTPUT_FORMAT.md.  Converts a fitted series-parallel tree + theta
// into the canonical upper-triangle adjacency matrix: rows[i][j] (i < j) is
// the vector of all edges directly connecting nodes i and j.  Nodes 0 and 1
// are the one-port terminals; internal chain nodes are numbered from 2 in
// emitter order.  An L device consumes two theta entries [log10 L, log10 Rd]
// and emits its fitted DC resistance; R and C edges always carry dcr = 0.

#include "circuits.hpp"
#include "fit_engine_a.hpp"

#include <string>
#include <vector>

namespace rlc {

struct Edge {
    char type = 'R';      // 'R' | 'L' | 'C'
    double parameter = 0.0;  // R[ohm] / L[H] / C[F]
    double dcr = 0.0;        // series DC resistance of L; 0 unless type == 'L'
};

class Adjacency {
public:
    explicit Adjacency(int V);  // V >= 2 (the two port terminals)

    int V() const { return V_; }
    // all edges directly connecting i and j; requires i < j
    std::vector<Edge>& slot(int i, int j);
    const std::vector<Edge>& slot(int i, int j) const;
    // append an undirected edge; {i, j} order is normalized; no self loops
    void add(int i, int j, Edge e);
    int nEdges() const;
    // non-empty slots in row-major upper-triangle order (spec sec.4)
    std::vector<std::tuple<int, int, const std::vector<Edge>*>> occupied() const;

    // unified print form: "adjacency[label] V=.. (ports 0,1):" + slot lines
    std::string formatBlock(const std::string& label = "") const;

private:
    int V_;
    // rows_[i][j - i - 1] = edges between (i, j), strict upper triangle
    std::vector<std::vector<std::vector<Edge>>> rows_;
};

// internal chain nodes the emitter allocates: k-1 per SER node
int nChainNodes(const TreePtr& tree);

// Realize a canonical SP tree between terminals 0 and 1.
// Deterministic numbering (locked rule): children are visited in stored
// canonical order; each SER node chains its children from the port-0 side,
// allocating k-1 fresh internal nodes (counter from 2); PAR children share
// the same terminal pair, forming multi-edges.  Values are consumed in leaf
// order (the theta convention): an L device takes [L, Rd].
Adjacency treeToAdjacency(const TreePtr& tree, const std::vector<double>& theta);
Adjacency candidateToAdjacency(const Candidate& cand);

}  // namespace rlc
