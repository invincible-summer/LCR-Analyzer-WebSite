#include "selector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>

namespace ng {

namespace {
// smallest DCR the refinement may reach (below this an inductor is ideal for
// every practical purpose)
constexpr double kDcrRefineMin = 1e-5;
constexpr double kDcrRefineOpen = 100.0;  // discovery ceiling for dcr == 0

// small dense real helper: solve (A) x = b via LU with partial pivoting
bool luSolveReal(std::vector<double>& A, int n, std::vector<double>& b) {
    for (int col = 0; col < n; ++col) {
        int piv = col;
        double best = std::fabs(A[(size_t)col * n + col]);
        for (int r = col + 1; r < n; ++r) {
            double a = std::fabs(A[(size_t)r * n + col]);
            if (a > best) {
                best = a;
                piv = r;
            }
        }
        if (!(best > 0.0) || !std::isfinite(best)) return false;
        if (piv != col) {
            for (int j = 0; j < n; ++j)
                std::swap(A[(size_t)piv * n + j], A[(size_t)col * n + j]);
            std::swap(b[piv], b[col]);
        }
        double d = A[(size_t)col * n + col];
        for (int r = col + 1; r < n; ++r) {
            double f = A[(size_t)r * n + col] / d;
            if (f == 0.0) continue;
            A[(size_t)r * n + col] = f;
            for (int j = col + 1; j < n; ++j)
                A[(size_t)r * n + j] -= f * A[(size_t)col * n + j];
            b[r] -= f * b[col];
        }
    }
    for (int i = n - 1; i >= 0; --i) {
        double s = b[i];
        for (int j = i + 1; j < n; ++j) s -= A[(size_t)i * n + j] * b[j];
        b[i] = s / A[(size_t)i * n + i];
    }
    return true;
}

// parameter layout: per component one entry (R/C value) or two (L: value, dcr)
struct ParamMap {
    std::vector<int> compIdx;
    std::vector<bool> isDcr;
};

ParamMap buildParamMap(const std::vector<Component>& comps) {
    ParamMap pm;
    for (int i = 0; i < (int)comps.size(); ++i) {
        pm.compIdx.push_back(i);
        pm.isDcr.push_back(false);
        if (comps[i].kind == 'L') {
            pm.compIdx.push_back(i);
            pm.isDcr.push_back(true);
        }
    }
    return pm;
}

}  // namespace

std::vector<Candidate> refineTopCandidates(std::vector<Candidate> candidates,
                                           const ComponentSet& compset,
                                           const std::vector<Complex>& s,
                                           const std::vector<Complex>& z,
                                           const std::vector<double>& w,
                                           int topStructures, double boundsDec) {
    if (candidates.empty() || topStructures < 1) return candidates;
    const int p_total = compset.nParams();
    if (p_total < 1) return candidates;

    // pick up to three best candidates per distinct structure, topStructures
    // structures, preserving the RSS order of the input
    std::vector<int> selected;
    std::map<std::string, int> perStructure;
    for (int i = 0; i < (int)candidates.size() &&
                    (int)perStructure.size() < topStructures;
         ++i) {
        int& cnt = perStructure[candidates[i].network.structure.key()];
        if (cnt >= 3) continue;
        ++cnt;
        selected.push_back(i);
    }

    const std::vector<Component>& nominal = compset.components();
    const ParamMap pm = buildParamMap(nominal);
    const int p = (int)pm.compIdx.size();

    // R4b: refinements are appended as CLONES — the original candidates stay
    // in the list.  On machine-exact data the original (exact nominal values)
    // keeps winning the RSS order, preserving the exact-recovery guarantee;
    // on real data the refined clone wins by lower RSS.
    //
    // R5b significance guard: a refined clone that beats the best untouched
    // candidate by LESS than the chi-square fluctuation band (1/sqrt(2m)) is
    // statistically tied with it; it is then placed directly BEHIND the best
    // untouched candidate instead of ahead of it (measured on dut4_lpar in
    // the e2e suite: a mimic wiring "won" by 6% rss, well inside the ~16%
    // band at 2m=80 — the truth wiring keeps the top spot).  Clones that
    // genuinely improve beyond the band keep their earned position.
    const double kBand = 1.0 + 1.0 / std::sqrt(2.0 * (double)z.size());
    double bestUnrefinedRss = std::numeric_limits<double>::infinity();
    for (const auto& c : candidates)
        if (!c.refined) bestUnrefinedRss = std::min(bestUnrefinedRss, c.rss);
    std::vector<Candidate> refinedClones;
    for (int ci : selected) {
        Candidate cand = candidates[ci];  // copy: the original stays untouched
        double rssStart = cand.rss;
        const Structure structure = cand.network.structure;
        const std::vector<int> assign = cand.network.assign;

        // parameter bounds in log10 space; a second refinement pass starts
        // from the first-pass values but the window stays anchored to the
        // nominal user input
        const std::vector<Component>& startComps =
            cand.comps.empty() ? nominal : cand.comps;
        std::vector<double> x0(p), lo(p), hi(p);
        for (int j = 0; j < p; ++j) {
            const Component& c0 = startComps[pm.compIdx[j]];
            const Component& cn = nominal[pm.compIdx[j]];
            if (pm.isDcr[j]) {
                double d0 = c0.dcr > 0 ? c0.dcr : kDcrRefineMin;
                x0[j] = std::log10(d0);
                if (cn.dcr > 0) {
                    double c = std::log10(cn.dcr);
                    lo[j] = std::max(c - boundsDec, std::log10(kDcrRefineMin));
                    hi[j] = c + boundsDec;
                } else {
                    lo[j] = std::log10(kDcrRefineMin);
                    hi[j] = std::log10(kDcrRefineOpen);
                }
            } else {
                x0[j] = std::log10(c0.value);
                double c = std::log10(cn.value);
                lo[j] = c - boundsDec;
                hi[j] = c + boundsDec;
            }
        }
        for (int j = 0; j < p; ++j)
            x0[j] = std::min(std::max(x0[j], lo[j]), hi[j]);

        StructureStamps stamps = StructureStamps::build(structure, nominal);
        std::vector<double> vals = stamps.vals, dcrs = stamps.dcrs;
        auto residual = [&](const std::vector<double>& x, std::vector<double>& out) {
            for (int j = 0; j < p; ++j) {
                double v = std::pow(10.0, x[j]);
                if (pm.isDcr[j]) dcrs[pm.compIdx[j]] = v;
                else vals[pm.compIdx[j]] = v;
            }
            stamps.vals = vals;
            stamps.dcrs = dcrs;
            std::vector<std::vector<Complex>> zf = stamps.zFull({assign}, s);
            out = residualVector(z, zf[0], w);
        };

        // bounded damped least squares with a forward-difference Jacobian
        std::vector<double> x = x0, r;
        residual(x, r);
        double rss = rssOf(r);
        double lambda = 1e-3;
        const int maxIter = 40;
        for (int iter = 0; iter < maxIter; ++iter) {
            // Jacobian (2m x p)
            std::vector<double> J((size_t)2 * z.size() * p);
            for (int j = 0; j < p; ++j) {
                std::vector<double> xr = x, rj;
                xr[j] = std::min(xr[j] + 1e-6, hi[j]);
                if (xr[j] == x[j]) xr[j] = std::max(x[j] - 1e-6, lo[j]);
                residual(xr, rj);
                double h = xr[j] - x[j];
                for (size_t k = 0; k < r.size(); ++k)
                    J[k * (size_t)p + j] = (rj[k] - r[k]) / h;
            }
            // normal equations
            std::vector<double> H((size_t)p * p, 0.0), g(p, 0.0);
            for (size_t k = 0; k < r.size(); ++k) {
                for (int a = 0; a < p; ++a) {
                    g[a] += J[k * (size_t)p + a] * r[k];
                    for (int b = a; b < p; ++b)
                        H[(size_t)a * p + b] +=
                            J[k * (size_t)p + a] * J[k * (size_t)p + b];
                }
            }
            for (int a = 0; a < p; ++a)
                for (int b = 0; b < a; ++b) H[(size_t)a * p + b] = H[(size_t)b * p + a];
            bool stepped = false;
            for (int attempt = 0; attempt < 8 && !stepped; ++attempt) {
                std::vector<double> Hs = H, dx = g, xTry(p), rTry;
                for (int a = 0; a < p; ++a) {
                    Hs[(size_t)a * p + a] += lambda * std::max(Hs[(size_t)a * p + a], 1e-12);
                    dx[a] = -dx[a];
                }
                if (!luSolveReal(Hs, p, dx)) {
                    lambda *= 8.0;
                    continue;
                }
                bool finite = true;
                for (int a = 0; a < p; ++a) {
                    if (!std::isfinite(dx[a])) finite = false;
                    xTry[a] = std::min(std::max(x[a] + dx[a], lo[a]), hi[a]);
                }
                if (!finite) {
                    lambda *= 8.0;
                    continue;
                }
                residual(xTry, rTry);
                double rssTry = rssOf(rTry);
                if (std::isfinite(rssTry) && rssTry < rss) {
                    double improve = rss - rssTry;
                    x = xTry;
                    r = rTry;
                    rss = rssTry;
                    lambda = std::max(lambda / 3.0, 1e-12);
                    stepped = true;
                    if (improve <= 1e-10 * std::max(rss, 1e-300)) iter = maxIter;
                } else {
                    lambda *= 8.0;
                }
            }
            if (!stepped) break;
        }

        // R7: outlier-robust second round (single IRLS pass, same rationale as
        // the Try3 stage E).  When a couple of wild points dominate the plain
        // least-squares landscape, the value-space valley flattens and the LM
        // wanders arbitrarily far (measured: +-0.9 decades at unchanged
        // wRMSE), producing clones whose behaviour diverges from the honest
        // fit.  Downweighting the outlier points and re-polishing lets the
        // parameters settle into the inlier-defined minimum.
        {
            stamps.vals = vals;
            stamps.dcrs = dcrs;
            std::vector<Complex> zf = stamps.zFull({assign}, s)[0];
            const size_t Mz = z.size();
            std::vector<double> magRe(Mz), magIm(Mz), mag(Mz);
            for (size_t k = 0; k < Mz; ++k) {
                Complex rr = (z[k] - zf[k]) / z[k];
                magRe[k] = std::fabs(rr.real());
                magIm[k] = std::fabs(rr.imag());
                mag[k] = std::abs(rr);
            }
            auto medOf = [](std::vector<double> v) {
                std::sort(v.begin(), v.end());
                return v[v.size() / 2];
            };
            double sigAxis =
                std::max(1.4826 * std::max(medOf(magRe), medOf(magIm)), 1e-9);
            const double kCut = 5.0, kIn = 2.5;
            std::vector<double> wR = w;
            bool anyOut = false;
            int nInlier = 0, nOut = 0;
            for (size_t k = 0; k < Mz; ++k) {
                double m = mag[k] / sigAxis;
                if (m > kCut) {
                    double f = kCut / m;
                    wR[k] *= f * f;
                    anyOut = true;
                    ++nOut;
                } else if (m < kIn) {
                    ++nInlier;
                }
            }
            if (anyOut && nOut <= (int)Mz / 3 && nInlier * 2 >= (int)Mz) {
                auto residualR = [&](const std::vector<double>& xx,
                                     std::vector<double>& out) {
                    for (int j = 0; j < p; ++j) {
                        double v = std::pow(10.0, xx[j]);
                        if (pm.isDcr[j]) dcrs[pm.compIdx[j]] = v;
                        else vals[pm.compIdx[j]] = v;
                    }
                    stamps.vals = vals;
                    stamps.dcrs = dcrs;
                    std::vector<std::vector<Complex>> z2 = stamps.zFull({assign}, s);
                    out = residualVector(z, z2[0], wR);
                };
                double rssInc;
                {
                    std::vector<double> rInc;
                    residualR(x, rInc);
                    rssInc = rssOf(rInc);
                }
                // short damped LM under the robust weights
                std::vector<double> xr = x, rr2;
                residualR(xr, rr2);
                double rssR = rssOf(rr2);
                double lam = 1e-3;
                for (int iter = 0; iter < 25; ++iter) {
                    std::vector<double> J((size_t)2 * Mz * p);
                    for (int j = 0; j < p; ++j) {
                        std::vector<double> xh = xr, rj;
                        xh[j] = std::min(xh[j] + 1e-6, hi[j]);
                        if (xh[j] == xr[j]) xh[j] = std::max(xr[j] - 1e-6, lo[j]);
                        residualR(xh, rj);
                        double h = xh[j] - xr[j];
                        for (size_t k = 0; k < rr2.size(); ++k)
                            J[k * (size_t)p + j] = (rj[k] - rr2[k]) / h;
                    }
                    std::vector<double> H((size_t)p * p, 0.0), g2(p, 0.0);
                    for (size_t k = 0; k < rr2.size(); ++k) {
                        for (int a = 0; a < p; ++a) {
                            g2[a] += J[k * (size_t)p + a] * rr2[k];
                            for (int b = a; b < p; ++b)
                                H[(size_t)a * p + b] +=
                                    J[k * (size_t)p + a] * J[k * (size_t)p + b];
                        }
                    }
                    for (int a = 0; a < p; ++a)
                        for (int b = 0; b < a; ++b)
                            H[(size_t)a * p + b] = H[(size_t)b * p + a];
                    bool stepped = false;
                    for (int attempt = 0; attempt < 6 && !stepped; ++attempt) {
                        std::vector<double> Hs = H, dx = g2, xTry(p), rTry;
                        for (int a = 0; a < p; ++a) {
                            Hs[(size_t)a * p + a] +=
                                lam * std::max(Hs[(size_t)a * p + a], 1e-12);
                            dx[a] = -dx[a];
                        }
                        if (!luSolveReal(Hs, p, dx)) {
                            lam *= 8.0;
                            continue;
                        }
                        bool finite = true;
                        for (int a = 0; a < p; ++a) {
                            if (!std::isfinite(dx[a])) finite = false;
                            xTry[a] = std::min(std::max(xr[a] + dx[a], lo[a]), hi[a]);
                        }
                        if (!finite) {
                            lam *= 8.0;
                            continue;
                        }
                        residualR(xTry, rTry);
                        double rssTry = rssOf(rTry);
                        if (std::isfinite(rssTry) && rssTry < rssR) {
                            xr = xTry;
                            rr2 = rTry;
                            rssR = rssTry;
                            lam = std::max(lam / 3.0, 1e-12);
                            stepped = true;
                        } else {
                            lam *= 8.0;
                        }
                    }
                    if (!stepped) break;
                }
                if (rssR < rssInc * 0.999) x = xr;  // adopt robust parameters
            }
        }

        // materialize refined values on the candidate — skip when the LM
        // never moved: with ideal inductors (dcr == 0) even the log-space
        // floor at kDcrRefineMin would fabricate a fake DCR, and a no-op
        // refinement must not shadow the exact nominal candidate
        std::vector<Component> comps = nominal;
        double maxMove = 0.0;
        for (int j = 0; j < p; ++j) {
            double v1 = std::pow(10.0, x[j]);
            double vMove = pm.isDcr[j]
                               ? std::max(startComps[pm.compIdx[j]].dcr, 0.0)
                               : startComps[pm.compIdx[j]].value;
            maxMove = std::max(maxMove, std::fabs(v1 - vMove) / std::max(vMove, 1e-300));
        }
        if (maxMove <= 1e-9) continue;
        // R5b self-significance: if the refinement did not improve its OWN
        // candidate beyond the chi-square fluctuation band, the clone carries
        // no information — skip it (this also removes flat-valley wanderers
        // whose values move wildly while wRMSE stays put)
        if (!(rss < rssStart / kBand)) continue;
        for (int j = 0; j < p; ++j) {
            double v = std::pow(10.0, x[j]);
            if (pm.isDcr[j]) comps[pm.compIdx[j]].dcr = v;
            else comps[pm.compIdx[j]].value = v;
        }
        stamps.vals.clear();
        stamps.dcrs.clear();
        for (const auto& c : comps) {
            stamps.vals.push_back(c.value);
            stamps.dcrs.push_back(c.dcr);
        }
        std::vector<std::vector<Complex>> zf = stamps.zFull({assign}, s);
        auto [wrmse, mre] = fitMetrics(z, zf[0]);
        cand.zFit = std::move(zf[0]);
        cand.rss = rssOf(residualVector(z, cand.zFit, w));  // honest plain-w rss
        cand.wrmse = wrmse;
        cand.maxRelErr = mre;
        cand.aiccVal = aicc(rss, 2 * (int)z.size(), p_total);
        cand.comps = std::move(comps);
        cand.refined = true;
        // statistically tied with the best untouched candidate -> rank just
        // behind it (order-preserving within the tied group so tied clones do
        // not pile up and sink the truth deep into the list); raw fit quality
        // stays visible via wrmse / maxRelErr
        if (cand.rss < bestUnrefinedRss && cand.rss > bestUnrefinedRss / kBand)
            cand.rss = bestUnrefinedRss + cand.rss * ((kBand - 1.0) / 4.0);
        refinedClones.push_back(std::move(cand));
    }

    candidates.insert(candidates.end(),
                      std::make_move_iterator(refinedClones.begin()),
                      std::make_move_iterator(refinedClones.end()));
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.rss < b.rss; });
    return candidates;
}

