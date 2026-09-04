#include "selector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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

double estimateRelativeNoise(const std::vector<double>& w,
                             const std::vector<Complex>& z) {
    // Local-quadratic detrending noise estimator.  For y_k = g_k + iid noise
    // with std s, the residual sum of squares of a quadratic LSQ fit through
    // 5 consecutive points has E[SSE] = 2 s^2 (2 residual dof); the quadratic
    // absorbs the smooth trend up to 2nd order, so unlike plain second
    // differences this stays unbiased across resonance regions where the
    // curvature of g varies point to point.  The median over windows keeps
    // isolated outliers from inflating the estimate.  ln|Z| and arg Z errors
    // of a small isotropic relative perturbation both equal the relative std,
    // so sigma_rel = sqrt(s_ln^2 + s_phi^2) matches the wRMSE scale of the
    // true model (E[wrmse^2] = s_ln^2 + s_phi^2).
    (void)w;  // trend is removed locally; only point ordering matters
    const size_t m = z.size();
    if (m < 7) return -1.0;
    std::vector<double> y(m), ph(m);
    for (size_t k = 0; k < m; ++k) {
        y[k] = std::log(std::max(std::abs(z[k]), 1e-300));
        ph[k] = std::atan2(z[k].imag(), z[k].real());
    }
    // quadratic coefficients for a 5-point window (indices -2..2):
    // orthogonal basis {p0, p1, p2} = {1, t, t^2-2} on t in {-2,-1,0,1,2}
    // (t^2-2 = (2,-1,-2,-1,2) is orthogonal to 1 and t), so the 2-dof
    // residual SSE is
    //   SSE = <y,y> - <y,p0>^2/<p0,p0> - <y,p1>^2/<p1,p1> - <y,p2>^2/<p2,p2>
    auto windowSse = [](const double* v) {
        double p1[5] = {-2, -1, 0, 1, 2};
        double p2[5] = {2, -1, -2, -1, 2};
        double yy = 0, a0 = 0, a1 = 0, a2 = 0;
        for (int i = 0; i < 5; ++i) {
            yy += v[i] * v[i];
            a0 += v[i];
            a1 += v[i] * p1[i];
            a2 += v[i] * p2[i];
        }
        return yy - a0 * a0 / 5.0 - a1 * a1 / 10.0 - a2 * a2 / 14.0;
    };
    auto robustSigma = [windowSse](const std::vector<double>& v) {
        const size_t n = v.size();
        // 5-point quadratic detrending (2 dof) ...
        std::vector<double> s2;
        for (size_t i = 2; i + 2 < n; ++i) {
            double win[5] = {v[i - 2], v[i - 1], v[i], v[i + 1], v[i + 2]};
            s2.push_back(windowSse(win) / 2.0);
        }
        if (s2.size() < 3) return 0.0;
        std::sort(s2.begin(), s2.end());
        double s5 = s2[s2.size() / 2];
        // ... combined with a 3-point second-difference MAD estimate (curvature
        // leakage is >= 0 in both, so the smaller of the two is the least
        // biased estimate of the pure noise floor)
        std::vector<double> d;
        for (size_t i = 1; i + 1 < n; ++i)
            d.push_back(v[i - 1] - 2.0 * v[i] + v[i + 1]);
        std::vector<double> ds = d;
        std::sort(ds.begin(), ds.end());
        double med = ds[ds.size() / 2];
        std::vector<double> ad;
        ad.reserve(d.size());
        for (double x : d) ad.push_back(std::fabs(x - med));
        std::sort(ad.begin(), ad.end());
        double s3 = ad[ad.size() / 2] * 1.4826 / std::sqrt(6.0);
        s3 *= s3;
        double s2min = std::min(s5, s3);
        if (!(s2min > 0.0)) return 0.0;  // machine-exact clean data
        return std::sqrt(s2min);
    };
    double sLn = robustSigma(y);
    double sPhi = robustSigma(ph);
    double s = std::sqrt(sLn * sLn + sPhi * sPhi);
    if (!std::isfinite(s)) return -1.0;
    return std::min(std::max(s, 1e-9), 0.5);
}

namespace {
// Champion rule is REGIME-GATED by rho = wrmse_best / sigmaRelData (R2;
// thresholds swept in R9):
//   * rho <= kRhoGate — residuals sit at the noise level, the best fit already
//     explains the data; model selection stays statistical (the pre-R2
//     discrepancy-principle champion, i.e. R1 behaviour).
//   * rho >  kRhoGate — residuals dwarf the noise floor: unmodelled
//     systematics are present (real-world fixtures etc.).  Under systematics
//     EVERY candidate is missing something and raw fit quality keeps
//     promoting bigger models (data1..4: 4-device mimics beat the 2-device
//     truth in AICc by a wide margin).  Here the champion is the
//     fewest-parameter candidate whose wRMSE is within the adequacy band
//     max(kNoiseMult*sigma, kRelMult*wrmse_best).
constexpr double kNoiseMult = 1.7;
constexpr double kRelMult = 2.0;
constexpr double kRhoGate = 1.4;
}  // namespace

