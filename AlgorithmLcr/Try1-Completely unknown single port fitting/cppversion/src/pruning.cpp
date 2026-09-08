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
    // R15 (contamination-triggered robustification): the raw features (plain
    // LSQ slope over 2-4 points, phase of the SINGLE endpoint sample) have
    // zero tolerance for one wild point at a band end — a single contaminated
    // sample moved slopeHigh by ~0.35 and phaseHighDeg by ~100 deg on repro
    // cases, planted fake resonances and poisoned every start hint BEFORE
    // any fitting happens (F2 prunes and the funnel both consume these
    // features; the R7/R14 IRLS rescue cannot recover a tree that was never
    // fitted).  The robust replacements (Theil-Sen slope, circular-median
    // phase) lose ~1/3 efficiency on clean short windows, which measurably
    // hurt resonance-rich cases — so they are switched on ONLY when they
    // disagree with the raw estimator by more than contamination can explain:
    //   slope : |LSQ - TheilSen| > 0.30 decade/decade (F2 branches fire at
    //           +-0.65, so 0.30 is safely below decision-relevant)
    //   phase : endpoint deviates from the window circular median by > 45 deg
    // On clean data both estimators agree and the features are bit-identical
    // to the pre-R15 values.
    int kw = (int)std::min((size_t)(k + 2), m);
    auto theilSen = [](const double* x, const double* y, int n) {
        std::vector<double> sl;
        sl.reserve((size_t)n * (n - 1) / 2);
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
                if (x[j] > x[i]) sl.push_back((y[j] - y[i]) / (x[j] - x[i]));
        if (sl.empty()) return 0.0;
        std::sort(sl.begin(), sl.end());
        return sl[sl.size() / 2];
    };
    auto circMedianDeg = [](const double* ph, int n) {
        double best = ph[0], bestSum = std::numeric_limits<double>::max();
        for (int i = 0; i < n; ++i) {
            double s = 0.0;
            for (int j = 0; j < n; ++j) {
                double d = ph[j] - ph[i];
                while (d > 180.0) d -= 360.0;
                while (d < -180.0) d += 360.0;
                s += std::fabs(d);
            }
            if (s < bestSum) {
                bestSum = s;
                best = ph[i];
            }
        }
        return best;
    };
    auto robustSlope = [&](const double* x, const double* y, int kRaw) {
        double lsq = polyfitSlope(x, y, kRaw);   // exact pre-R15 estimator
        double ts = theilSen(x, y, kw);
        // Contamination test, not estimator-disagreement: a curvature-heavy
        // window (resonance INSIDE the window — dut4_ind_parasitic sits at
        // the high band edge) makes any "biggest residual" test fire on
        // clean data.  The single-wild-sample signature has TWO parts:
        //   (i)  one residual dominates: max > 3x median and > 0.05 dex;
        //   (ii) REMOVING that point linearizes the rest — the residual
        //        scale of the remaining kw-1 points under a fresh robust
        //        line drops by >= 3x.  Smooth curvature fails (ii): delete
        //        the peak point and the rest is still curved.
        std::vector<double> interc;
        interc.reserve(kw);
        for (int i = 0; i < kw; ++i) interc.push_back(y[i] - ts * x[i]);
        std::sort(interc.begin(), interc.end());
        double a = interc[interc.size() / 2];
        int worst = 0;
        double worstR = -1.0;
        std::vector<double> res(kw);
        for (int i = 0; i < kw; ++i) {
            res[i] = std::fabs(y[i] - (ts * x[i] + a));
            if (res[i] > worstR) {
                worstR = res[i];
                worst = i;
            }
        }
        std::vector<double> resS = res;
        std::sort(resS.begin(), resS.end());
        double medR = resS[resS.size() / 2];
        bool contaminated = worstR > 3.0 * std::max(medR, 1e-12) && worstR > 0.05;
        if (contaminated && kw >= 4) {
            std::vector<double> x2, y2;
            for (int i = 0; i < kw; ++i)
                if (i != worst) {
                    x2.push_back(x[i]);
                    y2.push_back(y[i]);
                }
            double ts2 = theilSen(x2.data(), y2.data(), kw - 1);
            std::vector<double> interc2;
            for (int i = 0; i < kw - 1; ++i)
                interc2.push_back(y2[i] - ts2 * x2[i]);
            std::sort(interc2.begin(), interc2.end());
            double a2 = interc2[interc2.size() / 2];
            std::vector<double> res2(kw - 1);
            for (int i = 0; i < kw - 1; ++i)
                res2[i] = std::fabs(y2[i] - (ts2 * x2[i] + a2));
            std::sort(res2.begin(), res2.end());
            double medR2 = res2[res2.size() / 2];
            contaminated = medR2 < medR / 3.0;
        }
        return contaminated ? ts : lsq;
    };
    auto robustEndPhase = [&](const double* ph, int idx) {
        // R15 final: the endpoint phase stays RAW.  A circular-median
        // replacement was tried and reverted: when a genuine resonance
        // crosses the band edge (dut4_ind_parasitic, self-resonance between
        // the last two samples), the endpoint phase sits on the far side of
        // a tight cluster exactly like an outlier does — no local test
        // separates the two, and F2 consumes the phase as "which side of
        // the resonance does the band END on", where the endpoint sample is
        // the most authoritative one.  Endpoint-phase contamination is
        // bounded by F2 needing slope AND phase to agree, and the slope is
        // robustified above.
        (void)circMedianDeg;
        return ph[idx];
    };
    AsymptoticFeatures feat;
    feat.slopeLow = robustSlope(lw.data(), lmag.data(), k);
    feat.slopeHigh = robustSlope(lw.data() + (m - k), lmag.data() + (m - k), k);

    feat.rLevel = median(mag);
    double wMin = w.front(), wMax = w.back();

    // L estimate (H): from whichever band end looks inductive (|Z| ~ w L)
    auto meanLog = [](const std::vector<double>& v) {
        double s = 0.0;
        for (double x : v) s += std::log(x);
        return std::exp(s / (double)v.size());
    };
    const double phLow = robustEndPhase(phaseDeg.data(), 0);
    const double phHigh = robustEndPhase(phaseDeg.data() + (m - kw), kw - 1);
    if (feat.slopeLow > 0.5 || phLow > 60.0) {
        std::vector<double> q(k);
        for (int i = 0; i < k; ++i) q[i] = mag[i] / w[i];
        feat.lEst = meanLog(q);
    } else if (feat.slopeHigh > 0.5 || phHigh > 60.0) {
        std::vector<double> q(k);
        for (int i = 0; i < k; ++i) q[i] = mag[m - k + i] / w[m - k + i];
        feat.lEst = meanLog(q);
    } else {
        feat.lEst = feat.rLevel / wMax;
    }

    // C estimate (F): from whichever band end looks capacitive (|Z| ~ 1/(wC))
    if (feat.slopeHigh < -0.5 || phHigh < -60.0) {
        std::vector<double> q(k);
        for (int i = 0; i < k; ++i) q[i] = 1.0 / (w[m - k + i] * mag[m - k + i]);
        feat.cEst = meanLog(q);
    } else if (feat.slopeLow < -0.5 || phLow < -60.0) {
        std::vector<double> q(k);
        for (int i = 0; i < k; ++i) q[i] = 1.0 / (w[i] * mag[i]);
        feat.cEst = meanLog(q);
    } else {
        feat.cEst = 1.0 / (wMax * feat.rLevel);
    }

    // interior resonance / anti-resonance peak/dip in |Z|: hunt on the RAW
    // magnitude — a genuine resonance can be 1 point wide on a sparse grid
    // (the known sharp-resonance aliasing residual), and a 1-point peak is
    // locally indistinguishable from a 1-point outlier in |Z| alone, so any
    // spike-rejection rule trades one failure family for another (R15 tried;
    // suite2 regressed on clean tank cases).  The END LEVELS below are where
    // contamination actually does damage (an inflated end sample fabricates
    // or masks a resonance), and there the robustified level is
    // contamination-triggered only.
    feat.hasWRes = false;
    if (m >= 7) {
        size_t midLen = m - 3;
        size_t imaxRel = 0, iminRel = 0;
        for (size_t i = 0; i < midLen; ++i) {
            if (mag[2 + i] > mag[2 + imaxRel]) imaxRel = i;
            if (mag[2 + i] < mag[2 + iminRel]) iminRel = i;
        }
        size_t imax = 2 + imaxRel, imin = 2 + iminRel;
        // R15 (end levels stay RAW): a window-median end level was tried and
        // REVERTED — on a steep monotone band end (|Z| changes x3+ per grid
        // step) the median sits far from the endpoint for every clean sweep,
        // silently shifting the 1.5x/0.67x resonance thresholds.  The
        // fake-resonance-from-endpoint-outlier case this guarded is a
        // start-hint quality issue the multi-start funnel already tolerates
        // (suite2 outlier slice measured no difference).
        const double magF0 = mag[0];
        const double magFend = mag[m - 1];
        if (mag[imax] > 1.5 * magF0 && mag[imax] > 1.5 * magFend) {
            feat.wRes = w[imax];
            feat.hasWRes = true;
        } else if (mag[imin] < 0.67 * magF0 && mag[imin] < 0.67 * magFend) {
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

    feat.phaseLowDeg = phLow;
    feat.phaseHighDeg = phHigh;
    // rPeak/rFloor stay on the raw magnitude (start-hint quantities only;
    // robustifying them trades clean sharp-resonance behaviour for nothing
    // measurable — R15 tried, suite2 regressed).
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

int energyCount(const TreePtr& tree) {
    int n = 0;
    for (char k : leafKinds(tree))
        if (k == 'L' || k == 'C') ++n;
    return n;
}

std::vector<TreePtr> pruneTrees(const std::vector<TreePtr>& trees,
                                const AsymptoticFeatures& feat, int minEnergy,
                                bool enableF2, bool enableF3) {
    (void)minEnergy;
    (void)enableF3;  // R1: F3 is a scheduling key now, never destructive
    std::vector<TreePtr> out;
    for (const auto& t : trees) {
        if (enableF2 && !pruneF2(t, feat)) continue;
        out.push_back(t);
    }
    if (out.empty()) return trees;  // fallback: never return an empty library
    std::stable_sort(out.begin(), out.end(), [](const TreePtr& a, const TreePtr& b) {
        int ea = energyCount(a), eb = energyCount(b);
        if (ea != eb) return ea < eb;
        return canonical(a) < canonical(b);
    });
    return out;
}

}  // namespace rlc
