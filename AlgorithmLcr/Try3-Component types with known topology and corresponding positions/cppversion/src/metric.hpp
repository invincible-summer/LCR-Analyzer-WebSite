#pragma once
// Goodness-of-fit metrics, formula-identical to Try1 -- port of
// topofit_id/metric.py (DESIGN.md sec.6).

#include "graph.hpp"
#include "linalg.hpp"

#include <utility>
#include <vector>

namespace tf {

std::vector<double> defaultWeights(const std::vector<Complex>& z);

std::vector<double> residualVector(const std::vector<Complex>& z,
                                   const std::vector<Complex>& zModel,
                                   const std::vector<double>& w);

double rssOf(const std::vector<double>& residual);

double aicc(double rss, int nObs, int p);

std::pair<double, double> fitMetrics(const std::vector<Complex>& z,
                                     const std::vector<Complex>& zfit);

double curveMaxRel(const std::vector<Complex>& zTrue, const std::vector<Complex>& zFit);

// Max relative curve error with the denominator floored at
// floor_frac * median|z_true| (dynamic-range-aware Bode comparison).
double curveMaxRelFloored(const std::vector<Complex>& zTrue,
                          const std::vector<Complex>& zFit, double floorFrac = 0.1);

// Per-parameter relative errors between fitted and true group values,
// resolving interchangeable-group permutations (same node pair + kind).
// Labels are "u,v,kind:param" strings (informational).
std::pair<std::vector<double>, std::vector<std::string>> matchedGroupErrors(
    const std::vector<Value>& fitVals, const std::vector<Value>& trueVals,
    const std::vector<ReducedEdge>& reducedEdges);

}  // namespace tf
