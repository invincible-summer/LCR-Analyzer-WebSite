#pragma once
// Pruning funnel -- port of netgraph_id/filters.py (DESIGN.md section 7).
//
// With known component values every candidate is exactly evaluable at any
// frequency, so the cheapest complete filter is a small set of probe
// frequencies (band edges + middle): a wrong wiring of exact-valued
// components is wrong by O(1) relative error somewhere in the band while
// the true wiring sits at the noise floor.  The funnel keeps every
// candidate whose probe RSS is within funnel_ratio of the running best.

#include "components.hpp"
#include "enumerate.hpp"
#include "metric.hpp"
#include "nodal.hpp"

#include <limits>
#include <utility>
#include <vector>

namespace ng {

// Probe frequency indices: band edges + interior.  Python round() is
// banker's rounding; roundHalfEven mirrors it so both implementations pick
// identical probes.
long roundHalfEven(double x);
std::vector<int> coarseIndices(int M, int nPoints = 3);

struct FunnelState {
    ComponentSet compset;
    std::vector<Complex> s;          // full angular frequencies (M,)
    std::vector<Complex> z;          // measurements
    std::vector<double> w;           // weights
    std::vector<int> probeIdx;
    double funnelRatio = 1e6;
    int minKeep = 200;
    int batchSize = 4096;

    double bestProbe = std::numeric_limits<double>::infinity();
    std::vector<std::pair<double, Network>> kept;  // (probe_rss, network)
    long nTotal = 0;
    long nProbeEvaluated = 0;

    void update(const std::vector<Network>& nets, const std::vector<double>& probeRss);
    std::vector<Network> finalKeep() const;
};

FunnelState runFunnel(const ComponentSet& compset, const std::vector<Complex>& s,
                      const std::vector<Complex>& z, const std::vector<double>& w,
                      int coarsePoints, double funnelRatio, int minKeep, int batchSize,
                      bool allowDead);

}  // namespace ng