std::vector<Candidate> evaluateCandidates(const std::vector<Network>& nets,
                                          const ComponentSet& compset,
                                          const std::vector<Complex>& s,
                                          const std::vector<Complex>& z,
                                          const std::vector<double>& w,
                                          int batchSize) {
    const int nObs = 2 * (int)z.size();
    const int p = compset.nParams();
    std::vector<Candidate> out;
    // group by structure, preserving first-appearance order (dict semantics)
    std::vector<std::pair<std::string, std::pair<Structure, std::vector<std::vector<int>>>>>
        byStructure;
    std::map<std::string, int> indexOf;
    for (const auto& net : nets) {
        std::string key = net.structure.key();
        auto it = indexOf.find(key);
        if (it == indexOf.end()) {
            indexOf[key] = (int)byStructure.size();
            byStructure.push_back({key, {net.structure, {net.assign}}});
        } else {
            byStructure[it->second].second.second.push_back(net.assign);
        }
    }
    for (auto& [key, entry] : byStructure) {
        const Structure& structure = entry.first;
        StructureStamps stamps = StructureStamps::build(structure, compset);
        bool spFlag = isSeriesParallel(structure.V, structure.mult);
        for (size_t i0 = 0; i0 < entry.second.size(); i0 += (size_t)batchSize) {
            size_t i1 = std::min(entry.second.size(), i0 + (size_t)batchSize);
            std::vector<std::vector<int>> chunk(entry.second.begin() + i0,
                                                entry.second.begin() + i1);
            std::vector<std::vector<Complex>> zModel = stamps.zFull(chunk, s);
            for (size_t row = 0; row < chunk.size(); ++row) {
                const std::vector<Complex>& zf = zModel[row];
                bool finite = true;
                for (const Complex& v : zf)
                    if (!std::isfinite(v.real()) || !std::isfinite(v.imag())) {
                        finite = false;
                        break;
                    }
                if (!finite) continue;
                std::vector<double> res = residualVector(z, zf, w);
                double rss = rssOf(res);
                auto [wrmse, mre] = fitMetrics(z, zf);
                Candidate c;
                c.network = Network{structure, chunk[row]};
                c.zFit = zf;
                c.rss = rss;
                c.aiccVal = aicc(rss, nObs, p);
                c.wrmse = wrmse;
                c.maxRelErr = mre;
                c.sp = spFlag;
                out.push_back(std::move(c));
            }
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Candidate& a, const Candidate& b) { return a.rss < b.rss; });
    return out;
}

