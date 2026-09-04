#include "fit.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>

namespace tf {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEps = std::numeric_limits<double>::epsilon();

// Least squares min ||A x - y|| via one-sided Jacobi SVD (column scaling +
// numpy-lstsq-style cutoff); returns all-zero when the system is empty.
std::vector<double> svdSolve(const std::vector<double>& A, const std::vector<double>& y,
                             int m, int n) {
    std::vector<double> scale(n, 1.0);
    for (int j = 0; j < n; ++j) {
        double sum = 0.0;
        for (int i = 0; i < m; ++i) {
            double v = A[(size_t)i * n + j];
            sum += v * v;
        }
        scale[j] = std::sqrt(sum);
        if (scale[j] == 0.0) scale[j] = 1.0;
    }
    std::vector<double> B((size_t)m * n);
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j)
            B[(size_t)i * n + (size_t)j] = A[(size_t)i * n + (size_t)j] / scale[j];
    std::vector<double> V((size_t)n * n, 0.0);
    for (int j = 0; j < n; ++j) V[(size_t)j * n + (size_t)j] = 1.0;
    auto columnDot = [&](int a, int b) {
        double s = 0.0;
        for (int i = 0; i < m; ++i) s += B[(size_t)i * n + (size_t)a] * B[(size_t)i * n + (size_t)b];
        return s;
    };
    for (int sweep = 0; sweep < 60; ++sweep) {
        bool rotated = false;
        for (int pa = 0; pa < n - 1; ++pa) {
            for (int q = pa + 1; q < n; ++q) {
                double app = columnDot(pa, pa), aqq = columnDot(q, q);
                double apq = columnDot(pa, q);
                if (std::fabs(apq) <= 1e-15 * std::sqrt(app * aqq + 1e-300)) continue;
                rotated = true;
                double tau = (aqq - app) / (2.0 * apq);
                double t = (tau >= 0.0 ? 1.0 : -1.0) /
                           (std::fabs(tau) + std::sqrt(1.0 + tau * tau));
                double cc = 1.0 / std::sqrt(1.0 + t * t);
                double s = cc * t;
                for (int i = 0; i < m; ++i) {
                    double bp = B[(size_t)i * n + (size_t)pa], bq = B[(size_t)i * n + (size_t)q];
                    B[(size_t)i * n + (size_t)pa] = cc * bp - s * bq;
                    B[(size_t)i * n + (size_t)q] = s * bp + cc * bq;
                }
                for (int i = 0; i < n; ++i) {
                    double vp = V[(size_t)i * n + (size_t)pa], vq = V[(size_t)i * n + (size_t)q];
                    V[(size_t)i * n + (size_t)pa] = cc * vp - s * vq;
                    V[(size_t)i * n + (size_t)q] = s * vp + cc * vq;
                }
            }
        }
        if (!rotated) break;
    }
    std::vector<double> sigma(n);
    double smax = 0.0;
    for (int j = 0; j < n; ++j) {
        sigma[j] = std::sqrt(std::max(0.0, columnDot(j, j)));
        smax = std::max(smax, sigma[j]);
    }
    double cutoff = kEps * std::max(m, n) * smax;
    std::vector<double> x(n, 0.0);
    for (int j = 0; j < n; ++j) {
        if (sigma[j] <= cutoff || sigma[j] == 0.0) continue;
        double uy = 0.0;
        for (int i = 0; i < m; ++i) uy += B[(size_t)i * n + (size_t)j] * y[(size_t)i];
        double coef = uy / sigma[j] / sigma[j];
        for (int i = 0; i < n; ++i) x[(size_t)i] += coef * V[(size_t)i * n + (size_t)j];
    }
    for (int j = 0; j < n; ++j) x[(size_t)j] /= scale[j];
    return x;
}


void clipToBox(std::vector<double>& x, const std::vector<double>& lb,
               const std::vector<double>& ub) {
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = std::min(std::max(x[i], lb[i] + 1e-12), ub[i] - 1e-12);
}

}  // namespace

// ---------------------------------------------------------------------------
// box-constrained Levenberg-Marquardt
// ---------------------------------------------------------------------------

