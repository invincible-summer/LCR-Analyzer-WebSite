#include "fit_engine_a.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace rlc {

std::vector<double> defaultWeights(const std::vector<Complex>& z) {
    std::vector<double> w(z.size());
    for (size_t k = 0; k < z.size(); ++k) w[k] = 1.0 / std::abs(z[k]);
    return w;
}

std::vector<double> residualVector(const TreePtr& tree, const std::vector<double>& theta,
                                   const std::vector<Complex>& s,
                                   const std::vector<Complex>& z,
                                   const std::vector<double>& w) {
    const size_t m = z.size();
    std::vector<Complex> zfit(m);
    evalTheta(tree, theta, s.data(), m, zfit.data());
    std::vector<double> out(2 * m);
    for (size_t k = 0; k < m; ++k) {
        Complex r = w[k] * (z[k] - zfit[k]);
        out[2 * k] = r.real();
        out[2 * k + 1] = r.imag();
    }
    return out;
}

std::vector<double> jacobianCs(const TreePtr& tree, const std::vector<double>& theta,
                               const std::vector<Complex>& s,
                               const std::vector<double>& w) {
    const size_t m = s.size();
    const size_t p = theta.size();
    std::vector<Complex> z(m);
    std::vector<Complex> J(p * m);
    evalJac(tree, theta, s.data(), m, z.data(), J.data());
    std::vector<double> out(2 * m * p);
    for (size_t k = 0; k < m; ++k) {
        for (size_t i = 0; i < p; ++i) {
            Complex dr = -(double)w[k] * J[i * m + k];
            out[(2 * k) * p + i] = dr.real();
            out[(2 * k + 1) * p + i] = dr.imag();
        }
    }
    return out;
}

double rssOf(const std::vector<double>& residual) {
    double s = 0.0;
    for (double v : residual) s += v * v;
    return s;
}

double aicc(double rss, int n_obs, int p) {
    int k = p + 1;
    rss = std::max(rss, 1e-300);
    double denom = std::max((double)(n_obs - k - 1), 1.0);
    return n_obs * std::log(rss / n_obs) + 2 * k + 2 * k * (k + 1) / denom;
}

std::pair<double, double> fitMetrics(const std::vector<Complex>& z,
                                     const std::vector<Complex>& zfit) {
    double s2 = 0.0, mx = 0.0;
    for (size_t k = 0; k < z.size(); ++k) {
        double rel = std::abs((z[k] - zfit[k]) / z[k]);
        s2 += rel * rel;
        mx = std::max(mx, rel);
    }
    return {std::sqrt(s2 / (double)z.size()), mx};
}

std::vector<double> Candidate::values() const {
    std::vector<double> v(theta.size());
    for (size_t i = 0; i < theta.size(); ++i) v[i] = std::pow(10.0, theta[i]);
    return v;
}

