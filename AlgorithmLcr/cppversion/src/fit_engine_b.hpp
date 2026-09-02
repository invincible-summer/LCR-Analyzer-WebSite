#pragma once
// Engine B: SK-iteration rational fit + Foster I/II synthesis — port of
// rlc_id/fit_engine_b.py (DESIGN.md section 6).  The pole-relocation variant
// of the Sanathanan-Koerner iteration (vector fitting), pole snapping,
// discrepancy-principle order selection (D10) and the Foster mapping tables
// (D8 guards included) are reproduced one-to-one.

#include "circuits.hpp"
#include "fit_engine_a.hpp"

#include <string>
#include <vector>

namespace rlc {

// significance threshold for dropping numerically irrelevant model terms
constexpr double kTermDropRel = 1e-9;
// a term is kept when its contribution exceeds the estimated noise floor by
// this factor somewhere in the band
constexpr double kTermSnr = 3.0;
// significance threshold for pole-structure pruning info (F3)
constexpr double kPoleSigRel = 1e-2;
// pole snapping: |p| below this fraction of the band edge is a pole at 0
constexpr double kPoleZeroRel = 0.05;
// pole snapping: |p| beyond this multiple of the band edge is a pole at inf
constexpr double kPoleInfRel = 1e4;
// D8 tolerance on the pair constant term
constexpr double kCPairTol = 1e-6;
// tank with parallel R above this multiple of max|Z| is treated as lossless
constexpr double kTankROpenRel = 1e2;
// series-branch R below this fraction of min|Z| is treated as a short
constexpr double kBranchRShortRel = 1e-2;

struct RationalModel {
    int order = 0;
    double omega0 = 1.0;
    double e = 0.0;  // e*s term
    double d = 0.0;  // constant term
    double k0 = 0.0; // k0/s term
    std::vector<Complex> poles;
    std::vector<Complex> residues;
    double rss = 0.0;
    double aicc = 0.0;
    int nUnknowns = 0;
    double selAicc = std::numeric_limits<double>::infinity();
    std::vector<RationalModel> alternatives;  // full per-order scan (D10)

    std::vector<Complex> zFit(const std::vector<Complex>& s) const;
    // (degree, n_complex_pairs, n_real_poles) counting only in-band
    // significant poles (noise-robust, F3)
    void poleStructure(const std::vector<Complex>& s, int& degree, int& nPair,
                       int& nReal) const;
};

// Scan orders 0..max_order, SK-iterate each, select by the discrepancy
// principle (D10).  `w` holds angular frequencies.
RationalModel skRationalFit(const std::vector<double>& w, const std::vector<Complex>& z,
                            const std::vector<double>& wts, int maxOrder = 4,
                            int nIters = 15);

// Conservative lower bound on reactive element count (F3 pruning): the
// minimum pole count among all rational models within deltaAicc of the best.
int conservativeEnergyBound(const RationalModel& model, const std::vector<Complex>& s,
                            double deltaAicc = 10.0);

struct FosterResult {
    std::vector<Candidate> candidates;  // includes skipped D8 entries
    RationalModel zModel;
    RationalModel yModel;
};

FosterResult fosterCandidates(const std::vector<double>& w, const std::vector<Complex>& z,
                              const std::vector<double>& wts, int maxOrder = 4,
                              int nIters = 15);

}  // namespace rlc