LMOut lmFit(const std::function<void(const std::vector<double>&, std::vector<double>&)>& residual,
            const std::function<void(const std::vector<double>&, std::vector<double>&)>& jac,
            std::vector<double> x0, const std::vector<double>& lb,
            const std::vector<double>& ub, int maxNfev, double ftol, double xtol,
            double gtol) {
    const size_t p = x0.size();
    const size_t p_ = p;
    for (size_t i = 0; i < p; ++i) {
        x0[i] = std::min(std::max(x0[i], lb[i] + 1e-12), ub[i] - 1e-12);
        if (!std::isfinite(x0[i])) x0[i] = 0.5 * (lb[i] + ub[i]);
    }
    if (maxNfev <= 0) maxNfev = (int)(100 * p);
    const int m = (int)p;

    std::vector<double> r;
    residual(x0, r);
    double rss = rssOf(r);
    std::vector<double> x = x0;

    std::vector<double> J;
    std::vector<double> g(p), H(p * p), dx(p), rTry;
    std::vector<double> Hsolved;

    double lambda = -1.0;
    double nu = 2.0;
    int nfev = 1;
    const int maxIter = 200 * (int)p + 200;

    for (int iter = 0; iter < maxIter; ++iter) {
        jac(x, J);
        const int nrows = (int)J.size() / (int)p;
        std::fill(g.begin(), g.end(), 0.0);
        std::fill(H.begin(), H.end(), 0.0);
        for (int k = 0; k < nrows; ++k) {
            const double* row = J.data() + (size_t)k * p;
            double rk = r[(size_t)k];
            for (int i = 0; i < m; ++i) {
                g[i] += row[i] * rk;
                for (int j = i; j < m; ++j) H[(size_t)i * m + j] += row[i] * row[j];
            }
        }
        for (int i = 0; i < m; ++i)
            for (int j = 0; j < i; ++j) H[(size_t)i * m + j] = H[(size_t)j * m + i];

        double ginf = 0.0;
        for (int i = 0; i < m; ++i) ginf = std::max(ginf, std::fabs(g[i]));
        if (ginf <= gtol * std::max(1.0, rss)) {
            break;
        }

        double hmax = 0.0;
        for (int i = 0; i < m; ++i) hmax = std::max(hmax, H[(size_t)i * m + i]);
        double lamCap = 1e12 * std::max(hmax, 1e-12);  // scale-aware upper cap
        if (lambda < 0.0) {
            lambda = 1e-3 * std::max(hmax, 1e-12);
            nu = 2.0;
        }

        bool stepped = false;
        for (int attempt = 0; attempt < 60 && !stepped; ++attempt) {
            // Damped subproblem via the AUGMENTED least-squares system
            //   min ||J dx + r||^2 + lambda ||D^{1/2} dx||^2
            // solved by one-sided Jacobi SVD -- mathematically the same LM
            // step as (J^T J + lambda D) dx = -J^T r but numerically stable
            // on rank-deficient Jacobians (cond(J) instead of cond(J)^2;
            // minimum-norm step in null directions, like scipy trf).
            int nrowsJ = (int)J.size() / (int)p;
            std::vector<double> B((size_t)(nrowsJ + m) * m, 0.0);
            std::vector<double> y((size_t)(nrowsJ + m), 0.0);
            for (int k = 0; k < nrowsJ; ++k) {
                for (int i = 0; i < m; ++i)
                    B[(size_t)k * m + (size_t)i] = J[(size_t)k * p + (size_t)i];
                y[(size_t)k] = -r[(size_t)k];
            }
            for (int i = 0; i < m; ++i) {
                double di = std::sqrt(std::max(H[(size_t)i * m + (size_t)i], 1e-300));
                B[(size_t)(nrowsJ + i) * m + (size_t)i] = std::sqrt(lambda) * di;
                y[(size_t)(nrowsJ + i)] = 0.0;
            }
            dx = svdSolve(B, y, nrowsJ + m, m);
            bool finite = true;
            for (double v : dx) {
                if (!std::isfinite(v)) {
                    finite = false;
                    break;
                }
            }
            if (!finite) {
                lambda = (lambda > 0.0) ? lambda * 10.0 : 1e-12;
                nu = 2.0;
                if (lambda > lamCap) break;
                continue;
            }


            std::vector<double> xTry(p);
            for (size_t i = 0; i < p; ++i) {
                double xi = x[i] + dx[i];
                if (xi < lb[i]) {
                    // REFLECT off the bound (TRF-style): the excursion beyond
                    // the box carries information -- bounce it back inside
                    // instead of pinning the iterate to the wall
                    xi = lb[i] + (lb[i] - xi);
                    if (!(xi <= ub[i])) xi = lb[i];
                } else if (xi > ub[i]) {
                    xi = ub[i] - (xi - ub[i]);
                    if (!(xi >= lb[i])) xi = ub[i];
                }
                xTry[i] = xi;
            }
            double rssTry;
            if (attempt == 8) {
                // fallback after half the failed damping attempts: projected
                // scaled-gradient direction with backtracking -- first-order
                // descent that escapes boundary stalls the damped Gauss
                // Newton step cannot handle (outward gradient pinned at a
                // bound; scipy trf solves this via its reflective transform)
                std::vector<double> d(p);
                for (size_t i = 0; i < p; ++i) {
                    double hi = std::max(H[i * p_ + i], 1e-12);
                    d[i] = -g[i] / hi;
                    if (x[i] <= lb[i] + 1e-12 && d[i] < 0.0) d[i] = 0.0;
                    if (x[i] >= ub[i] - 1e-12 && d[i] > 0.0) d[i] = 0.0;
                }
                bool ok = false;
                for (double t = 1.0; t > 1e-9 && nfev < maxNfev; t *= 0.25) {
                    for (size_t i = 0; i < p; ++i)
                        xTry[i] = std::min(std::max(x[i] + t * d[i], lb[i]), ub[i]);
                    residual(xTry, rTry);
                    ++nfev;
                    double r2 = rssOf(rTry);
                    if (std::isfinite(r2) && r2 < rss) {
                        rssTry = r2;
                        lambda = -1.0;  // re-initialize damping at the new point
                        ok = true;
                        break;
                    }
                }
                if (!ok) {
                    lambda *= nu;
                    nu *= 2.0;
                    if (lambda > lamCap) break;
                    if (nfev >= maxNfev) break;
                    continue;
                }
            } else {
                residual(xTry, rTry);
                ++nfev;
                rssTry = rssOf(rTry);
            }

            // predicted reduction of the PROJECTED step (bound-clipped
            // components must not be credited): dproj = xTry - x
            double q = 0.0;
            for (int i = 0; i < m; ++i) q += g[i] * (xTry[i] - x[i]);
            for (int i = 0; i < m; ++i)
                for (int j = 0; j < m; ++j)
                    q += 0.5 * (xTry[i] - x[i]) * H[(size_t)i * m + j] * (xTry[j] - x[j]);
            double pred = -q;
            double denom = std::max(pred, 1e-300);
            double rho = (rss - rssTry) / denom;
            double rssFinite =
                std::isfinite(rssTry) ? rssTry : std::numeric_limits<double>::infinity();

            if (rho > 0.0 && rssFinite < rss) {
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
                // floor: keep the damping tied to the problem scale so a run
                // of lucky steps cannot collapse lambda into a pure Gauss
                // -Newton step that explodes on ill-conditioned valleys
                double hmax2 = 0.0;
                for (int i2 = 0; i2 < m; ++i2)
                    hmax2 = std::max(hmax2, H[(size_t)i2 * m + (size_t)i2]);
                double lmin = 1e-10 * std::max(hmax2, 1e-12);
                if (lambda < lmin) lambda = lmin;
                nu = 2.0;
                stepped = true;
                if (actualRed <= ftol * std::max(rssOld, 1e-300)) {
                    break;
                }
                if (stepNorm <= xtol * (xtol + xNorm)) {
                    break;
                }
                if (pred <= ftol * std::max(rssOld, 1e-300)) {
                    break;
                }
            } else {
                lambda *= nu;
                nu *= 2.0;
                if (lambda > lamCap) break;
            }
            if (nfev >= maxNfev) break;
        }
        if (!stepped) {
            break;
        }
        if (nfev >= maxNfev) {
            break;
        }
    }

    LMOut out;
    out.x = std::move(x);
    out.residual = std::move(r);
    out.rss = rss;
    out.nfev = nfev;
    return out;
}

// ---------------------------------------------------------------------------
// FitResult helpers
// ---------------------------------------------------------------------------