// R7: outlier-robust refit of one candidate (single IRLS pass).  A few wild
// measurement points bend a plain least-squares fit away from the inlier
// core; the robust scale is estimated per axis of the COMPLEX relative
// residual (1.4826 x median|x| = sigma for a Gaussian axis), points whose
// residual magnitude exceeds 5 sigma are quadratically downweighted, and the
// candidate is re-fit under the downweighted weights from its own solution.
// Returns true when the parameters changed; metrics (rss/wrmse/aicc) are
// recomputed with the ORIGINAL weights so candidates stay comparable.
bool robustRefitCandidate(Candidate& c, const std::vector<Complex>& s,
                          const std::vector<Complex>& z,
                          const std::vector<double>& w) {
    const size_t m = z.size();
    if (m < 8) return false;
    // model at the current parameters
    std::vector<Complex> zfit(m);
    evalTheta(c.tree, c.theta, s.data(), m, zfit.data());
    std::vector<double> magRe(m), magIm(m), mag(m);
    for (size_t k = 0; k < m; ++k) {
        Complex rr = (z[k] - zfit[k]) / z[k];
        magRe[k] = std::fabs(rr.real());
        magIm[k] = std::fabs(rr.imag());
        mag[k] = std::abs(rr);
    }
    auto medOf = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    double sigAxis = std::max(1.4826 * std::max(medOf(magRe), medOf(magIm)), 1e-9);
    const double kCut = 5.0, kIn = 2.5;
    std::vector<double> wR = w;
    bool anyOut = false;
    int nInlier = 0, nOut = 0;
    for (size_t k = 0; k < m; ++k) {
        double r = mag[k] / sigAxis;
        if (r > kCut) {
            double f = kCut / r;
            wR[k] *= f * f;
            anyOut = true;
            ++nOut;
        } else if (r < kIn) {
            ++nInlier;
        }
    }
    if (!anyOut || nOut > (int)m / 3 || nInlier * 2 < (int)m) return false;

    // acceptance is judged on the ROBUST objective: the honest (inlier)
    // parameters necessarily have a HIGHER plain-weighted rss than the bent
    // least-squares solution, because the plain optimum minimises exactly
    // that.  Comparing plain rss would always reject the rescue.
    auto robustRss = [&](const std::vector<double>& theta) {
        std::vector<Complex> zf(m);
        evalTheta(c.tree, theta, s.data(), m, zf.data());
        double r = 0.0;
        for (size_t k = 0; k < m; ++k) {
            Complex e = wR[k] * (z[k] - zf[k]);
            r += e.real() * e.real() + e.imag() * e.imag();
        }
        return r;
    };
    double rssInc = robustRss(c.theta);

    std::vector<double> lb, ub;
    thetaBounds(c.tree, lb, ub);
    LMOpts opts;
    opts.maxNfev = 3000;
    opts.ftol = opts.xtol = opts.gtol = 1e-12;
    auto residual = [&](const std::vector<double>& th, std::vector<double>& out) {
        out = residualVector(c.tree, th, s, z, wR);
    };
    auto jac = [&](const std::vector<double>& th, std::vector<double>& out) {
        out = jacobianCs(c.tree, th, s, wR);
    };
    LMOut res = lmFit(residual, jac, c.theta, lb, ub, opts);
    double rssR = rssOf(res.residual);
    if (!std::isfinite(rssR) || rssR >= rssInc) return false;

    // metrics with the original weights at the robust parameters
    std::vector<double> resPlain = residualVector(c.tree, res.x, s, z, w);
    double rss = rssOf(resPlain);
    std::vector<Complex> zfit2(m);
    evalTheta(c.tree, res.x, s.data(), m, zfit2.data());
    auto [wrmse, mre] = fitMetrics(z, zfit2);
    c.theta = std::move(res.x);
    c.rss = rss;
    c.aiccVal = aicc(rss, (int)(2 * m), (int)c.theta.size());
    c.wrmse = wrmse;
    c.maxRelErr = mre;
    return true;
}

