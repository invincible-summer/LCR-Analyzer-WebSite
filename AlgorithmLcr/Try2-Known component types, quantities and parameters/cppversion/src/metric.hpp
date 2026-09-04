#pragma once
// Goodness-of-fit metrics, formula-identical to Try1 -- port of
// netgraph_id/metric.py (DESIGN.md section 6).  Try2 has no free parameters,
// so all candidates share K = n_params + 1 and AICc ordering degenerates to
// RSS ordering; the AICc value is still reported for continuity.

#include "nodal.hpp"

#include <vector>

namespace ng {

std::vector<double> defaultWeights(const std::vector<Complex>& z);  // 1/|z|

// Interleaved [Re r_1, Im r_1, Re r_2, ...] weighted residual.
std::vector<double> residualVector(const std::vector<Complex>& z,
                                   const std::vector<Complex>& zModel,
                                   const std::vector<double>& w);

double rssOf(const std::vector<double>& residual);

// Corrected AIC (Try1 section 5.5); n_obs = 2M, K = p + 1.
double aicc(double rss, int nObs, int p);

// Relative RMSE and max relative error (Try1 section 8.1).
std::pair<double, double> fitMetrics(const std::vector<Complex>& z,
                                     const std::vector<Complex>& zfit);

// RSS of the weighted residual for a batch of candidates:
// zModel[c][k] vs z[k], weights w[k].
std::vector<double> weightedRssBatch(const std::vector<Complex>& z,
                                     const std::vector<std::vector<Complex>>& zModel,
                                     const std::vector<double>& w);

double weightedRss(const std::vector<Complex>& z, const std::vector<Complex>& zModel,
                   const std::vector<double>& w);

}  // namespace ng
