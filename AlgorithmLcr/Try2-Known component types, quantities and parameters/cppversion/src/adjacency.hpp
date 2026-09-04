#pragma once
// Unified adjacency-matrix output (../../../OUTPUT_FORMAT.md section 5.2) --
// port of netgraph_id/adjacency.py.  Expands a candidate Network (structure
// + assignment) into the canonical upper-triangle adjacency matrix:
// rows[i][j] (i < j) is the vector of all edges directly connecting i and j.

#include "components.hpp"
#include "graph.hpp"
#include "selector.hpp"

#include <string>
#include <vector>

namespace ng {

// One 2-terminal element (OUTPUT_FORMAT.md section 1).  Field name
// parameterOfCapacitanceDCResistance is spelled dcr here as in the Python
// reference; the semantics is the series DC resistance of an inductor.
struct Edge {
    char type = 'R';      // 'R' | 'L' | 'C'
    double parameter = 0.0;
    double dcr = 0.0;
};

class Adjacency {
public:
    explicit Adjacency(int V);
    int V() const { return V_; }
    std::vector<Edge>& slot(int i, int j);              // requires i < j
    const std::vector<Edge>& slot(int i, int j) const;
    void add(int i, int j, Edge edge);
    int nEdges() const;
    // Non-empty slots in row-major upper-triangle order.
    std::vector<std::tuple<int, int, const std::vector<Edge>*>> occupied() const;
    // Unified print form (OUTPUT_FORMAT.md section 4).
    std::string formatBlock(const std::string& label = "",
                            const std::vector<std::string>& extraLines = {}) const;

private:
    int V_;
    std::vector<std::vector<std::vector<Edge>>> rows_;
};

Adjacency networkToAdjacency(const Network& network, const ComponentSet& compset);
Adjacency networkToAdjacency(const Network& network, const std::vector<Component>& comps);
Adjacency candidateToAdjacency(const Candidate& cand, const ComponentSet& compset);
// R4 overload: refined candidates carry their own component values (in
// canonical compset order).
Adjacency candidateToAdjacency(const Candidate& cand, const std::vector<Component>& comps);

}  // namespace ng