std::vector<Value> FitResult::groupValues() const {
    std::vector<Value> out;
    out.reserve(groups.size());
    for (const auto& g : groups) out.push_back(g.value);
    return out;
}

std::vector<Complex> FitResult::zModel(const std::vector<double>& f) const {
    std::vector<Complex> sT(f.size());
    for (size_t k = 0; k < f.size(); ++k)
        sT[k] = Complex(0.0, 1.0) * (2.0 * kPi * f[k] / w0);
    std::vector<Complex> Z, J;
    model.zAndJac(thetaNorm, sT, Z, J);
    for (auto& zk : Z) zk *= z0;
    return Z;
}

std::string FitResult::describe() const {
    std::string out;
    char buf[160];
    for (const auto& g : groups) {
        std::string vs;
        if (g.kind == 'R') {
            std::snprintf(buf, sizeof(buf), "%.6gohm", g.value.v1);
            vs = buf;
        } else if (g.kind == 'C') {
            std::snprintf(buf, sizeof(buf), "%.6gF", g.value.v1);
            vs = buf;
        } else {
            std::snprintf(buf, sizeof(buf), "%.6gH+%.6gohm", g.value.v1, g.value.v2);
            vs = buf;
        }
        std::string extra;
        if (!g.weakParams.empty()) {
            extra += " weak:";
            for (size_t i = 0; i < g.weakParams.size(); ++i) {
                if (i) extra += ",";
                extra += g.weakParams[i];
            }
        }
        if (!g.atBound.empty()) {
            extra += " at_bound:";
            for (size_t i = 0; i < g.atBound.size(); ++i) {
                if (i) extra += ",";
                extra += g.atBound[i];
            }
        }
        std::string mode =
            g.members.size() == 1 ? std::string("single") : g.mode + "-merged";
        std::string mem;
        for (size_t i = 0; i < g.members.size(); ++i) {
            if (i) mem += ",";
            mem += std::to_string(g.members[i]);
        }
        std::snprintf(buf, sizeof(buf), "  g%d %c[%d-%d] %s = %s{%s}%s\n", g.gid, g.kind,
                      g.u, g.v, mode.c_str(), vs.c_str(), mem.c_str(), extra.c_str());
        out += buf;
    }
    for (const auto& [i, why] : reduction.dropped) {
        std::snprintf(buf, sizeof(buf), "  e%d %c dropped (%s)\n", i,
                      edges[(size_t)i] == std::tuple<int, int, char>{0, 0, 'R'}
                          ? '?'
                          : std::get<2>(edges[(size_t)i]),
                      why.c_str());
        out += buf;
    }
    std::snprintf(buf, sizeof(buf),
                  "  wRMSE=%.4g  maxRel=%.4g  AICc=%.2f  (%.1f ms, %d starts)\n", wrmse,
                  maxRel, aiccVal, seconds * 1e3, nStartsUsed);
    out += buf;
    if (jacRank >= 0 && jacRank < nParams) {
        std::snprintf(buf, sizeof(buf),
                      "  identifiability: rank %d/%d -- parameter vector jointly"
                      " unidentifiable (only rank combinations are determined)\n",
                      jacRank, nParams);
        out += buf;
    } else if (jacCond > 1e4) {
        std::snprintf(buf, sizeof(buf),
                      "  identifiability: full rank but ill-conditioned (cond %.1e)\n",
                      jacCond);
        out += buf;
    }
    if (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

// ---------------------------------------------------------------------------
// the solver core (port of fit.py internals)
// ---------------------------------------------------------------------------

namespace {

struct Unit {
    char letter = 'R';  // 'R' | 'C' | 'L' | 'd' (Rd)
    double scale = 1.0;
};

std::vector<Unit> paramUnits(const NodalModel& model) {
    std::vector<Unit> units;
    for (const auto& [u, v, kind] : model.edges) {
        (void)u;
        (void)v;
        if (kind == 'R') units.push_back({'R', 1.0});
        else if (kind == 'C') units.push_back({'C', 1.0});
        else {
            units.push_back({'L', 1.0});
            units.push_back({'d', 1.0});
        }
    }
    return units;
}

std::vector<double> normScales(const std::vector<Unit>& units, double w0, double z0) {
    std::vector<double> out(units.size());
    for (size_t t = 0; t < units.size(); ++t) {
        switch (units[t].letter) {
            case 'R': out[t] = z0; break;
            case 'L': out[t] = z0 / w0; break;
            case 'C': out[t] = 1.0 / (z0 * w0); break;
            default: out[t] = z0; break;  // Rd
        }
    }
    return out;
}

struct Objective {
    const NodalModel* model;
    std::vector<Complex> sT;
    std::vector<Complex> zT;
    std::vector<double> w;
    std::vector<double> cacheTheta;
    std::vector<Complex> cacheZ, cacheJ;
    bool hasCache = false;

    void eval(const std::vector<double>& theta) {
        if (hasCache && cacheTheta == theta) return;
        model->zAndJac(theta, sT, cacheZ, cacheJ);
        cacheTheta = theta;
        hasCache = true;
    }
    void fun(const std::vector<double>& theta, std::vector<double>& out) {
        eval(theta);
        out.resize(2 * zT.size());
        for (size_t k = 0; k < zT.size(); ++k) {
            Complex r = w[k] * (zT[k] - cacheZ[k]);
            out[2 * k] = r.real();
            out[2 * k + 1] = r.imag();
        }
    }
    // (2M, p) row-major: rows interleaved Re/Im, columns = params
    void jacM(const std::vector<double>& theta, std::vector<double>& out) {
        eval(theta);
        const size_t M = zT.size(), p = (size_t)model->nParams;
        out.assign(2 * M * p, 0.0);
        for (size_t t = 0; t < p; ++t)
            for (size_t k = 0; k < M; ++k) {
                Complex dr = -w[k] * cacheJ[t * M + k];
                out[(2 * k) * p + t] = dr.real();
                out[(2 * k + 1) * p + t] = dr.imag();
            }
    }
};

std::vector<double> unitStart(const NodalModel& model, const std::vector<Unit>& units) {
    std::vector<double> theta((size_t)model.nParams, 0.0);
    for (size_t t = 0; t < units.size(); ++t)
        if (units[t].letter == 'd') theta[t] = -1.0;
    return theta;
}

std::vector<double> resonanceOmegas(const std::vector<Complex>& sT,
                                    const std::vector<Complex>& zT, double prominence = 4.0,
                                    int maxRes = 2) {
    std::vector<double> a(zT.size());
    for (size_t k = 0; k < zT.size(); ++k) a[k] = std::abs(zT[k]);
    double med = median(a);
    std::vector<std::pair<double, double>> cands;  // (prominence, omega)
    for (size_t k = 1; k + 1 < a.size(); ++k) {
        if (a[k] >= a[k - 1] && a[k] > a[k + 1]) {
            double prom = a[k] / med;
            if (prom >= prominence) cands.push_back({prom, std::abs(sT[k])});
        } else if (a[k] <= a[k - 1] && a[k] < a[k + 1]) {
            double prom = med / a[k];
            if (prom >= prominence) cands.push_back({prom, std::abs(sT[k])});
        }
    }
    std::stable_sort(cands.begin(), cands.end(),
                     [](const auto& x, const auto& y) { return x.first > y.first; });
    std::vector<double> out;
    for (size_t i = 0; i < cands.size() && (int)i < maxRes; ++i)
        out.push_back(cands[i].second);
    return out;
}

std::vector<std::vector<double>> resonanceStarts(const NodalModel& model,
                                                 const std::vector<Unit>& units,
                                                 const std::vector<double>& lb,
                                                 const std::vector<double>& ub,
                                                 const std::vector<Complex>& sT,
                                                 const std::vector<Complex>& zT,
                                                 const std::vector<double>& base) {
    (void)model;
    (void)units;
    std::vector<double> omegas = resonanceOmegas(sT, zT);
    if (omegas.empty()) return {};
    std::vector<int> lIdx, cIdx;
    for (size_t t = 0; t < units.size(); ++t) {
        if (units[t].letter == 'L') lIdx.push_back((int)t);
        if (units[t].letter == 'C') cIdx.push_back((int)t);
    }
    if (lIdx.empty() || cIdx.empty()) return {};
    std::vector<std::vector<double>> out;
    for (double w0 : omegas) {
        for (int li : lIdx) {
            std::vector<double> x = base;
            double c = std::pow(10.0, x[(size_t)cIdx[0]]);
            x[(size_t)li] = std::log10(1.0 / (w0 * w0 * c));
            clipToBox(x, lb, ub);
            out.push_back(x);
        }
        for (int ci : cIdx) {
            std::vector<double> x = base;
            double l = std::pow(10.0, x[(size_t)lIdx[0]]);
            x[(size_t)ci] = std::log10(1.0 / (w0 * w0 * l));
            clipToBox(x, lb, ub);
            out.push_back(x);
        }
    }
    return out;
}

std::vector<std::vector<double>> lhsCenter(const NodalModel& model,
                                           const std::vector<double>& lb,
                                           const std::vector<double>& ub, int n, Rng& rng,
                                           double width = 2.0) {
    (void)model;
    if (n <= 0) return {};
    std::vector<double> clo(lb.size()), chi(lb.size());
    for (size_t i = 0; i < lb.size(); ++i) {
        clo[i] = std::max(lb[i], -width);
        chi[i] = std::min(ub[i], width);
    }
    return lhsStarts(n, clo, chi, rng);
}

std::vector<std::vector<double>> perturbStarts(const std::vector<double>& xBest,
                                               const std::vector<double>& lb,
                                               const std::vector<double>& ub, int n,
                                               Rng& rng, double sigma = 1.0) {
    if (n <= 0 || xBest.empty()) return {};
    std::vector<std::vector<double>> out;
    for (int q = 0; q < n; ++q) {
        std::vector<double> x = xBest;
        for (size_t i = 0; i < x.size(); ++i) x[i] += rng.normal() * sigma;
        clipToBox(x, lb, ub);
        out.push_back(std::move(x));
    }
    return out;
}

std::vector<std::vector<double>> topoResonanceStarts(const NodalModel& model,
                                                     const std::vector<Unit>& units,
                                                     const std::vector<double>& lb,
                                                     const std::vector<double>& ub,
                                                     const std::vector<Complex>& sT,
                                                     const std::vector<Complex>& zT,
                                                     const std::vector<double>& base) {
    (void)units;
    std::vector<double> omegas = resonanceOmegas(sT, zT);
    if (omegas.empty()) return {};
    // last L / C edge per node pair (dict overwrite semantics)
    std::map<std::pair<int, int>, int> lOf, cOf;
    std::vector<std::pair<int, int>> lOrder, cOrder;
    for (size_t e = 0; e < model.edges.size(); ++e) {
        auto [u, v, kind] = model.edges[e];
        auto pr = std::make_pair(std::min(u, v), std::max(u, v));
        if (kind == 'L') {
            if (!lOf.count(pr)) lOrder.push_back(pr);
            lOf[pr] = (int)e;
        } else if (kind == 'C') {
            if (!cOf.count(pr)) cOrder.push_back(pr);
            cOf[pr] = (int)e;
        }
    }
    std::map<int, int> deg;
    for (const auto& [u, v, k] : model.edges) {
        (void)k;
        deg[u] += 1;
        deg[v] += 1;
    }
    auto paramStart = [&model](int e) { return model.edgeParamSlices[(size_t)e].first; };

    std::vector<std::pair<int, int>> pairs;  // (L param, C param)
    std::set<std::pair<int, int>> seenPair;
    for (auto pr : lOrder) {
        if (cOf.count(pr))
            pairs.push_back({paramStart(lOf[pr]), paramStart(cOf[pr])});
    }
    for (size_t e1 = 0; e1 < model.edges.size(); ++e1) {
        for (size_t e2 = e1 + 1; e2 < model.edges.size(); ++e2) {
            char k1 = std::get<2>(model.edges[e1]), k2 = std::get<2>(model.edges[e2]);
            if (!((k1 == 'L' && k2 == 'C') || (k1 == 'C' && k2 == 'L'))) continue;
            auto [u1, v1, kk1] = model.edges[e1];
            auto [u2, v2, kk2] = model.edges[e2];
            (void)kk1;
            (void)kk2;
            std::vector<int> sh;  // {u1,v1} & {u2,v2}, ascending labels
            for (int w : {u1, v1})
                if ((w == u2 || w == v2) && std::find(sh.begin(), sh.end(), w) == sh.end())
                    sh.push_back(w);
            std::sort(sh.begin(), sh.end());
            for (int w : sh) {
                if (w != 0 && w != 1) {
                    auto dw = deg.find(w);
                    if (dw != deg.end() && dw->second == 2) {
                        auto key = std::make_pair((int)e1, (int)e2);
                        if (!seenPair.count(key)) {
                            seenPair.insert(key);
                            pairs.push_back({paramStart((int)(k1 == 'L' ? e1 : e2)),
                                             paramStart((int)(k1 == 'C' ? e1 : e2))});
                        }
                    }
                }
            }
        }
    }
    // dict.fromkeys dedup (preserve first occurrence), cap 6
    std::vector<std::pair<int, int>> uniq;
    std::set<std::pair<int, int>> uniqSeen;
    for (auto& pr : pairs) {
        if (uniqSeen.insert(pr).second) {
            uniq.push_back(pr);
            if (uniq.size() >= 6) break;
        }
    }
    if (uniq.empty()) return {};
    pairs = std::move(uniq);

    auto aligned = [&](const std::vector<std::pair<std::pair<int, int>, double>>& po) {
        std::vector<double> x = base;
        for (const auto& [lc, w0] : po) {
            double cVal = std::pow(10.0, x[(size_t)lc.second]);
            x[(size_t)lc.first] = std::log10(1.0 / (w0 * w0 * cVal));
        }
        clipToBox(x, lb, ub);
        return x;
    };

    std::vector<std::vector<double>> out;
    for (double w0 : omegas)
        for (const auto& pr : pairs) out.push_back(aligned({{pr, w0}}));
    if (omegas.size() >= 2 && pairs.size() >= 2)
        for (size_t i = 0; i < pairs.size(); ++i)
            for (size_t j = 0; j < pairs.size(); ++j) {
                if (i == j) continue;
                out.push_back(aligned({{pairs[i], omegas[0]}, {pairs[j], omegas[1]}}));
            }
    return out;
}

std::vector<std::vector<double>> pairResonanceRestarts(const NodalModel& model,
                                                       const std::vector<Unit>& units,
                                                       const std::vector<double>& lb,
                                                       const std::vector<double>& ub,
                                                       const std::vector<Complex>& sT,
                                                       const std::vector<Complex>& zT,
                                                       const std::vector<double>& xBest) {
    (void)model;
    std::vector<double> omegas = resonanceOmegas(sT, zT);
    if (omegas.empty() || xBest.empty()) return {};
    std::vector<int> lIdx, cIdx;
    for (size_t t = 0; t < units.size(); ++t) {
        if (units[t].letter == 'L') lIdx.push_back((int)t);
        if (units[t].letter == 'C') cIdx.push_back((int)t);
    }
    std::vector<std::vector<double>> out;
    for (double w0 : omegas)
        for (int li : lIdx)
            for (int ci : cIdx) {
                std::vector<double> x = xBest;
                double cVal = std::pow(10.0, x[(size_t)ci]);
                x[(size_t)li] = std::log10(1.0 / (w0 * w0 * cVal));
                clipToBox(x, lb, ub);
                out.push_back(x);
            }
    return out;
}

struct RssPoint {
    double rss;
    std::vector<double> x;
};

std::vector<RssPoint> runStarts(Objective& obj, const std::vector<std::vector<double>>& starts,
                                const std::vector<double>& lb,
                                const std::vector<double>& ub, const FitConfig& cfg) {
    std::vector<RssPoint> results;
    int maxNfev = std::max(120, cfg.maxNfevFactor * obj.model->nParams);
    for (const auto& s : starts) {
        LMOut res = lmFit([&](const std::vector<double>& th, std::vector<double>& o) { obj.fun(th, o); },
                          [&](const std::vector<double>& th, std::vector<double>& o) { obj.jacM(th, o); },
                          s, lb, ub, maxNfev, cfg.tolCoarse, cfg.tolCoarse, cfg.tolCoarse);
        if (!std::isfinite(res.rss)) continue;
        results.push_back({res.rss, std::move(res.x)});
    }
    std::stable_sort(results.begin(), results.end(),
                     [](const RssPoint& a, const RssPoint& b) { return a.rss < b.rss; });
    return results;
}

std::pair<double, std::vector<double>> polish(Objective& obj,
                                              const std::vector<std::vector<double>>& startsX,
                                              const std::vector<double>& lb,
                                              const std::vector<double>& ub,
                                              const FitConfig& cfg,
                                              int nfevBudget = 0) {
    double bestRss = std::numeric_limits<double>::infinity();
    std::vector<double> bestX;
    int maxNfev = nfevBudget > 0 ? nfevBudget : std::max(400, 60 * obj.model->nParams);
    for (const auto& s : startsX) {
        LMOut res = lmFit([&](const std::vector<double>& th, std::vector<double>& o) { obj.fun(th, o); },
                          [&](const std::vector<double>& th, std::vector<double>& o) { obj.jacM(th, o); },
                          s, lb, ub, maxNfev, cfg.tolPolish, cfg.tolPolish, cfg.tolPolish);
        bool xFinite = std::isfinite(res.rss);
        for (double v : res.x) xFinite = xFinite && std::isfinite(v);
        if (xFinite && res.rss < bestRss) {
            bestRss = res.rss;
            bestX = res.x;
        }
    }
    return {bestRss, bestX};
}

std::pair<double, std::vector<double>> homotopyRescue(Objective& obj,
                                                      const NodalModel& model,
                                                      const std::vector<double>& lb,
                                                      const std::vector<double>& ub,
                                                      const std::vector<double>& x0) {
    std::vector<double> x = x0;
    clipToBox(x, lb, ub);
    for (double floorV : {1.0, 0.0, -1.0, -2.0}) {
        std::vector<double> lbk = lb;
        for (size_t e = 0; e < model.edges.size(); ++e) {
            if (std::get<2>(model.edges[e]) == 'L') {
                auto [sl, cnt] = model.edgeParamSlices[e];
                (void)cnt;
                lbk[(size_t)sl + 1] = std::max(lb[(size_t)sl + 1], floorV);
            }
        }
        std::vector<double> xk = x;
        for (size_t i = 0; i < x.size(); ++i)
            xk[i] = std::min(std::max(xk[i], lbk[i] + 1e-12), ub[i] - 1e-12);
        LMOut res = lmFit([&](const std::vector<double>& th, std::vector<double>& o) { obj.fun(th, o); },
                          [&](const std::vector<double>& th, std::vector<double>& o) { obj.jacM(th, o); },
                          xk, lbk, ub, 2000, 1e-12, 1e-12, 1e-12);
        x = res.x;
    }
    std::vector<double> resid;
    obj.fun(x, resid);
    return {rssOf(resid), x};
}

void buildReports(const ReductionResult& red,
                  const std::vector<std::tuple<int, int, char>>& edges,
                  const NodalModel& model, const std::vector<double>& physTheta,
                  const std::vector<char>& weak,
                  const std::vector<char>& atBnd, std::vector<GroupReport>& groups,
                  std::vector<EdgeReport>& edgesOut) {
    for (size_t gid = 0; gid < red.edges.size(); ++gid) {
        const auto& gedge = red.edges[gid];
        auto [sl, cnt] = model.edgeParamSlices[gid];
        GroupReport g;
        g.gid = (int)gid;
        g.kind = gedge.kind;
        g.u = gedge.u;
        g.v = gedge.v;
        g.members = gedge.members;
        g.mode = gedge.expr.leaf ? "single" : (gedge.expr.tag == 'p' ? "par" : "ser");
        if (gedge.kind == 'R') {
            g.value = Value{'R', std::pow(10.0, physTheta[(size_t)sl]), 0.0};
        } else if (gedge.kind == 'C') {
            g.value = Value{'C', std::pow(10.0, physTheta[(size_t)sl]), 0.0};
        } else {
            g.value = Value{'L', std::pow(10.0, physTheta[(size_t)sl]),
                            std::pow(10.0, physTheta[(size_t)sl + 1])};
        }
        const char* names[2] = {"v", "v"};
        if (gedge.kind == 'L') {
            names[0] = "L";
            names[1] = "Rd";
        }
        for (int j = 0; j < cnt; ++j) {
            if (weak[(size_t)sl + (size_t)j]) g.weakParams.push_back(names[j]);
            if (atBnd[(size_t)sl + (size_t)j]) g.atBound.push_back(names[j]);
        }
        groups.push_back(std::move(g));
        bool single = gedge.members.size() == 1;
        for (int m : gedge.members) {
            EdgeReport er;
            er.index = m;
            er.kind = std::get<2>(edges[(size_t)m]);
            er.status = single ? "fitted" : "merged";
            er.group = (int)gid;
            er.value = groups.back().value;
            if (!single)
                er.note = groups.back().mode + "-merged group g" + std::to_string(gid) +
                          ": only the aggregate is identifiable";
            edgesOut.push_back(std::move(er));
        }
    }
    for (const auto& [m, why] : red.dropped) {
        EdgeReport er;
        er.index = m;
        er.kind = std::get<2>(edges[(size_t)m]);
        er.status = "dropped";
        er.group = -1;
        er.note = why;
        edgesOut.push_back(std::move(er));
    }
    std::stable_sort(edgesOut.begin(), edgesOut.end(),
                     [](const EdgeReport& a, const EdgeReport& b) { return a.index < b.index; });
}

}  // namespace

FitResult fitGraph(const std::vector<double>& f, const std::vector<Complex>& z,
                   const std::vector<std::tuple<int, int, char>>& edges,
                   const FitConfig* config) {
    FitConfig cfg = config ? *config : FitConfig();
    auto tStart = std::chrono::steady_clock::now();

    ReductionResult red = reduceGraph(edges);
    NodalModel model = modelFromReduced(red);

    std::vector<double> omega(f.size());
    for (size_t k = 0; k < f.size(); ++k) omega[k] = 2.0 * kPi * f[k];
    double logSum = 0.0, logZSum = 0.0;
    for (double w : omega) logSum += std::log(w);
    for (const auto& zk : z) logZSum += std::log(std::abs(zk));
    double w0 = std::exp(logSum / (double)omega.size());
    double z0 = std::exp(logZSum / (double)z.size());
    std::vector<Complex> sT(f.size()), zT(z.size());
    for (size_t k = 0; k < f.size(); ++k) sT[k] = Complex(0.0, 1.0) * (omega[k] / w0);
    for (size_t k = 0; k < z.size(); ++k) zT[k] = z[k] / z0;

    std::vector<Unit> units = paramUnits(model);
    std::vector<double> scales = normScales(units, w0, z0);
    PhysBounds pb;
    std::vector<double> lb(units.size()), ub(units.size());
    for (size_t t = 0; t < units.size(); ++t) {
        double lo, hi;
        switch (units[t].letter) {
            case 'R': lo = pb.rLo; hi = pb.rHi; break;
            case 'C': lo = pb.cLo; hi = pb.cHi; break;
            case 'L': lo = pb.lLo; hi = pb.lHi; break;
            default: lo = pb.rdLo; hi = pb.rdHi; break;
        }
        lb[t] = std::log10(lo / scales[t]);
        ub[t] = std::log10(hi / scales[t]);
    }

    Objective obj{&model, sT, zT, defaultWeights(zT), {}, {}, {}, false};
    Rng rng(cfg.seed);
    auto wrmseOf = [&](double rssN) { return std::sqrt(rssN / (2.0 * (double)z.size())); };

    // R6: data-driven escalation threshold.  The configured escalationWrmse
    // (3%) sits far above the achievable floor for low-noise data (truth
    // wRMSE ~ 0.4% at 0.3% noise), so stages C/D stopped while the fit was
    // still 2-7x above the noise-level optimum.  When the noise floor can be
    // estimated from the data, tighten the stop threshold to 3x that floor
    // (never below 1e-4, never above the configured ceiling so very noisy
    // data does not escalate forever).
    double escWrmse = cfg.escalationWrmse;
    {
        double sHat = estimateRelativeNoise(omega, zT);
        if (sHat > 0.0)
            escWrmse = std::min(escWrmse, std::max(3.0 * sHat, 1e-4));
    }

    // stage A: unit + full-box LHS + center-focused LHS + resonance seeds
    std::vector<std::vector<double>> starts{unitStart(model, units)};
    {
        auto lhs = lhsStarts(cfg.nStarts, lb, ub, rng);
        for (auto& s : lhs) starts.push_back(std::move(s));
    }
    {
        auto c = lhsCenter(model, lb, ub, cfg.nCenter, rng);
        for (auto& s : c) starts.push_back(std::move(s));
    }
    {
        auto rs = resonanceStarts(model, units, lb, ub, sT, zT, starts[0]);
        for (auto& s : rs) starts.push_back(std::move(s));
    }
    {
        auto ts = topoResonanceStarts(model, units, lb, ub, sT, zT, starts[0]);
        for (auto& s : ts) starts.push_back(std::move(s));
    }
    std::vector<RssPoint> results = runStarts(obj, starts, lb, ub, cfg);
    int nUsed = (int)starts.size();

    // stage B: perturbation restarts around the incumbent
    if (!results.empty()) {
        auto ps = perturbStarts(results[0].x, lb, ub, cfg.nPerturb, rng);
        auto more = runStarts(obj, ps, lb, ub, cfg);
        nUsed += (int)ps.size();
        results.insert(results.end(), more.begin(), more.end());
        std::stable_sort(results.begin(), results.end(),
                         [](const RssPoint& a, const RssPoint& b) { return a.rss < b.rss; });
    }

    // stage B2: resonance-aligned (L, C) pair restarts around the incumbent
    if (!results.empty()) {
        auto rs = pairResonanceRestarts(model, units, lb, ub, sT, zT, results[0].x);
        if (!rs.empty()) {
            auto more = runStarts(obj, rs, lb, ub, cfg);
            nUsed += (int)rs.size();
            results.insert(results.end(), more.begin(), more.end());
            std::stable_sort(results.begin(), results.end(),
                             [](const RssPoint& a, const RssPoint& b) { return a.rss < b.rss; });
        }
    }

    if (results.empty()) throw std::runtime_error("all starts failed (numerical)");

    // stage C: escalation while still far above any plausible noise floor
    {
        bool centerNext = false;
        for (int round = 0; round < std::max(0, cfg.escalationRounds); ++round) {
            if (!results.empty() && wrmseOf(results[0].rss) <= escWrmse) break;
            std::vector<std::vector<double>> extra;
            {
                auto e2 = centerNext ? lhsCenter(model, lb, ub, cfg.nStarts + cfg.nCenter, rng)
                                     : lhsStarts(cfg.nStarts + cfg.nCenter, lb, ub, rng);
                for (auto& s : e2) extra.push_back(std::move(s));
            }
            auto rs = pairResonanceRestarts(model, units, lb, ub, sT, zT, results[0].x);
            for (auto& s : rs) extra.push_back(std::move(s));
            centerNext = !centerNext;
            auto more = runStarts(obj, extra, lb, ub, cfg);
            nUsed += (int)extra.size();
            results.insert(results.begin(), more.begin(), more.end());
            std::stable_sort(results.begin(), results.end(),
                             [](const RssPoint& a, const RssPoint& b) { return a.rss < b.rss; });
        }
    }

    // stage D: damped continuation rescue when still far above the floor
    if (wrmseOf(results[0].rss) > escWrmse) {
        const std::vector<double>* x0p[2] = {&results[0].x, nullptr};
        for (int qI = 0; qI < 2; ++qI) {
            std::vector<double> x0 = x0p[qI] ? *x0p[qI] : unitStart(model, units);
            auto [rssH, xH] = homotopyRescue(obj, model, lb, ub, x0);
            // R6 note: escWrmse can now be small enough that stage D runs on
            // data where homotopy overflows; never admit non-finite results
            // (a NaN rss would break the strict-weak-order of the sort)
            if (std::isfinite(rssH)) {
                results.push_back({rssH, xH});
                nUsed += 1;
            }
            std::stable_sort(results.begin(), results.end(),
                             [](const RssPoint& a, const RssPoint& b) { return a.rss < b.rss; });
            if (results.empty() || wrmseOf(results[0].rss) <= escWrmse) break;
        }
    }

    // stage E: last-resort mixed restarts (campaign hard cases)
    for (int round = 0; round < std::max(0, cfg.lastResortRounds); ++round) {
        if (wrmseOf(results[0].rss) <= escWrmse) break;
        std::vector<std::vector<double>> extra;
        {
            auto e2 = lhsStarts(cfg.lastResortBatch, lb, ub, rng);
            for (auto& s : e2) extra.push_back(std::move(s));
        }
        {
            auto e2 = lhsCenter(model, lb, ub, cfg.lastResortBatch, rng);
            for (auto& s : e2) extra.push_back(std::move(s));
        }
        for (double sig : {2.0, 1.0, 0.5}) {
            auto e2 = perturbStarts(results[0].x, lb, ub, cfg.lastResortBatch / 2, rng, sig);
            for (auto& s : e2) extra.push_back(std::move(s));
        }
        {
            auto e2 = pairResonanceRestarts(model, units, lb, ub, sT, zT, results[0].x);
            for (auto& s : e2) extra.push_back(std::move(s));
        }
        auto more = runStarts(obj, extra, lb, ub, cfg);
        nUsed += (int)extra.size();
        results.insert(results.begin(), more.begin(), more.end());
        std::stable_sort(results.begin(), results.end(),
                         [](const RssPoint& a, const RssPoint& b) { return a.rss < b.rss; });
    }

    // R6 note: drop any candidate whose parameters went non-finite (a finite
    // rss with a non-finite x would poison the final metrics)
    results.erase(std::remove_if(results.begin(), results.end(),
                                 [](const RssPoint& r) {
                                     for (double v : r.x)
                                         if (!std::isfinite(v)) return true;
                                     return !std::isfinite(r.rss);
                                 }),
                  results.end());

    // polish the best starts
    std::vector<std::vector<double>> topX;
    for (int i = 0; i < cfg.nPolish && i < (int)results.size(); ++i)
        topX.push_back(results[(size_t)i].x);
    auto [rssN, bestX] = polish(obj, topX, lb, ub, cfg);
    if (bestX.empty() || !std::isfinite(rssN) || rssN > results[0].rss) {
        rssN = results[0].rss;
        bestX = results[0].x;
    }
    // deep polish (C++ strengthening): chained tight restarts from the
    // incumbent, each re-initializing the damping -- narrow resonance
    // valleys need many more damped steps than the shared coarse/polish
    // budgets allow, and a restart can escape a stalled damping state
    for (int round = 0; round < 4; ++round) {
        auto [rssD, xD] = polish(obj, {bestX}, lb, ub, cfg, 8000);
        if (!xD.empty() && std::isfinite(rssD) && rssD < rssN * 0.999) {
            rssN = rssD;
            bestX = xD;
        } else {
            break;  // no further progress
        }
    }

    // R7: outlier-robust refit (single-pass IRLS).  A few wild measurement
    // points (probe glitches, EMI) bend a plain least-squares fit away from
    // the inlier core; e.g. one 30-sigma point on a 16-point sweep costs
    // ~7% wRMSE.  The robust scale is estimated per axis of the COMPLEX
    // relative residual (1.4826 x median|x| = sigma for a Gaussian axis), and
    // points whose residual magnitude exceeds kCut sigma (Rayleigh tail) are
    // quadratically downweighted before a re-polish.  Adopted only when the
    // downweighted objective improves, so outlier-free data is a no-op.
    {
        std::vector<double> wSave = obj.w;
        std::vector<Complex> zc, Jtmp2;
        model.zAndJac(bestX, sT, zc, Jtmp2);
        const size_t Mz = zT.size();
        if (Mz >= 8) {
            std::vector<double> magRe(Mz), magIm(Mz), mag(Mz);
            for (size_t k = 0; k < Mz; ++k) {
                Complex rr = (zT[k] - zc[k]) / zT[k];
                magRe[k] = std::fabs(rr.real());
                magIm[k] = std::fabs(rr.imag());
                mag[k] = std::abs(rr);
            }
            auto medOf = [](std::vector<double> v) {
                std::sort(v.begin(), v.end());
                return v[v.size() / 2];
            };
            // 1.4826 x median|x| = sigma for a zero-mean Gaussian axis
            double sigRe = 1.4826 * medOf(magRe);
            double sigIm = 1.4826 * medOf(magIm);
            double sigAxis = std::max(std::max(sigRe, sigIm), 1e-9);
            const double kCut = 5.0;   // Rayleigh-magnitude tail cut
            const double kIn = 2.5;    // core-member threshold
            bool anyOut = false;
            int nInlier = 0;
            int nOut = 0;
            for (size_t k = 0; k < Mz; ++k) {
                double m = mag[k] / sigAxis;
                if (m > kCut) {
                    double f = kCut / m;
                    obj.w[k] *= f * f;
                    anyOut = true;
                    ++nOut;
                } else if (m < kIn) {
                    ++nInlier;
                }
            }
            // engage only for a genuine "few outliers on an inlier core" shape
            if (anyOut && nOut <= (int)Mz / 3 && nInlier * 2 >= (int)Mz) {
                std::vector<double> rInc;
                obj.fun(bestX, rInc);
                double rssInc = rssOf(rInc);
                auto [rssR, xR] = polish(obj, {bestX}, lb, ub, cfg, 8000);
                bool finite = std::isfinite(rssR);
                for (double v : xR) finite = finite && std::isfinite(v);
                if (!xR.empty() && finite && rssR < rssInc * 0.999) {
                    bestX = xR;
                }
            }
        }
        obj.w = wSave;
    }

    {
        bool finite = !bestX.empty();
        for (double v : bestX) finite = finite && std::isfinite(v);
        if (!finite) {
            bestX = results[0].x;
            rssN = results[0].rss;
        }
    }

    // final metrics on physical z
    std::vector<Complex> ZfitT, Jtmp;
    model.zAndJac(bestX, sT, ZfitT, Jtmp);
    std::vector<Complex> zFit(z.size());
    for (size_t k = 0; k < z.size(); ++k) zFit[k] = z0 * ZfitT[k];
    auto [wrmse, maxRel] = fitMetrics(z, zFit);
    double rss = 0.0;
    for (size_t k = 0; k < z.size(); ++k) {
        double rel = std::abs((z[k] - zFit[k]) / z[k]);
        rss += rel * rel;
    }

    // diagnostics on physical elasticity
    std::vector<double> physTheta(bestX.size());
    for (size_t t = 0; t < bestX.size(); ++t)
        physTheta[t] = bestX[t] + std::log10(scales[t]);
    std::vector<Complex> E;
    model.elasticity(bestX, sT, E);
    std::vector<char> weak(bestX.size(), 0), atBnd(bestX.size(), 0);
    const int M = (int)sT.size();
    for (int t = 0; t < (int)bestX.size(); ++t) {
        double mx = 0.0;
        for (int m = 0; m < M; ++m) mx = std::max(mx, std::abs(E[(size_t)t * M + (size_t)m]));
        if (mx < cfg.visThreshold) weak[(size_t)t] = 1;
        atBnd[(size_t)t] =
            (std::fabs(bestX[(size_t)t] - lb[(size_t)t]) < 1e-9 ||
             std::fabs(ub[(size_t)t] - bestX[(size_t)t]) < 1e-9)
                ? 1
                : 0;
    }

    std::vector<double> Jw;
    obj.jacM(bestX, Jw);
    std::vector<double> sv = svdValues(Jw, 2 * (int)z.size(), (int)bestX.size());
    int rank = 0;
    double cond = std::numeric_limits<double>::infinity();
    if (!sv.empty()) {
        double tol = sv[0] * std::max(2 * (int)z.size(), (int)bestX.size()) * kEps;
        double cutoff = std::max(tol, sv[0] * 1e-6);
        for (double v : sv)
            if (v > cutoff) ++rank;
        if (rank == (int)sv.size()) cond = sv[0] / sv.back();
    }

    FitResult out;
    out.edges = edges;
    out.reduction = std::move(red);
    out.ok = true;
    out.rss = rss;
    out.wrmse = wrmse;
    out.maxRel = maxRel;
    out.aiccVal = aicc(rss, 2 * (int)z.size(), (int)bestX.size());
    out.nParams = model.nParams;
    out.thetaNorm = bestX;
    out.nStartsUsed = nUsed;
    out.jacSv = sv;
    out.jacRank = rank;
    out.jacCond = cond;
    out.model = std::move(model);
    out.w0 = w0;
    out.z0 = z0;
    buildReports(out.reduction, edges, out.model, physTheta, weak, atBnd,
                 out.groups, out.edgesOut);
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
    return out;
}

std::vector<FitResult> identifyMany(
    const std::vector<double>& f, const std::vector<Complex>& z,
    const std::vector<std::vector<std::tuple<int, int, char>>>& graphs,
    const FitConfig* config) {
    std::vector<FitResult> out;
    out.reserve(graphs.size());
    for (const auto& g : graphs) out.push_back(fitGraph(f, z, g, config));
    std::stable_sort(out.begin(), out.end(),
                     [](const FitResult& a, const FitResult& b) { return a.aiccVal < b.aiccVal; });
    return out;
}

}  // namespace tf
