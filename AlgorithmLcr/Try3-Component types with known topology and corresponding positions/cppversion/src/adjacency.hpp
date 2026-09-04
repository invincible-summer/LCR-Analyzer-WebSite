#pragma once
// Unified adjacency-matrix output (../../../OUTPUT_FORMAT.md section 5.3) --
// port of topofit_id/adjacency.py.  Fitted parameter groups are placed on
// their original node labels; reduced-away nodes survive as empty slots and
// merged/dropped edges become annotation lines.

#include "fit.hpp"

#include <string>
#include <vector>

namespace tf {

// One 2-terminal element (OUTPUT_FORMAT.md section 1); dcr is the series DC
// resistance of an inductor (the spec's parameterOfCapacitanceDCResistance).
struct Edge {
    char type = 'R';
    double parameter = 0.0;
    double dcr = 0.0;
};

class Adjacency {
public:
    explicit Adjacency(int V);
    int V() const { return V_; }
    std::vector<Edge>& slot(int i, int j);
    const std::vector<Edge>& slot(int i, int j) const;
    void add(int i, int j, Edge edge);
    int nEdges() const;
    std::vector<std::tuple<int, int, const std::vector<Edge>*>> occupied() const;
    std::string formatBlock(const std::string& label = "",
                            const std::vector<std::string>& extraLines = {}) const;

private:
    int V_;
    std::vector<std::vector<std::vector<Edge>>> rows_;
};

// V = max label + 1 over fitted groups and the original input edges.
int nodeSpan(const FitResult& res);

Adjacency fitresultToAdjacency(const FitResult& res);

// Annotation lines for merged/dropped original edges (spec sec.5.3).
std::vector<std::string> adjacencyNotes(const FitResult& res);

}  // namespace tf
