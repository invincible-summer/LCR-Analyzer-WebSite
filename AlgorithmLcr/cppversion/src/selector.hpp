#pragma once
// Selector: candidate merge, equivalence clustering and ranking — port of
// rlc_id/selector.py (DESIGN.md section 5.5, D6; section 7 F6).

#include "circuits.hpp"
#include "fit_engine_a.hpp"

#include <string>
#include <vector>

namespace rlc {

constexpr double kEquivBandExpand = 10.0;
constexpr int kEquivNPoints = 200;
constexpr double kEquivMaxRelTol = 1e-3;

struct EquivalenceClass {
    Candidate representative;
    std::vector<Candidate> members;

    double aicc() const { return representative.aiccVal; }
    double wrmse() const { return representative.wrmse; }
    double maxRelErr() const { return representative.maxRelErr; }
    int nParams() const { return representative.nParams(); }
};

// log-spaced validation grid expanded beyond the measured band
std::vector<double> makeValidationGrid(const std::vector<double>& f,
                                       double expand = kEquivBandExpand,
                                       int nPoints = kEquivNPoints);

bool areEquivalent(const Candidate& c1, const Candidate& c2,
                   const std::vector<double>& fGrid, double tol = kEquivMaxRelTol);

// secondary criterion: fewer elements first, then parameter plausibility
std::pair<int, double> secondarySortKey(const Candidate& cand);

std::vector<EquivalenceClass> rankAndClusterEquivalent(std::vector<Candidate> candidates,
                                                       const std::vector<double>& f,
                                                       double equivTol = kEquivMaxRelTol,
                                                       int nObs = -1);

}  // namespace rlc