std::vector<double> makeValidationGrid(const std::vector<double>& f, int n,
                                       double expand) {
    double fmin = *std::min_element(f.begin(), f.end());
    double fmax = *std::max_element(f.begin(), f.end());
    // numpy.logspace over [log10(fmin/expand), log10(fmax*expand)]
    double la = std::log10(fmin / expand), lb = std::log10(fmax * expand);
    std::vector<double> out((size_t)n);
    for (int i = 0; i < n; ++i)
        out[i] = std::pow(10.0, la + (lb - la) * (double)i / (double)(n - 1));
    return out;
}

bool relDiffBelow(const std::vector<Complex>& za, const std::vector<Complex>& zb,
                  double tol) {
    double mx = -1.0;
    bool any = false;
    for (size_t k = 0; k < za.size(); ++k) {
        double denom = std::abs(za[k]);
        if (denom < 1e-300) denom = std::abs(zb[k]);
        double rel = std::abs(za[k] - zb[k]) / denom;
        if (!std::isfinite(rel)) continue;  // np.isfinite filter
        any = true;
        mx = std::max(mx, rel);
    }
    if (!any) return false;
    return mx < tol;
}

bool areEquivalent(const Network& a, const Network& b, const ComponentSet& compset,
                   const std::vector<double>& grid, double tol) {
    std::vector<Complex> za = networkZ(a, compset, grid);
    std::vector<Complex> zb = networkZ(b, compset, grid);
    return relDiffBelow(za, zb, tol);
}

