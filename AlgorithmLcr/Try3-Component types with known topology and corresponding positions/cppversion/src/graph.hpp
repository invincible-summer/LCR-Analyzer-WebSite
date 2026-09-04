#pragma once
// Multigraph representation and exact structure reductions -- port of
// topofit_id/graph.py (DESIGN.md sec.3).
//
// The DUT is a multigraph: nodes are junctions (0/1 = port), edges are
// components with known kind.  Four classes of value-independent exact
// reductions remove edges/parameters that cannot influence Z:
//   F1 self loops / edges outside the port component / dangling branches;
//   F2 same-kind parallel merge (R, C only);
//   F3 same-kind series merge at degree-2 internal nodes;
//   F4 series absorb of an R edge into an adjacent L edge (Rd shift).
// Merged edges keep an aggregation expression tree over the original edges.

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace tf {

class PortOpenError : public std::runtime_error {
public:
    explicit PortOpenError(const std::string& what) : std::runtime_error(what) {}
};

// Aggregation tree over original edges: leaf {isLeaf, edgeIdx, kind} or
// inner {isLeaf=false, tag 's'|'p', children, kind}.
struct Expr {
    bool leaf = true;
    int edgeIdx = -1;              // leaf: original edge index
    char tag = 'e';                // inner: 's' (series) | 'p' (parallel)
    char kind = 'R';               // resulting kind of this subtree
    std::vector<Expr> children;    // inner only
};

// Value tuple: ("R", r) | ("C", c) | ("L", l, rd)
struct Value {
    char kind = 'R';
    double v1 = 0.0;
    double v2 = 0.0;  // rd, L only
};

// Evaluate an aggregation tree on original-edge values (graph.eval_group).
Value evalGroup(const Expr& expr, const std::vector<Value>& origVals);

struct ReducedEdge {
    int u = 0, v = 0;   // original node labels
    char kind = 'R';
    Expr expr;
    std::vector<int> members;  // original edge indices in the group
};

struct ReductionResult {
    std::vector<ReducedEdge> edges;
    std::map<int, std::string> dropped;  // original edge idx -> reason
    int nPasses = 0;
    int nGroups() const { return (int)edges.size(); }
    std::vector<Value> groupValues(const std::vector<Value>& origVals) const;
    std::string describe() const;
};

// throws PortOpenError when nodes 0/1 end up disconnected / nothing remains
ReductionResult reduceGraph(const std::vector<std::tuple<int, int, char>>& edges);

}  // namespace tf
