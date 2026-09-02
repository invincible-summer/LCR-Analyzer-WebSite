#include "pruning.hpp"

#include <algorithm>
#include <cmath>

namespace rlc {

AsymptoticFeatures extractAsymptotics(const std::vector<double>& w,
                                      const std::vector<Complex>& z) {
    const size_t m = w.size();
    std::vector<double> mag(m), lw(m), lmag(m), phaseDeg(m);
    for (size_t k = 0; k < m; ++k) {
        mag[k] = std::abs(z[k]);
        lw[k] = std::log10(w[k]);
        lmag[k] = std::log10(std::max(mag[k], 1e-300));
        phaseDeg[k] = std::atan2(z[k].imag(), z[k].real()) * 180.0 / M_PI;
    }

    int k = (int)std::min(4.0, (double)std::max(2, (int)(m / 5)));
    AsymptoticFeatures feat;
    feat.slopeLow = polyfitSlope(lw.data(), lmag.data(), k);
    feat.slopeHigh = polyfitSlope(lw.data() + (m - k), lmag.data() + (m - k), k);

    feat.rLevel = median(mag);
    double wMin = w.front(), wMax = w.back();

    // L estimate (H): from whichever band end looks inductive (|Z| ~ w L)
    auto meanLog = [](const std::vector<double>& v) {
        double s = 0.0;
        for (double x : v) s += std::log(x);
        return std::exp(s / (double)v.size());
    };
    if (feat.slopeLow > 0.5 || phaseDeg[0] > 60.0) {
        std::vector<double> q(k);
        for (int i = 0; i < k; ++i) q[i] = mag[i] / w[i];
        feat.lEst = meanLog(q);
    } else if (feat.slopeHigh > 0.5 || phaseDeg[m - 1] > 60.0) {
        std::vector<double> q(k);
        for (int i = 0; i < k; ++i) q[i] = mag[m - k + i] / w[m - k + i];
        feat.lEst = meanLog(q);
    } else {
        feat.lEst = feat.rLevel / wMax;
    }

    // C estimate (F): from whichever band end looks capacitive (|Z| ~ 1/(wC))
    if (feat.slopeHigh < -0.5 || phaseDeg[m - 1] < -60.0) {
        std::vector<double> q(k);
        for (int i = 0; i < k; ++i) q[i] = 1.0 / (w[m - k + i] * mag[m - k + i]);
        feat.cEst = meanLog(q);
    } else if (feat.slopeLow < -0.5 || phaseDeg[0] < -60.0) {
        std::vector<double> q(k);
        for (int i = 0; i < k; ++i) q[i] = 1.0 / (w[i] * mag[i]);
        feat.cEst = meanLog(q);
    } else {
        feat.cEst = 1.0 / (wMax * feat.rLevel);
    }

    // interior resonance / anti-resonance peak/dip in |Z|
    feat.hasWRes = false;
    if (m >= 7) {
        // mid_mag = mag[2:-1]; allow the penultimate point to be the peak
        size_t midLen = m - 3;
        size_t imaxRel = 0, iminRel = 0;
        for (size_t i = 0; i < midLen; ++i) {
            if (mag[2 + i] > mag[2 + imaxRel]) imaxRel = i;
            if (mag[2 + i] < mag[2 + iminRel]) iminRel = i;
        }
        size_t imax = 2 + imaxRel, imin = 2 + iminRel;
        if (mag[imax] > 1.5 * mag[0] && mag[imax] > 1.5 * mag[m - 1]) {
            feat.wRes = w[imax];
            feat.hasWRes = true;
        } else if (mag[imin] < 0.67 * mag[0] && mag[imin] < 0.67 * mag[m - 1]) {
            feat.wRes = w[imin];
            feat.hasWRes = true;
        }
    }
    if (!feat.hasWRes) {
        // pure +1 then -1 slope (or reverse) at the band ends: the wL and 1/(wC)
        // asymptotes cross at the resonance, w0 = 1/sqrt(L C)
        bool signs = (feat.slopeLow > 0.5 && feat.slopeHigh < -0.5) ||
                     (feat.slopeLow < -0.5 && feat.slopeHigh > 0.5);
        if (signs && feat.lEst > 0 && feat.cEst > 0) {
            double w0 = 1.0 / std::sqrt(feat.lEst * feat.cEst);
            if (wMin <= w0 && w0 <= wMax) {
                feat.wRes = w0;
                feat.hasWRes = true;
            }
        }
    }

    feat.phaseLowDeg = phaseDeg[0];
    feat.phaseHighDeg = phaseDeg[m - 1];
    feat.rPeak = *std::max_element(mag.begin(), mag.end());
    feat.rFloor = *std::min_element(mag.begin(), mag.end());
    return feat;
}

StartHints hintsFromFeatures(const AsymptoticFeatures& feat) {
    StartHints h;
    h.rLevel = feat.rLevel;
    h.lEst = feat.lEst;
    h.cEst = feat.cEst;
    h.hasWRes = feat.hasWRes;
    h.wRes = feat.wRes;
    h.hasRPeak = true;
    h.rPeak = feat.rPeak;
    return h;
}

std::pair<int, int> highFreqSlopeRange(const TreePtr& tree) {
    if (tree->isLeaf) {
        if (tree->elem == 'L') return {1, 1};
        if (tree->elem == 'C') return {-1, -1};
        return {0, 0};
    }
    int lo, hi;
    if (tree->kind == NK::Ser) {
        // in series the child with the highest slope dominates at s -> inf
        lo = hi = std::numeric_limits<int>::min();
        for (const auto& c : tree->kids) {
            auto r = highFreqSlopeRange(c);
            lo = std::max(lo, r.first);
            hi = std::max(hi, r.second);
        }
    } else {
        // in parallel the child with the lowest slope dominates
        lo = hi = std::numeric_limits<int>::max();
        for (const auto& c : tree->kids) {
            auto r = highFreqSlopeRange(c);
            lo = std::min(lo, r.first);
            hi = std::min(hi, r.second);
        }
    }
    return {lo, hi};
}

bool pruneF2(const TreePtr& tree, const AsymptoticFeatures& feat) {
    auto [sMin, sMax] = highFreqSlopeRange(tree);
    // strong inductive trend at high frequency: reject purely capacitive
    // terminations
    if (feat.slopeHigh > 0.65 && feat.phaseHighDeg > 60.0) {
        if (sMax < 0) return false;
    }
    // strong capacitive trend: reject purely inductive terminations
    if (feat.slopeHigh < -0.65 && feat.phaseHighDeg < -60.0) {
        if (sMin > 0) return false;
    }
    return true;
}

bool pruneF3(const TreePtr& tree, int minEnergy) {
    auto kinds = leafKinds(tree);
    int nEnergy = 0;
    for (char k : kinds)
        if (k == 'L' || k == 'C') ++nEnergy;
    return nEnergy >= minEnergy;
}

std::vector<TreePtr> pruneTrees(const std::vector<TreePtr>& trees,
                                const AsymptoticFeatures& feat, int minEnergy,
                                bool enableF2, bool enableF3) {
    std::vector<TreePtr> out;
    for (const auto& t : trees) {
        if (enableF2 && !pruneF2(t, feat)) continue;
        if (enableF3 && !pruneF3(t, minEnergy)) continue;
        out.push_back(t);
    }
    if (out.empty()) return trees;  // fallback: never return an empty library
    return out;
}

}  // namespace rlc