namespace {

// per-candidate component vector (refined values when present)
inline const std::vector<Component>& compsOf(const Candidate& c,
                                             const ComponentSet& compset) {
    return c.comps.empty() ? compset.components() : c.comps;
}

// str(network.serialize(comp_keys)) as the Python reference builds it --
// repr of the nested tuple (slot)(key)('C', value, dcr).  Reproduced here so
// the within-class representative choice matches the reference bit-for-bit
// (Python compares these strings, not the tuples).
std::string pySerialStr(const std::vector<std::vector<Component>>& serial) {
    auto keyStr = [](const Component& c) {
        return "('" + std::string(1, c.kind) + "', " + pyRepr(c.value) + ", " +
               pyRepr(c.dcr) + ")";
    };
    std::string out = "(";
    for (size_t k = 0; k < serial.size(); ++k) {
        if (k) out += ", ";
        out += "(";
        for (size_t q = 0; q < serial[k].size(); ++q) {
            if (q) out += ", ";
            out += keyStr(serial[k][q]);
        }
        if (serial[k].size() == 1) out += ",";
        out += ")";
    }
    if (serial.size() == 1) out += ",";
    out += ")";
    return out;
}

// (fewer internal junctions, series-parallel first, nominal values before
// refined clones, canonical wiring key)
struct SecondaryKey {
    int nInternal;
    int spPenalty;
    int refinedPenalty;
    std::string serialStr;
    bool operator<(const SecondaryKey& o) const {
        if (nInternal != o.nInternal) return nInternal < o.nInternal;
        if (spPenalty != o.spPenalty) return spPenalty < o.spPenalty;
        if (refinedPenalty != o.refinedPenalty) return refinedPenalty < o.refinedPenalty;
        return serialStr < o.serialStr;
    }
};

SecondaryKey secondaryKey(const Candidate& c, const std::vector<Component>& comps) {
    return SecondaryKey{c.nInternal(), c.sp ? 0 : 1, c.refined ? 1 : 0,
                        pySerialStr(c.network.serialize(comps))};
}

}  // namespace

