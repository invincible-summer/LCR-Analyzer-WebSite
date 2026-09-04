#pragma once
// Candidate ranking and equivalence clustering -- port of
// netgraph_id/selector.py (DESIGN.md section 8).
//
// Rank by RSS (AICc adds a constant since every candidate shares the same
// component set), then merge electrically equivalent candidates: distinct
// wirings can share the same Z(s) exactly (graph symmetries, Whitney
// 2-isomorphism, value-coincidence Y-delta), so the correct output is an
// ordered list of equivalence classes.  Equivalence test (Try1 selector):
// max relative difference of Z on a validation grid (band expanded 10x at
// each end, 200 log-spaced points) below max(tol, 3 * sigma_rel_hat).

#include "components.hpp"
#include "graph.hpp"
#include "metric.hpp"
#include "nodal.hpp"

#include <vector>

namespace ng {

struct Candidate {
    Network network;
    std::vector<Complex> zFit;   // model Z at the measured frequencies
    double rss = 0.0;
    double aiccVal = 0.0;
    double wrmse = 0.0;
    double maxRelErr = 0.0;
    bool sp = true;              // series-parallel wiring?
    int nInternal() const { return network.structure.nInternal(); }
};

struct EquivalenceClass {
    Candidate representative;
    std::vector<Candidate> members;
    double rss() const { return representative.rss; }
    double wrmse() const { return representative.wrmse; }
    double maxRelErr() const { return representative.maxRelErr; }
    double aicc() const { return representative.aiccVal; }
    int nMembers() const { return 1 + (int)members.size(); }
};

// Full-band evaluation of funnel survivors into Candidates (RSS sorted).
std::vector<Candidate> evaluateCandidates(const std::vector<Network>& nets,
                                          const ComponentSet& compset,
                                          const std::vector<Complex>& s,
                                          const std::vector<Complex>& z,
                                          const std::vector<double>& w,
                                          int batchSize = 4096);

// Band expanded `expand`x at each end, n log-spaced points.
std::vector<double> makeValidationGrid(const std::vector<double>& f, int n = 200,
                                       double expand = 10.0);

bool relDiffBelow(const std::vector<Complex>& za, const std::vector<Complex>& zb,
                  double tol);

bool areEquivalent(const Network& a, const Network& b, const ComponentSet& compset,
                   const std::vector<double>& grid, double tol);

std::vector<EquivalenceClass> rankAndCluster(std::vector<Candidate> candidates,
                                             const ComponentSet& compset,
                                             const std::vector<double>& f,
                                             int clusterTop = 50,
                                             double equivTol = 1e-3);

}  // namespace ng
