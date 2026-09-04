#include "selector.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace rlc {

std::vector<double> makeValidationGrid(const std::vector<double>& f, double expand,
                                       int nPoints) {
    double fMin = *std::min_element(f.begin(), f.end()) / expand;
    double fMax = *std::max_element(f.begin(), f.end()) * expand;
    return geomspace(fMin, fMax, nPoints);
}

bool areEquivalent(const Candidate& c1, const Candidate& c2,
                   const std::vector<double>& fGrid, double tol) {
    if (c1.skipped || c2.skipped) return false;
    const size_t m = fGrid.size();
    std::vector<Complex> z1(m), z2(m);
    evalThetaFreq(c1.tree, c1.theta, fGrid.data(), m, z1.data());
    evalThetaFreq(c2.tree, c2.theta, fGrid.data(), m, z2.data());
    double maxRel = 0.0;
    for (size_t k = 0; k < m; ++k) {
        double rel = std::abs(z1[k] - z2[k]) / std::max(std::abs(z1[k]), 1e-300);
        maxRel = std::max(maxRel, rel);
    }
    return maxRel < tol;
}

std::pair<int, double> secondarySortKey(const Candidate& cand) {
    // center deviation penalty: sum of (log10(v) - mid)^2
    // typical nominal centers: R = 1k (3), L = 1 mH (-3), Rd = 1 ohm (0),
    // C = 10 nF (-8); iterated over PARAMETER kinds (two per L device)
    std::map<char, double> center{{'R', 3.0}, {'L', -3.0}, {'D', 0.0}, {'C', -8.0}};
    auto kinds = paramKinds(cand.tree);
    double penalty = 0.0;
    for (size_t i = 0; i < kinds.size(); ++i) {
        double c = 0.0;
        auto it = center.find(kinds[i]);
        if (it != center.end()) c = it->second;
        double dd = cand.theta[i] - c;
        penalty += dd * dd;
    }
    return {cand.nParams(), penalty};
}

std::vector<EquivalenceClass> rankAndClusterEquivalent(std::vector<Candidate> candidates,
                                                       const std::vector<double>& f,
                                                       double equivTol, int nObsIn) {
    std::vector<Candidate> valid;
    for (auto& c : candidates) {
        if (!c.skipped && std::isfinite(c.aiccVal)) valid.push_back(std::move(c));
    }
    if (valid.empty()) return {};

    int nObs = nObsIn > 0 ? nObsIn : 2 * 60;  // fallback; callers pass 2M

    // noise-floor estimate from the best achievable per-dof RSS
    double sigma2Hat = std::numeric_limits<double>::infinity();
    for (const auto& c : valid) {
        double r = c.rss / std::max((double)(nObs - c.nParams()), 1.0);
        sigma2Hat = std::min(sigma2Hat, r);
    }
    double margin = 3.0 * std::sqrt(2.0 / std::max(nObs, 1));
    std::vector<size_t> consistentIdx;
    for (size_t i = 0; i < valid.size(); ++i) {
        double r = valid[i].rss / std::max((double)(nObs - valid[i].nParams()), 1.0);
        if (r <= sigma2Hat * (1.0 + margin)) consistentIdx.push_back(i);
    }
    int minP = std::numeric_limits<int>::max();
    for (size_t i : consistentIdx) minP = std::min(minP, valid[i].nParams());
    size_t championIdx = 0;
    bool hasChampion = false;
    double bestA = std::numeric_limits<double>::infinity();
    for (size_t i : consistentIdx) {
        if (valid[i].nParams() == minP && valid[i].aiccVal < bestA) {
            bestA = valid[i].aiccVal;
            championIdx = i;
            hasChampion = true;
        }
    }

    // order: champion first, the rest by AICc (stable)
    std::vector<size_t> restIdx;
    for (size_t i = 0; i < valid.size(); ++i) {
        if (!hasChampion || i != championIdx) restIdx.push_back(i);
    }
    std::stable_sort(restIdx.begin(), restIdx.end(),
                     [&](size_t a, size_t b) { return valid[a].aiccVal < valid[b].aiccVal; });
    std::vector<Candidate> ordered;
    if (hasChampion) ordered.push_back(valid[championIdx]);
    for (size_t i : restIdx) ordered.push_back(valid[i]);

    // noise-aware equivalence tolerance (two independent noise-floor fits of
    // the same circuit must not split into two classes)
    double sigmaRelHat = std::sqrt(sigma2Hat);
    double effTol = std::max(equivTol, 3.0 * sigmaRelHat);

    std::vector<double> fGrid = makeValidationGrid(f);
    std::vector<EquivalenceClass> classes;
    for (auto& cand : ordered) {
        bool matched = false;
        for (auto& eq : classes) {
            if (areEquivalent(cand, eq.representative, fGrid, effTol)) {
                eq.members.push_back(std::move(cand));
                matched = true;
                break;
            }
        }
        if (!matched) {
            EquivalenceClass cls;
            cls.representative = cand;
            cls.members.push_back(std::move(cand));
            classes.push_back(std::move(cls));
        }
    }

    for (auto& eq : classes) {
        std::stable_sort(eq.members.begin(), eq.members.end(),
                         [](const Candidate& a, const Candidate& b) {
                             return secondarySortKey(a) < secondarySortKey(b);
                         });
    }
    return classes;
}

}  // namespace rlc
