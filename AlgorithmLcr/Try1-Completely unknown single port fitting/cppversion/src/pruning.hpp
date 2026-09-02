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

std::vector<TreePtr> pruneTrees(const std::vector<TreePtr>& trees,
                                const AsymptoticFeatures& feat, int minEnergy = 0,
                                bool enableF2 = true, bool enableF3 = true);

}  // namespace rlc