std::vector<EquivalenceClass> rankAndCluster(std::vector<Candidate> candidates,
                                             const ComponentSet& compset,
                                             const std::vector<double>& f,
                                             int clusterTop, double equivTol) {
    if (candidates.empty()) return {};
    double sigmaHat = candidates[0].wrmse;  // candidates are RSS-sorted; py takes
    for (const auto& c : candidates)        // min(wrmse) -- same set, same min
        sigmaHat = std::min(sigmaHat, c.wrmse);
    double tol = std::max(equivTol, 3.0 * sigmaHat);
    std::vector<double> grid = makeValidationGrid(f);
    size_t topN = std::min((size_t)clusterTop, candidates.size());
    std::vector<std::vector<Complex>> zGrids(topN);
    for (size_t i = 0; i < topN; ++i)
        zGrids[i] = networkZValues(candidates[i].network, compsOf(candidates[i], compset), grid);

    std::vector<EquivalenceClass> classes;
    std::vector<std::vector<Complex>> classZ;
    for (size_t i = 0; i < topN; ++i) {
        bool matched = false;
        for (size_t ci = 0; ci < classes.size(); ++ci) {
            if (relDiffBelow(classZ[ci], zGrids[i], tol)) {
                classes[ci].members.push_back(std::move(candidates[i]));
                matched = true;
                break;
            }
        }
        if (!matched) {
            classes.push_back(EquivalenceClass{std::move(candidates[i]), {}});
            classZ.push_back(zGrids[i]);
        }
    }
    for (auto& cl : classes) {
        std::vector<Candidate> everyone;
        everyone.push_back(std::move(cl.representative));
        for (auto& m : cl.members) everyone.push_back(std::move(m));
        std::stable_sort(everyone.begin(), everyone.end(),
                         [&](const Candidate& a, const Candidate& b) {
                             return secondaryKey(a, compsOf(a, compset)) <
                                    secondaryKey(b, compsOf(b, compset));
                         });
        cl.representative = std::move(everyone[0]);
        cl.members.clear();
        for (size_t i = 1; i < everyone.size(); ++i)
            cl.members.push_back(std::move(everyone[i]));
    }
    std::stable_sort(classes.begin(), classes.end(),
                     [](const EquivalenceClass& a, const EquivalenceClass& b) {
                         return a.rss() < b.rss();
                     });
    return classes;
}

}  // namespace ng