std::vector<std::vector<double>> heuristicStarts(const TreePtr& tree,
                                                 const StartHints* hints) {
    std::vector<char> kinds = paramKinds(tree);  // 'D' = Rd parameter of L
    std::vector<double> lb, ub;
    thetaBounds(tree, lb, ub);
    const size_t p = kinds.size();
    std::vector<double> mid(p);
    for (size_t i = 0; i < p; ++i) mid[i] = 0.5 * (lb[i] + ub[i]);
    auto dcrBounds = kindBounds('D');
    const double rdMid = 0.5 * (dcrBounds.first + dcrBounds.second);

    auto baseEstimate = [&](char kind, size_t i) -> double {
        if (hints == nullptr) return kind == 'D' ? rdMid : mid[i];
        if (kind == 'R') return std::log10(clipKind('R', hints->rLevel));
        if (kind == 'L') return std::log10(clipKind('L', hints->lEst));
        if (kind == 'D') return rdMid;  // no robust data-driven Rd estimate
        return std::log10(clipKind('C', hints->cEst));
    };

    std::vector<double> s0(p);
    for (size_t i = 0; i < p; ++i) s0[i] = baseEstimate(kinds[i], i);
    std::vector<std::vector<double>> starts{s0};

    bool hasL = false, hasC = false;
    for (char k : kinds) {
        if (k == 'L') hasL = true;
        if (k == 'C') hasC = true;
    }
    if (hints != nullptr && hints->hasWRes && hasL && hasC) {
        std::vector<double> th = s0;
        double lFirst = std::log10(clipKind('L', hints->lEst));
        for (size_t i = 0; i < p; ++i)
            if (kinds[i] == 'L') th[i] = lFirst;
        for (size_t i = 0; i < p; ++i) {
            if (kinds[i] == 'C') {
                double lc = 1.0 / (hints->wRes * hints->wRes);
                th[i] = std::log10(clipKind('C', lc / std::pow(10.0, lFirst)));
            }
        }
        if (hints->hasRPeak) {
            for (size_t i = 0; i < p; ++i)
                if (kinds[i] == 'R') th[i] = std::log10(clipKind('R', hints->rPeak));
        }
        for (size_t i = 0; i < p; ++i)
            th[i] = std::min(std::max(th[i], lb[i]), ub[i]);
        starts.push_back(std::move(th));
    }
    for (auto& st : starts) {
        for (size_t i = 0; i < p; ++i)
            st[i] = std::min(std::max(st[i], lb[i]), ub[i]);
    }
    return starts;
}

// ---------------------------------------------------------------------------
// box-constrained Levenberg-Marquardt
// ---------------------------------------------------------------------------