std::vector<EquivalenceClass> rankAndClusterEquivalent(std::vector<Candidate> candidates,
                                                       const std::vector<double>& f,
                                                       double equivTol, int nObsIn,
                                                       double sigmaRelData) {
    std::vector<Candidate> valid;
    for (auto& c : candidates) {
        if (!c.skipped && std::isfinite(c.aiccVal)) valid.push_back(std::move(c));
    }
    if (valid.empty()) return {};

    int nObs = nObsIn > 0 ? nObsIn : 2 * 60;  // fallback; callers pass 2M

    double wrmseBest = std::numeric_limits<double>::infinity();
    for (const auto& c : valid) wrmseBest = std::min(wrmseBest, c.wrmse);
    const bool sysRegime =
        sigmaRelData > 0.0 && wrmseBest > kRhoGate * sigmaRelData;

    // ---- champion selection (R2, regime-gated) -------------------------------
    // parsimony within the adequacy band (systematics regime only)
    auto championFromAdequate = [&]() -> int {
        double threshold = std::max(kNoiseMult * sigmaRelData, kRelMult * wrmseBest);
        int champ = -1;
        for (int i = 0; i < (int)valid.size(); ++i) {
            const auto& c = valid[i];
            if (c.wrmse > threshold) continue;
            if (champ < 0 || c.nParams() < valid[champ].nParams() ||
                (c.nParams() == valid[champ].nParams() && c.aiccVal < valid[champ].aiccVal))
                champ = i;
        }
        return champ;
    };
    // noise regime rule: noise floor from the best per-dof RSS, discrepancy
    // margin, fewest parameters among the consistent set
    auto championFromRss = [&]() -> int {
        double sigma2Hat = std::numeric_limits<double>::infinity();
        for (const auto& c : valid) {
            double r = c.rss / std::max((double)(nObs - c.nParams()), 1.0);
            sigma2Hat = std::min(sigma2Hat, r);
        }
        double margin = 3.0 * std::sqrt(2.0 / std::max(nObs, 1));
        int minP = std::numeric_limits<int>::max();
        double bestA = std::numeric_limits<double>::infinity();
        int champ = -1;
        for (int i = 0; i < (int)valid.size(); ++i) {
            const auto& c = valid[i];
            double r = c.rss / std::max((double)(nObs - c.nParams()), 1.0);
            if (r > sigma2Hat * (1.0 + margin)) continue;
            if (c.nParams() < minP || (c.nParams() == minP && c.aiccVal < bestA)) {
                minP = c.nParams();
                bestA = c.aiccVal;
                champ = i;
            }
        }
        return champ;
    };
    int championIdx = sysRegime ? championFromAdequate() : championFromRss();
    if (championIdx < 0) championIdx = championFromRss();
    bool hasChampion = championIdx >= 0;

    // ---- ordering: champion, adequate-set members by (nParams, aicc), rest by aicc
    double adequateThreshold =
        sysRegime ? std::max(kNoiseMult * sigmaRelData, kRelMult * wrmseBest)
                  : std::numeric_limits<double>::infinity();
    std::vector<int> adequate, rest;
    for (int i = 0; i < (int)valid.size(); ++i) {
        if (hasChampion && i == championIdx) continue;
        if (valid[i].wrmse <= adequateThreshold) adequate.push_back(i);
        else rest.push_back(i);
    }
    auto byParamsAicc = [&](int a, int b) {
        if (valid[a].nParams() != valid[b].nParams())
            return valid[a].nParams() < valid[b].nParams();
        return valid[a].aiccVal < valid[b].aiccVal;
    };
    std::stable_sort(adequate.begin(), adequate.end(), byParamsAicc);
    std::stable_sort(rest.begin(), rest.end(), [&](int a, int b) {
        return valid[a].aiccVal < valid[b].aiccVal;
    });
    std::vector<Candidate> ordered;
    if (hasChampion) ordered.push_back(std::move(valid[championIdx]));
    for (int i : adequate) ordered.push_back(std::move(valid[i]));
    for (int i : rest) ordered.push_back(std::move(valid[i]));

    // noise-aware equivalence tolerance (two independent noise-floor fits of
    // the same circuit must not split into two classes)
    double effTol = equivTol;
    if (sigmaRelData > 0.0) {
        effTol = std::max(effTol, 3.0 * sigmaRelData);
    } else {
        double sigma2Hat = std::numeric_limits<double>::infinity();
        for (const auto& c : ordered) {
            double r = c.rss / std::max((double)(nObs - c.nParams()), 1.0);
            sigma2Hat = std::min(sigma2Hat, r);
        }
        effTol = std::max(effTol, 3.0 * std::sqrt(sigma2Hat));
    }

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
