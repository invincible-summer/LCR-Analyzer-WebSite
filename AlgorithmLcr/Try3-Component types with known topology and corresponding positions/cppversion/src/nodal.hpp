#pragma once
// Batched nodal analysis with adjoint sensitivities -- port of
// topofit_id/nodal.py (DESIGN.md sec.4).
//
// Stamping (Try2 convention): edge (u,v) with admittance y contributes
// Y[u,u]+=y, Y[v,v]+=y, Y[u,v]-=y, Y[v,u]-=y; node 0 grounded, unit current
// injected at node 1: Z(s) = (Y_red^-1)[0, 0] over the (V-1)x(V-1) reduced
// matrix of non-ground nodes (node 1 first).
//
// theta = log10 of linear values, laid out per edge: R -> [log R];
// C -> [log C]; L -> [log L, log Rd].  The Jacobian dZ/dtheta comes from the
// adjoint method: with x = Y_red^-1 e_0,
//     dZ/dtheta_t[k] = -(dy_t/dtheta_t)[k] * (x[k, ri] - x[k, rj])^2
// (Director & Rohrer 1969); after one LU per frequency the whole p x M
// Jacobian costs O(M*p).

#include "graph.hpp"
#include "linalg.hpp"

#include <vector>

namespace tf {

struct NodalModel {
    std::vector<std::tuple<int, int, char>> edges;  // (u, v, kind)
    std::vector<int> nodeOf;  // reduced index -> original label; [0] is node 1
    int nParams = 0;
    std::vector<std::pair<int, int>> edgeParamSlices;  // per edge (start, count)
    std::vector<int> riList;  // per edge reduced index of u (-1 = ground)
    std::vector<int> rjList;  // per edge reduced index of v (-1 = ground)

    static NodalModel fromEdges(const std::vector<std::tuple<int, int, char>>& edges);
    int nNodesRed() const { return (int)nodeOf.size(); }

    // Z (M) and full Jacobian dZ/dtheta (p x M, row-major p rows of M).
    void zAndJac(const std::vector<double>& theta, const std::vector<Complex>& s,
                 std::vector<Complex>& Z, std::vector<Complex>& J) const;

    std::vector<Complex> zLinear(const std::vector<double>& vals,
                                 const std::vector<Complex>& s) const;

    // E[t, k] = dlnZ_k / dln(value_t) = J_t / (Z * ln10)  (p x M row-major)
    void elasticity(const std::vector<double>& theta, const std::vector<Complex>& s,
                    std::vector<Complex>& E) const;
};

NodalModel modelFromReduced(const ReductionResult& red);

}  // namespace tf
