#pragma once
// Pruning filters — port of rlc_id/pruning.py (DESIGN.md section 5.4/6.1/7):
// F2 asymptotic slope / termination filter and F3 pole-structure energy bound.

#include "circuits.hpp"
#include "fit_engine_a.hpp"

#include <vector>

namespace rlc {

struct AsymptoticFeatures {
    double slopeLow = 0.0;      // log10|Z| / log10(w) at the low-frequency end
    double slopeHigh = 0.0;     // at the high-frequency end
    double phaseLowDeg = 0.0;   // phase at the lowest frequency [deg]
    double phaseHighDeg = 0.0;  // phase at the highest frequency [deg]
    double rLevel = 0.0;        // flat-region resistance magnitude estimate
    double rPeak = 0.0;         // max|Z| (parallel-resonance peak level)
    double rFloor = 0.0;        // min|Z| (series-resonance floor level)
    double lEst = 0.0;          // inductance estimate from the band ends
    double cEst = 0.0;          // capacitance estimate from the band ends
    bool hasWRes = false;
    double wRes = 0.0;          // interior extremum angular frequency, if any
};

AsymptoticFeatures extractAsymptotics(const std::vector<double>& w,
                                      const std::vector<Complex>& z);

StartHints hintsFromFeatures(const AsymptoticFeatures& feat);

// Asymptotic log-log slope bounds for s -> inf: (min, max) in {-1, 0, +1}.
std::pair<int, int> highFreqSlopeRange(const TreePtr& tree);

// true if the tree should be KEPT
bool pruneF2(const TreePtr& tree, const AsymptoticFeatures& feat);
bool pruneF3(const TreePtr& tree, int minEnergy);

// number of energy-storage leaves (L or C) — the F3 sort key
int energyCount(const TreePtr& tree);

// F2 filters destructively (asymptotic physics — reliable).  F3 does NOT:
// under real-world noise the rational-fit pole count systematically
// OVERestimates the required energy storage (OPTIMIZATION_LOG.md R1: measured
// bound=4 on data4 whose truth has 2), so pruning by it can delete the truth.
// Instead the F2-safe library is returned SORTED by ascending energy count so
// the engine-A funnel spends its refine budget on the most plausible (small)
// candidates first, without ever discarding a possible truth.
// `minEnergy`/`enableF3` are accepted for compatibility and ignored.
std::vector<TreePtr> pruneTrees(const std::vector<TreePtr>& trees,
                                const AsymptoticFeatures& feat, int minEnergy = 0,
                                bool enableF2 = true, bool enableF3 = true);

}  // namespace rlc