LMOut lmFit(std::function<void(const std::vector<double>&, std::vector<double>&)> residual,
            std::function<void(const std::vector<double>&, std::vector<double>&)> jac,
            std::vector<double> x0, const std::vector<double>& lb,
            const std::vector<double>& ub, const LMOpts& opts) {
    const size_t p = x0.size();
    // Python: x0 = clip(x0, lb + 1e-12, ub - 1e-12)
    for (size_t i = 0; i < p; ++i) {
        x0[i] = std::min(std::max(x0[i], lb[i] + 1e-12), ub[i] - 1e-12);
        if (!std::isfinite(x0[i])) x0[i] = 0.5 * (lb[i] + ub[i]);
    }
    int maxNfev = opts.maxNfev > 0 ? opts.maxNfev : (int)(100 * p);
    const int m = (int)p;

    std::vector<double> r;
    residual(x0, r);
    double rss = rssOf(r);
    std::vector<double> x = x0;

    std::vector<double> J;
    std::vector<double> g(p), H(p * p), dx(p), rTry;
    std::vector<double> Hsolved;

    double lambda = -1.0;  // initialised from the first Jacobian
    double nu = 2.0;
    int nfev = 1;
    const int maxIter = 200 * (int)p + 200;

    for (int iter = 0; iter < maxIter; ++iter) {
        // convergence: projected gradient
        jac(x, J);
        const int nrows = (int)J.size() / (int)p;
        std::fill(g.begin(), g.end(), 0.0);
        std::fill(H.begin(), H.end(), 0.0);
        for (int k = 0; k < nrows; ++k) {
            const double* row = J.data() + (size_t)k * p;
            double rk = r[k];
            for (int i = 0; i < m; ++i) {
                g[i] += row[i] * rk;
                for (int j = i; j < m; ++j) H[(size_t)i * m + j] += row[i] * row[j];
            }
        }
        for (int i = 0; i < m; ++i)
            for (int j = 0; j < i; ++j) H[(size_t)i * m + j] = H[(size_t)j * m + i];

        double ginf = 0.0;
        for (int i = 0; i < m; ++i) ginf = std::max(ginf, std::fabs(g[i]));
        if (ginf <= opts.gtol * std::max(1.0, rss)) break;

        if (lambda < 0.0) {
            double hmax = 0.0;
            for (int i = 0; i < m; ++i) hmax = std::max(hmax, H[(size_t)i * m + i]);
            lambda = 1e-3 * std::max(hmax, 1e-12);
            nu = 2.0;
        }

        bool stepped = false;
        for (int attempt = 0; attempt < 60 && !stepped; ++attempt) {
            // damped normal equations (H + lambda diag(H)) dx = -g
            Hsolved = H;
            for (int i = 0; i < m; ++i) {
                double& d = Hsolved[(size_t)i * m + i];
                d += lambda * std::max(d, 1e-12);
            }
            dx = g;
            for (double& v : dx) v = -v;
            bool ok = solveSPD(Hsolved, m, dx);
            bool finite = ok;
            if (ok) {
                for (double v : dx) {
                    if (!std::isfinite(v)) {
                        finite = false;
                        break;
                    }
                }
            }
            if (!finite) {
                lambda = (lambda > 0.0) ? lambda * 10.0 : 1e-12;
                nu = 2.0;
                continue;
            }

            std::vector<double> xTry(p);
            for (size_t i = 0; i < p; ++i)
                xTry[i] = std::min(std::max(x[i] + dx[i], lb[i]), ub[i]);
            residual(xTry, rTry);
            ++nfev;

            double rssTry = rssOf(rTry);
            // predicted reduction = -(g.dx + dx.H.dx / 2), positive for a
            // descent step
            double q = 0.0;
            for (int i = 0; i < m; ++i) q += g[i] * dx[i];
            for (int i = 0; i < m; ++i)
                for (int j = 0; j < m; ++j)
                    q += 0.5 * dx[i] * H[(size_t)i * m + j] * dx[j];
            double pred = -q;
            double denom = std::max(pred, 1e-300);
            double rho = (rss - rssTry) / denom;
            double rssFinite =
                std::isfinite(rssTry) ? rssTry : std::numeric_limits<double>::infinity();

            if (rho > 0.0 && rssFinite < rss) {
                // accept
                double stepNorm = 0.0, xNorm = 0.0;
                for (size_t i = 0; i < p; ++i) {
                    double d = xTry[i] - x[i];
                    stepNorm += d * d;
                    xNorm += xTry[i] * xTry[i];
                }
                stepNorm = std::sqrt(stepNorm);
                xNorm = std::sqrt(xNorm);
                double actualRed = rss - rssTry;
                double rssOld = rss;
                x = xTry;
                r = rTry;
                rss = rssTry;
                double t = std::max(1.0 / 3.0, 1.0 - std::pow(2.0 * rho - 1.0, 3));
                lambda *= t;
                if (lambda < 1e-18) lambda = 1e-18;
                nu = 2.0;
                stepped = true;
                if (actualRed <= opts.ftol * std::max(rssOld, 1e-300)) break;
                if (stepNorm <= opts.xtol * (opts.xtol + xNorm)) break;
                if (pred <= opts.ftol * std::max(rssOld, 1e-300)) break;
            } else {
                lambda *= nu;
                nu *= 2.0;
                if (lambda > 1e14) break;  // stalled at a bound / flat region
            }
            if (nfev >= maxNfev) break;
        }
        if (!stepped) break;      // no progress possible
        if (nfev >= maxNfev) break;
    }

    LMOut out;
    out.x = std::move(x);
    out.residual = std::move(r);
    out.rss = rss;
    out.nfev = nfev;
    return out;
}

