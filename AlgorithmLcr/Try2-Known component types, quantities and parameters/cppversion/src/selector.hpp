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
    // R4 value refinement: when non-empty, components with the FITTED values
    // in canonical compset order (assignment indices refer to this order).
    // Empty = the nominal compset values.
    std::vector<Component> comps;
    bool refined = false;
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

// R4 value refinement (OPTIMIZATION_LOG.md): real components carry tolerance
// and user-typed values are nominal, so exhaustive search on FIXED values
// plateaus at the tolerance error floor.  This stage takes the best
// (up to two assignments per) candidate of the top `topStructures` distinct
// structures and refines the p component parameters in log10 space around the
// nominal values (bounds +-`boundsDec` decades; DCR floors at kDcrRefineMin),
// then re-sorts everything by the refined RSS.  Selected candidates carry
// their refined `comps` vector; all others keep nominal values.
// R5b: bounds default +-0.3 decades (x[0.5, 2]) — wide enough for component
// tolerance and hand-typed values, tight enough that a wrong wiring cannot
// contort the values to out-fit the truth (the R4-trial +-0.9 let a mimic
// beat the truth on noisy synthetic data; see OPTIMIZATION_LOG.md R5).
std::vector<Candidate> refineTopCandidates(std::vector<Candidate> candidates,
                                           const ComponentSet& compset,
                                           const std::vector<Complex>& s,
                                           const std::vector<Complex>& z,
                                           const std::vector<double>& w,
                                           int topStructures = 8,
                                           double boundsDec = 0.9);

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