std::optional<Candidate> fitTopology(const TreePtr& tree, const std::vector<Complex>& s,
                                     const std::vector<Complex>& z,
                                     const std::vector<double>& w,
                                     const std::vector<std::vector<double>>& starts,
                                     int maxNfev, double tol) {
    std::vector<double> lb, ub;
    thetaBounds(tree, lb, ub);
    const size_t p = lb.size();
    std::optional<Candidate> best;

    auto residual = [&](const std::vector<double>& th, std::vector<double>& out) {
        out = residualVector(tree, th, s, z, w);
    };
    auto jac = [&](const std::vector<double>& th, std::vector<double>& out) {
        out = jacobianCs(tree, th, s, w);
    };

    for (const auto& st : starts) {
        LMOpts opts;
        opts.maxNfev = maxNfev;
        opts.ftol = opts.xtol = opts.gtol = tol;
        LMOut res;
        try {
            res = lmFit(residual, jac, st, lb, ub, opts);
        } catch (...) {
            continue;
        }
        double rss = rssOf(res.residual);
        if (!best.has_value() || rss < best->rss) {
            std::vector<Complex> zfit(z.size());
            evalTheta(tree, res.x, s.data(), z.size(), zfit.data());
            auto [wrmse, emax] = fitMetrics(z, zfit);
            Candidate c;
            c.tree = tree;
            c.theta = res.x;
            c.rss = rss;
            c.aiccVal = aicc(rss, (int)(2 * z.size()), (int)p);
            c.wrmse = wrmse;
            c.maxRelErr = emax;
            best = std::move(c);
        }
    }
    return best;
}

std::vector<Candidate> fitLibrary(const std::vector<TreePtr>& trees,
                                  const std::vector<Complex>& s,
                                  const std::vector<Complex>& z,
                                  const std::vector<double>& w,
                                  const EngineAConfig& config,
                                  const StartHints* hints,
                                  const std::map<std::string, std::vector<std::vector<double>>>* extraStarts) {
    Rng rng(config.seed);
    std::vector<Candidate> coarse;
    for (const auto& tree : trees) {
        std::vector<double> lb, ub;
        thetaBounds(tree, lb, ub);
        std::vector<std::vector<double>> starts = heuristicStarts(tree, hints);
        if (extraStarts) {
            auto it = extraStarts->find(canonical(tree));
            if (it != extraStarts->end())
                for (const auto& v : it->second) starts.push_back(v);
        }
        int nLhs = std::max(config.nStartsCoarse - (int)starts.size(), 1);
        auto lhs = lhsStarts(nLhs, lb, ub, rng);
        for (auto& v : lhs) starts.push_back(std::move(v));
        auto cand = fitTopology(tree, s, z, w, starts, config.maxNfevCoarse, config.tolCoarse);
        if (cand.has_value()) coarse.push_back(std::move(*cand));
    }
    if (coarse.empty()) return {};

    // stage 2: refine the best fraction with the full start set
    std::stable_sort(coarse.begin(), coarse.end(),
                     [](const Candidate& a, const Candidate& b) { return a.rss < b.rss; });
    int nRef = std::max(1, (int)std::ceil(config.refineFraction * (double)coarse.size()));
    std::set<std::string> front;
    for (int i = 0; i < nRef && i < (int)coarse.size(); ++i)
        front.insert(coarse[i].canonicalStr());
    if (extraStarts)
        for (const auto& kv : *extraStarts) front.insert(kv.first);

    std::vector<Candidate> refined;
    for (auto& cand : coarse) {
        if (front.find(cand.canonicalStr()) == front.end()) {
            refined.push_back(cand);
            continue;
        }
        const TreePtr& tree = cand.tree;
        std::vector<double> lb, ub;
        thetaBounds(tree, lb, ub);
        std::vector<std::vector<double>> starts{cand.theta};
        for (auto& v : heuristicStarts(tree, hints)) starts.push_back(std::move(v));
        if (extraStarts) {
            auto it = extraStarts->find(canonical(tree));
            if (it != extraStarts->end())
                for (const auto& v : it->second) starts.push_back(v);
        }
        int nLhs = std::max(config.nStartsRefine - (int)starts.size(), 2);
        auto lhs = lhsStarts(nLhs, lb, ub, rng);
        for (auto& v : lhs) starts.push_back(std::move(v));
        auto best = fitTopology(tree, s, z, w, starts, config.maxNfevRefine, config.tolRefine);
        refined.push_back(best.has_value() ? std::move(*best) : cand);
    }
    std::stable_sort(refined.begin(), refined.end(), [](const Candidate& a, const Candidate& b) {
        return a.aiccVal < b.aiccVal;
    });
    return refined;
}

}  // namespace rlc
