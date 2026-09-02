#include "fit_engine_b.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rlc {

std::vector<Complex> RationalModel::zFit(const std::vector<Complex>& s) const {
    std::vector<Complex> out(s.size());
    for (size_t k = 0; k < s.size(); ++k) {
        out[k] = e * s[k] + d + (k0 != 0.0 ? k0 / s[k] : Complex(0.0, 0.0));
    }
    for (size_t i = 0; i < poles.size(); ++i) {
        for (size_t k = 0; k < s.size(); ++k) out[k] = out[k] + residues[i] / (s[k] - poles[i]);
    }
    return out;
}

void RationalModel::poleStructure(const std::vector<Complex>& s, int& degree, int& nPair,
                                  int& nReal) const {
    auto zf = zFit(s);
    double zmax = 0.0;
    for (const auto& v : zf) zmax = std::max(zmax, std::abs(v));
    nReal = 0;
    nPair = 0;
    std::vector<bool> seen(poles.size(), false);
    for (size_t i = 0; i < poles.size(); ++i) {
        if (seen[i]) continue;
        const Complex& p = poles[i];
        double contrib = 0.0;
        for (size_t k = 0; k < s.size(); ++k)
            contrib = std::max(contrib, std::abs(residues[i] / (s[k] - p)));
        if (std::abs(p.imag()) > 1e-9 * std::max(1.0, std::abs(p))) {
            // find the conjugate partner to count the pair once
            for (size_t j = i + 1; j < poles.size(); ++j) {
                if (std::abs(poles[j] - std::conj(p)) < 1e-6 * std::abs(p)) {
                    seen[j] = true;
                    for (size_t k = 0; k < s.size(); ++k)
                        contrib = std::max(contrib,
                                           std::abs(residues[j] / (s[k] - poles[j])));
                    break;
                }
            }
            if (contrib > kPoleSigRel * zmax) ++nPair;
        } else if (contrib > kPoleSigRel * zmax) {
            ++nReal;
        }
    }
    degree = nReal + 2 * nPair;
    if (std::abs(k0) > 0) ++degree;
    if (std::abs(e) > 0) ++degree;
}

namespace {

// ---- helpers --------------------------------------------------------------

using ComplexV = std::vector<Complex>;

// split into real poles and conjugate pairs (representative Im > 0); with
// optional residues carried alongside
struct SplitResult {
    std::vector<double> realPoles;             // p (negative reals usually)
    std::vector<double> realResidues;          // only when residues given
    std::vector<Complex> pairPoles;            // representative, Im > 0
    std::vector<Complex> pairResidues;         // only when residues given
    bool hasResidues = false;
};

SplitResult splitPoles(const ComplexV& poles, const ComplexV* residues) {
    SplitResult out;
    out.hasResidues = residues != nullptr;
    size_t n = poles.size();
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (poles[a].real() != poles[b].real()) return poles[a].real() < poles[b].real();
        return std::abs(poles[a].imag()) < std::abs(poles[b].imag());
    });
    std::vector<bool> used(n, false);
    for (size_t oi = 0; oi < n; ++oi) {
        size_t i = order[oi];
        if (used[i]) continue;
        Complex p = poles[i];
        Complex ri = residues ? (*residues)[i] : Complex(0.0, 0.0);
        if (std::abs(p.imag()) <= 1e-9 * std::max(1.0, std::abs(p))) {
            out.realPoles.push_back(p.real());
            if (residues) out.realResidues.push_back(ri.real());
            used[i] = true;
            continue;
        }
        // find the closest conjugate partner
        size_t bestJ = SIZE_MAX;
        double bestD = std::numeric_limits<double>::infinity();
        for (size_t j = 0; j < n; ++j) {
            if (j == i || used[j]) continue;
            double dd = std::abs(poles[j] - std::conj(p));
            if (dd < bestD) {
                bestD = dd;
                bestJ = j;
            }
        }
        if (bestJ == SIZE_MAX || bestD > 1e-6 * std::abs(p)) {
            out.realPoles.push_back(p.real());  // unpaired: keep real part
            if (residues) out.realResidues.push_back(ri.real());
            used[i] = true;
            continue;
        }
        used[i] = used[bestJ] = true;
        if (p.imag() < 0) {  // representative has Im > 0
            p = std::conj(p);
            if (residues) ri = std::conj(ri);
        }
        out.pairPoles.push_back(Complex(p.real(), std::abs(p.imag())));
        if (residues) out.pairResidues.push_back(ri);
    }
    return out;
}

// Data-driven resonance locations (in |x| units): interior |Z| extrema with
// prominence -- these are where the poles must live.
std::vector<double> featureCentersX(const ComplexV& x, const ComplexV& z) {
    const size_t m = z.size();
    std::vector<double> mag(m), ax(m);
    for (size_t k = 0; k < m; ++k) {
        mag[k] = std::abs(z[k]);
        ax[k] = std::abs(x[k]);
    }
    std::vector<double> centers;
    for (size_t i = 1; i + 1 < m; ++i) {
        double lo = std::min(mag[i - 1], mag[i + 1]);
        double hi = std::max(mag[i - 1], mag[i + 1]);
        if (mag[i] > 1.25 * hi || mag[i] < 0.8 * lo) centers.push_back(ax[i]);
    }
    // strongest extrema first (stable), deduplicated in log space
    std::stable_sort(centers.begin(), centers.end(), [&](double a, double b) {
        auto magAt = [&](double c) {
            size_t best = 0;
            double bd = std::numeric_limits<double>::infinity();
            for (size_t k = 0; k < m; ++k) {
                double d = std::fabs(ax[k] - c);
                if (d < bd) {
                    bd = d;
                    best = k;
                }
            }
            return std::fabs(std::log10(mag[best]));
        };
        return magAt(a) > magAt(b);
    });
    std::vector<double> out;
    for (double c : centers) {
        bool dup = false;
        for (double o : out) {
            if (std::fabs(std::log10(c) - std::log10(o)) <= 0.15) {
                dup = true;
                break;
            }
        }
        if (!dup) out.push_back(c);
        if (out.size() >= 4) break;
    }
    return out;
}

// Initial pole sets (normalized x units) for the SK/VF iteration:
// vector-fitting style log-spaced lightly damped pole sets covering the band,
// seeded at data-driven resonance locations.
ComplexV pairSet(const std::vector<double>& centers, double extraReal, int n) {
    const double xi = 0.1;  // light relative damping of the starting poles
    ComplexV poles;
    for (double c : centers) {
        poles.push_back(Complex(-xi * c, c));
        poles.push_back(Complex(-xi * c, -c));
    }
    if (std::isfinite(extraReal)) poles.push_back(Complex(-extraReal, 0.0));
    if ((int)poles.size() > n) poles.resize((size_t)n);
    return poles;
}

std::vector<ComplexV> initialPolesX(int n, double xlo, double xhi,
                                    const std::vector<double>& hints) {
    int nPairs = n / 2;
    double mid = std::sqrt(xlo * xhi);

    // pool of candidate pair centers: resonance hints + coarse log grid
    std::vector<double> pool = hints;
    auto grid = geomspace(xlo, xhi, 5);
    for (int i = 1; i <= 3 && i < (int)grid.size() - 1; ++i) pool.push_back(grid[i]);
    pool.push_back(mid);
    std::sort(pool.begin(), pool.end());
    std::vector<double> dedup;
    for (double c : pool) {
        if (dedup.empty() ||
            std::fabs(std::log10(c) - std::log10(dedup.back())) > 0.1)
            dedup.push_back(c);
    }
    if (dedup.size() > 6) dedup.resize(6);
    pool = dedup;

    std::vector<ComplexV> inits;
    bool extra = (n % 2 == 1);
    double extraReal = extra ? mid : std::numeric_limits<double>::quiet_NaN();

    if (nPairs >= 1) {
        int r = std::min(nPairs, (int)pool.size());
        // combinations of pool entries of size r, lexicographic by index
        std::vector<int> combo;
        std::function<void(int, int)> rec = [&](int start, int depth) {
            if (depth == r) {
                std::vector<double> centers;
                centers.reserve(combo.size());
                for (int idx : combo) centers.push_back(pool[idx]);
                if ((int)centers.size() < nPairs)
                    centers.insert(centers.end(), nPairs - centers.size(), mid);
                inits.push_back(pairSet(centers, extraReal, n));
                return;
            }
            for (int i = start; i < (int)pool.size(); ++i) {
                combo.push_back(i);
                rec(i + 1, depth + 1);
                combo.pop_back();
            }
        };
        rec(0, 0);
        if ((int)pool.size() < nPairs)
            inits.push_back(pairSet(std::vector<double>(nPairs, mid), extraReal, n));
    }
    if (n % 2 == 1 || inits.empty()) {
        // purely real log-spaced poles (covers odd orders / RC-RL cases)
        auto g = geomspace(xlo, xhi, n);
        ComplexV poles;
        for (double v : g) poles.push_back(Complex(-v, 0.0));
        inits.push_back(std::move(poles));
    }
    return inits;
}

// Real-coefficient partial-fraction basis for the given poles (x-domain):
// one column 1/(x-p) per real pole and two columns 2(x+alpha)/D2, -2 beta/D2
// per conjugate pair.
struct PoleBasis {
    std::vector<ComplexV> cols;
    std::vector<double> reals;
    std::vector<Complex> pairs;
};

PoleBasis poleBasis(const ComplexV& x, const ComplexV& poles) {
    SplitResult sp = splitPoles(poles, nullptr);
    PoleBasis pb;
    pb.reals = sp.realPoles;
    pb.pairs = sp.pairPoles;
    for (double p : sp.realPoles) {
        ComplexV col(x.size());
        for (size_t k = 0; k < x.size(); ++k) col[k] = Complex(1.0, 0.0) / (x[k] - p);
        pb.cols.push_back(std::move(col));
    }
    for (const auto& p : sp.pairPoles) {
        double alpha = -p.real(), beta = std::abs(p.imag());
        ComplexV c1(x.size()), c2(x.size());
        for (size_t k = 0; k < x.size(); ++k) {
            Complex d2 = (x[k] + alpha) * (x[k] + alpha) + Complex(beta * beta, 0.0);
            c1[k] = 2.0 * (x[k] + alpha) / d2;
            c2[k] = -2.0 * beta / d2;
        }
        pb.cols.push_back(std::move(c1));
        pb.cols.push_back(std::move(c2));
    }
    return pb;
}

// Zeros of sigma(x) = 1 + sum_j c~_j/(x - p_j) as roots of
// prod(x-p) + sum_j c~_j prod_{i!=j}(x-p_i) (real coefficients).
ComplexV sigmaZeros(const std::vector<double>& reals, const std::vector<Complex>& pairs,
                    const std::vector<double>& ctilde) {
    std::vector<Complex> polesAll;
    std::vector<Complex> cAll;
    size_t k = 0;
    for (double p : reals) {
        polesAll.push_back(Complex(p, 0.0));
        cAll.push_back(Complex(ctilde[k], 0.0));
        ++k;
    }
    for (const auto& p : pairs) {
        double cr = ctilde[k], ci = ctilde[k + 1];
        k += 2;
        polesAll.push_back(p);
        polesAll.push_back(std::conj(p));
        cAll.push_back(Complex(cr, ci));
        cAll.push_back(Complex(cr, -ci));
    }
    std::vector<Complex> num = polyFromRoots(polesAll);
    for (size_t j = 0; j < cAll.size(); ++j) {
        std::vector<Complex> rest;
        rest.reserve(polesAll.size() - 1);
        for (size_t i = 0; i < polesAll.size(); ++i)
            if (i != j) rest.push_back(polesAll[i]);
        std::vector<Complex> lower = polyFromRoots(rest);
        // np.pad(lower, (len(num) - len(lower), 0)) -- left zero padding
        std::vector<Complex> padded(num.size(), Complex(0.0, 0.0));
        for (size_t i = 0; i < lower.size(); ++i)
            padded[padded.size() - lower.size() + i] = lower[i];
        for (size_t i = 0; i < num.size(); ++i) num[i] += cAll[j] * padded[i];
    }
    std::vector<double> realPart(num.size());
    for (size_t i = 0; i < num.size(); ++i) realPart[i] = num[i].real();
    return polyRoots(realPart);
}

// One SK iteration in pole basis (= vector-fitting pole relocation):
// solve min |sigma*z - (e x + d + sum r B)|^2 with fixed weights,
// sigma = 1 + sum c~ B, then relocate poles to the zeros of sigma.
ComplexV vfStep(const ComplexV& x, const ComplexV& z, const std::vector<double>& wgt,
                const ComplexV& poles) {
    PoleBasis pb = poleBasis(x, poles);
    std::vector<ComplexV> cols{ComplexV(x)};
    {
        ComplexV ones(x.size(), Complex(1.0, 0.0));
        cols.push_back(std::move(ones));
    }
    for (auto& c : pb.cols) cols.push_back(c);
    for (const auto& b : pb.cols) {
        ComplexV c(x.size());
        for (size_t k = 0; k < x.size(); ++k) c[k] = -z[k] * b[k];
        cols.push_back(std::move(c));
    }
    size_t m = x.size(), ncols = cols.size();
    std::vector<double> A(2 * m * ncols), yv(2 * m);
    for (size_t k = 0; k < m; ++k) {
        for (size_t j = 0; j < ncols; ++j) {
            Complex v = wgt[k] * cols[j][k];
            A[k * ncols + j] = v.real();
            A[(m + k) * ncols + j] = v.imag();
        }
        Complex yy = wgt[k] * z[k];
        yv[k] = yy.real();
        yv[m + k] = yy.imag();
    }
    std::vector<double> sol = lstsqScaled(A, yv, (int)(2 * m), (int)ncols);
    size_t nb = pb.cols.size();
    std::vector<double> ctilde(nb);
    for (size_t i = 0; i < nb; ++i) ctilde[i] = sol[2 + nb + i];
    ComplexV newPoles = sigmaZeros(pb.reals, pb.pairs, ctilde);
    for (auto& p : newPoles) {
        if (p.real() > 0) p = Complex(-p.real(), p.imag());
    }
    return newPoles;
}

// True weighted RSS of the best e*x + d + pole-residue model at the given
// poles (honest convergence metric for the relocation loop).
double quickScore(const ComplexV& x, const ComplexV& z, const std::vector<double>& w,
                  const ComplexV& poles) {
    PoleBasis pb = poleBasis(x, poles);
    std::vector<ComplexV> cols{ComplexV(x)};
    {
        ComplexV ones(x.size(), Complex(1.0, 0.0));
        cols.push_back(std::move(ones));
    }
    for (auto& c : pb.cols) cols.push_back(c);
    size_t m = x.size(), ncols = cols.size();
    std::vector<double> A(2 * m * ncols), yv(2 * m);
    for (size_t k = 0; k < m; ++k) {
        for (size_t j = 0; j < ncols; ++j) {
            Complex v = w[k] * cols[j][k];
            A[k * ncols + j] = v.real();
            A[(m + k) * ncols + j] = v.imag();
        }
        Complex yy = w[k] * z[k];
        yv[k] = yy.real();
        yv[m + k] = yy.imag();
    }
    std::vector<double> sol = lstsqScaled(A, yv, (int)(2 * m), (int)ncols);
    double rss = 0.0;
    for (size_t i = 0; i < yv.size(); ++i) {
        double pred = 0.0;
        for (size_t j = 0; j < ncols; ++j) pred += A[i * ncols + j] * sol[j];
        double rr = yv[i] - pred;
        rss += rr * rr;
    }
    return rss;
}

// SK iteration at fixed order n; returns the fitted poles (s-domain).
ComplexV skFitOrder(const ComplexV& s, const ComplexV& z, const std::vector<double>& w,
                    int n, double omega0, int nIters) {
    if (n == 0) return {};
    ComplexV x(s.size());
    for (size_t k = 0; k < s.size(); ++k) x[k] = s[k] / omega0;
    double xlo = std::numeric_limits<double>::infinity(), xhi = 0.0;
    for (const auto& v : x) {
        xlo = std::min(xlo, std::abs(v));
        xhi = std::max(xhi, std::abs(v));
    }
    std::vector<double> hints = featureCentersX(x, z);
    ComplexV best;
    double bestRss = std::numeric_limits<double>::infinity();
    for (const auto& init : initialPolesX(n, xlo, xhi, hints)) {
        ComplexV poles = init;
        for (int it = 0; it < std::max(nIters, 1); ++it) {
            poles = vfStep(x, z, w, poles);
            double score = quickScore(x, z, w, poles);
            if (score < bestRss) {
                bestRss = score;
                best = poles;
            }
        }
    }
    for (auto& p : best) p = p * omega0;
    return best;
}

// Linear weighted re-estimation of e, d, k0 and residues (poles fixed).
RationalModel refitResidues(const ComplexV& s, const ComplexV& z,
                            const std::vector<double>& w, const ComplexV& polesIn,
                            int order, double omega0, std::pair<double, double> wBand) {
    double wmin = wBand.first, wmax = wBand.second;
    // snap band-exterior poles
    ComplexV kept;
    for (const auto& p : polesIn) {
        double ap = std::abs(p);
        if (ap >= kPoleZeroRel * wmin && ap <= kPoleInfRel * wmax) kept.push_back(p);
    }
    SplitResult sp = splitPoles(kept, nullptr);

    std::vector<ComplexV> cols;
    {
        cols.push_back(s);
        cols.push_back(ComplexV(s.size(), Complex(1.0, 0.0)));
        ComplexV inv(s.size());
        for (size_t k = 0; k < s.size(); ++k) inv[k] = Complex(1.0, 0.0) / s[k];
        cols.push_back(std::move(inv));
    }
    for (double p : sp.realPoles) {
        ComplexV c(s.size());
        for (size_t k = 0; k < s.size(); ++k) c[k] = Complex(1.0, 0.0) / (s[k] - p);
        cols.push_back(std::move(c));
    }
    for (const auto& p : sp.pairPoles) {
        double alpha = -p.real(), beta = std::abs(p.imag());
        ComplexV c1(s.size()), c2(s.size());
        for (size_t k = 0; k < s.size(); ++k) {
            Complex d2 = (s[k] + alpha) * (s[k] + alpha) + Complex(beta * beta, 0.0);
            c1[k] = 2.0 * (s[k] + alpha) / d2;
            c2[k] = -2.0 * beta / d2;
        }
        cols.push_back(std::move(c1));
        cols.push_back(std::move(c2));
    }
    size_t m = s.size(), ncols = cols.size();
    std::vector<double> A(2 * m * ncols), yv(2 * m);
    for (size_t k = 0; k < m; ++k) {
        for (size_t j = 0; j < ncols; ++j) {
            Complex v = w[k] * cols[j][k];
            A[k * ncols + j] = v.real();
            A[(m + k) * ncols + j] = v.imag();
        }
        Complex yy = w[k] * z[k];
        yv[k] = yy.real();
        yv[m + k] = yy.imag();
    }
    std::vector<double> sol = lstsqScaled(A, yv, (int)(2 * m), (int)ncols);

    double e = sol[0], d = sol[1], k0 = sol[2];
    size_t idx = 3;
    std::vector<Complex> poleList, resList;
    for (double p : sp.realPoles) {
        poleList.push_back(Complex(p, 0.0));
        resList.push_back(Complex(sol[idx], 0.0));
        ++idx;
    }
    for (const auto& p : sp.pairPoles) {
        double rr = sol[idx], ri = sol[idx + 1];
        idx += 2;
        poleList.push_back(p);
        poleList.push_back(std::conj(p));
        resList.push_back(Complex(rr, ri));
        resList.push_back(Complex(rr, -ri));
    }

    // drop insignificant terms (near pole-zero cancellation, section 6.1)
    ComplexV zfit0(m);
    for (size_t k = 0; k < m; ++k)
        zfit0[k] = e * s[k] + d + (k0 != 0.0 ? k0 / s[k] : Complex(0.0, 0.0));
    for (size_t i = 0; i < poleList.size(); ++i) {
        for (size_t k = 0; k < m; ++k)
            zfit0[k] = zfit0[k] + resList[i] / (s[k] - poleList[i]);
    }
    double zmax = 0.0;
    for (const auto& v : zfit0) zmax = std::max(zmax, std::abs(v));
    double rssFull = 0.0;
    for (size_t k = 0; k < m; ++k) {
        Complex rr = w[k] * (z[k] - zfit0[k]);
        rssFull += rr.real() * rr.real() + rr.imag() * rr.imag();
    }
    double sigmaHat =
        std::sqrt(rssFull / std::max(2.0 * (double)m - (double)sol.size(), 1.0));
    std::vector<double> noiseFloor(m);
    for (size_t k = 0; k < m; ++k)
        noiseFloor[k] = kTermSnr * sigmaHat / std::max(w[k], 1e-300);

    std::vector<std::vector<double>> termArrays;
    {
        std::vector<double> t0(m), t1(m, std::abs(d)), t2(m);
        for (size_t k = 0; k < m; ++k) {
            t0[k] = std::abs(e * s[k]);
            t2[k] = std::abs(k0 / s[k]);
        }
        termArrays.push_back(std::move(t0));
        termArrays.push_back(std::move(t1));
        termArrays.push_back(std::move(t2));
    }
    for (size_t i = 0; i < poleList.size(); ++i) {
        std::vector<double> t(m);
        for (size_t k = 0; k < m; ++k)
            t[k] = std::abs(resList[i] / (s[k] - poleList[i]));
        termArrays.push_back(std::move(t));
    }

    auto significant = [&](size_t i) {
        const auto& t = termArrays[i];
        double tmax = 0.0;
        for (double v : t) tmax = std::max(tmax, v);
        if (zmax > 0 && tmax < kTermDropRel * zmax) return false;
        for (size_t k = 0; k < m; ++k)
            if (t[k] > noiseFloor[k]) return true;
        return false;
    };

    if (!significant(0)) e = 0.0;
    if (!significant(1)) d = 0.0;
    if (!significant(2)) k0 = 0.0;
    std::vector<Complex> keepPoles, keepRes;
    for (size_t i = 0; i < poleList.size(); ++i) {
        if (!significant(3 + i)) continue;
        keepPoles.push_back(poleList[i]);
        keepRes.push_back(resList[i]);
    }

    int nKept = (e != 0.0 ? 1 : 0) + (d != 0.0 ? 1 : 0) + (k0 != 0.0 ? 1 : 0) +
                (int)keepPoles.size();
    RationalModel model;
    model.order = order;
    model.omega0 = omega0;
    model.e = e;
    model.d = d;
    model.k0 = k0;
    model.poles = keepPoles;
    model.residues = keepRes;
    model.nUnknowns = nKept;
    ComplexV zf(m);
    for (size_t k = 0; k < m; ++k)
        zf[k] = e * s[k] + d + (k0 != 0.0 ? k0 / s[k] : Complex(0.0, 0.0));
    for (size_t i = 0; i < keepPoles.size(); ++i) {
        for (size_t k = 0; k < m; ++k)
            zf[k] = zf[k] + keepRes[i] / (s[k] - keepPoles[i]);
    }
    double rss = 0.0;
    for (size_t k = 0; k < m; ++k) {
        Complex rr = w[k] * (z[k] - zf[k]);
        rss += rr.real() * rr.real() + rr.imag() * rr.imag();
    }
    model.rss = rss;
    model.aicc = aicc(rss, (int)(2 * m), nKept);
    return model;
}

// ---- Foster synthesis (section 6.2 mapping tables, decision D8) -----------

struct FosterSections {
    bool ok = true;  // false -> whole candidate skipped (D8)
    std::vector<Assembled> sections;
    std::vector<std::string> notes;
};

FosterSections fosterSections(const RationalModel& model, const ComplexV& /*sData*/,
                              const ComplexV& zData, bool admittance, double sigmaHat) {
    double zmax = 0.0, zmin = std::numeric_limits<double>::infinity();
    for (const auto& v : zData) {
        zmax = std::max(zmax, std::abs(v));
        zmin = std::min(zmin, std::abs(v));
    }
    FosterSections out;

    if (model.e != 0.0) {
        if (model.e > 0) {
            if (admittance)
                out.sections.push_back(Assembled{Tree::makeLeaf('C'), {model.e}});
            else
                out.sections.push_back(Assembled{Tree::makeLeaf('L'), {model.e}});
        } else {
            out.ok = false;
            char buf[160];
            std::snprintf(buf, sizeof(buf), "e=%s term negative (%.3g), skipped (D8)",
                          admittance ? "admittance" : "impedance", model.e);
            out.notes.push_back(buf);
            return out;
        }
    }
    if (model.k0 != 0.0) {
        if (model.k0 > 0) {
            if (admittance)
                out.sections.push_back(Assembled{Tree::makeLeaf('L'), {1.0 / model.k0}});
            else
                out.sections.push_back(Assembled{Tree::makeLeaf('C'), {1.0 / model.k0}});
        } else {
            out.ok = false;
            char buf[96];
            std::snprintf(buf, sizeof(buf), "k0 term negative (%.3g), skipped (D8)", model.k0);
            out.notes.push_back(buf);
            return out;
        }
    }
    if (model.d != 0.0) {
        if (model.d > 0) {
            if (admittance)
                out.sections.push_back(Assembled{Tree::makeLeaf('R'), {1.0 / model.d}});
            else
                out.sections.push_back(Assembled{Tree::makeLeaf('R'), {model.d}});
        } else {
            out.ok = false;
            char buf[96];
            std::snprintf(buf, sizeof(buf), "d term negative (%.3g), skipped (D8)", model.d);
            out.notes.push_back(buf);
            return out;
        }
    }

    SplitResult sp = splitPoles(model.poles, &model.residues);

    for (size_t i = 0; i < sp.realPoles.size(); ++i) {
        double a = -sp.realPoles[i];
        double rho = sp.realResidues[i];
        if (a <= 0) {
            out.ok = false;
            char buf[96];
            std::snprintf(buf, sizeof(buf), "real pole at +|a| (a=%.3g), skipped (D8)", a);
            out.notes.push_back(buf);
            return out;
        }
        if (admittance) {
            // rho'/(s+a') in Y -> series R-L branch: L = 1/rho', R = a'/rho'
            if (rho <= 0) {
                out.ok = false;
                char buf[96];
                std::snprintf(buf, sizeof(buf), "Y real-pole residue %.3g <= 0, skipped (D8)",
                              rho);
                out.notes.push_back(buf);
                return out;
            }
            double lv = 1.0 / rho, rv = a / rho;
            if (rv < kBranchRShortRel * zmin) {
                out.notes.push_back("branch R below band floor, dropped (short)");
                out.sections.push_back(Assembled{Tree::makeLeaf('L'), {lv}});
            } else {
                out.sections.push_back(assemble(
                    NK::Ser, {Assembled{Tree::makeLeaf('R'), {rv}},
                              Assembled{Tree::makeLeaf('L'), {lv}}}));
            }
        } else {
            // rho/(s+a) in Z -> series R||C section: C = 1/rho, R = rho/a
            if (rho <= 0) {
                out.ok = false;
                char buf[96];
                std::snprintf(buf, sizeof(buf), "Z real-pole residue %.3g <= 0, skipped (D8)",
                              rho);
                out.notes.push_back(buf);
                return out;
            }
            double cv = 1.0 / rho, rv = rho / a;
            out.sections.push_back(assemble(
                NK::Par, {Assembled{Tree::makeLeaf('R'), {rv}},
                          Assembled{Tree::makeLeaf('C'), {cv}}}));
        }
    }

    for (size_t i = 0; i < sp.pairPoles.size(); ++i) {
        Complex p = sp.pairPoles[i];
        Complex rho = sp.pairResidues[i];
        double alpha = p.real(), beta = std::abs(p.imag());
        if (alpha > 0) {
            out.ok = false;
            char buf[96];
            std::snprintf(buf, sizeof(buf), "unstable pole pair (alpha=%.3g), skipped (D8)",
                          alpha);
            out.notes.push_back(buf);
            return out;
        }
        alpha = -alpha;  // damping >= 0 after flipping
        double rhoR = rho.real(), rhoI = rho.imag();
        double cConst = rhoR * alpha - rhoI * beta;
        double om = std::hypot(alpha, beta);
        if (rhoR <= 0) {
            out.ok = false;
            char buf[96];
            std::snprintf(buf, sizeof(buf), "pair residue rho_r=%.3g <= 0, skipped (D8)", rhoR);
            out.notes.push_back(buf);
            return out;
        }
        // D8: c is treated as zero when within the estimated noise floor
        double cTol = std::max(kCPairTol, 3.0 * sigmaHat) * std::abs(rhoR) * om;
        if (std::abs(cConst) > cTol) {
            out.ok = false;
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "complex pair with c=%.3g != 0 (needs Bott-Duffin/bridge), skipped (D8)",
                          cConst);
            out.notes.push_back(buf);
            return out;
        }
        if (admittance) {
            // series R-L-C branch
            double lv = 1.0 / (2.0 * rhoR);
            double cv = 2.0 * rhoR / (om * om);
            double rv = alpha / rhoR;
            if (rv < kBranchRShortRel * zmin) {
                out.notes.push_back("branch R below band floor, dropped (short)");
                out.sections.push_back(assemble(
                    NK::Ser, {Assembled{Tree::makeLeaf('L'), {lv}},
                              Assembled{Tree::makeLeaf('C'), {cv}}}));
            } else {
                out.sections.push_back(assemble(
                    NK::Ser, {Assembled{Tree::makeLeaf('R'), {rv}},
                              Assembled{Tree::makeLeaf('L'), {lv}},
                              Assembled{Tree::makeLeaf('C'), {cv}}}));
            }
        } else {
            // parallel R||L||C tank
            double cv = 1.0 / (2.0 * rhoR);
            double lv = 2.0 * rhoR / (om * om);
            double rv = (alpha == 0.0) ? std::numeric_limits<double>::infinity()
                                       : rhoR / alpha;
            if (rv > kTankROpenRel * zmax) {
                out.notes.push_back("tank parallel R above band ceiling, dropped "
                                    "(lossless section)");
                out.sections.push_back(assemble(
                    NK::Par, {Assembled{Tree::makeLeaf('L'), {lv}},
                              Assembled{Tree::makeLeaf('C'), {cv}}}));
            } else {
                out.sections.push_back(assemble(
                    NK::Par, {Assembled{Tree::makeLeaf('R'), {rv}},
                              Assembled{Tree::makeLeaf('L'), {lv}},
                              Assembled{Tree::makeLeaf('C'), {cv}}}));
            }
        }
    }
    return out;
}

std::optional<Candidate> sectionsToCandidate(std::vector<Assembled> sections, NK rootKind,
                                             const ComplexV& s, const ComplexV& z,
                                             const std::vector<double>& w,
                                             const std::string& note) {
    if (sections.empty()) return std::nullopt;
    Assembled assembled;
    if (sections.size() == 1) {
        assembled = sections[0];
    } else {
        assembled = assemble(rootKind, std::move(sections));
    }
    std::vector<double> theta(assembled.values.size());
    for (size_t i = 0; i < assembled.values.size(); ++i)
        theta[i] = std::log10(assembled.values[i]);
    std::vector<double> residual = residualVector(assembled.tree, theta, s, z, w);
    double rss = rssOf(residual);
    std::vector<Complex> zfit(z.size());
    evalTheta(assembled.tree, theta, s.data(), z.size(), zfit.data());
    auto [wrmse, emax] = fitMetrics(z, zfit);
    Candidate c;
    c.tree = assembled.tree;
    c.theta = std::move(theta);
    c.rss = rss;
    c.aiccVal = aicc(rss, (int)(2 * z.size()), (int)c.theta.size());
    c.wrmse = wrmse;
    c.maxRelErr = emax;
    c.engine = "B";
    c.note = note;
    return c;
}

}  // namespace

RationalModel skRationalFit(const std::vector<double>& w, const ComplexV& z,
                            const std::vector<double>& wts, int maxOrder, int nIters) {
    const size_t m = z.size();
    ComplexV s(m);
    double logSum = 0.0;
    for (size_t k = 0; k < m; ++k) {
        s[k] = Complex(0.0, 1.0) * w[k];
        logSum += std::log(w[k]);
    }
    double omega0 = std::exp(logSum / (double)m);
    double wmin = std::numeric_limits<double>::infinity(), wmax = 0.0;
    for (double v : w) {
        wmin = std::min(wmin, v);
        wmax = std::max(wmax, v);
    }
    // RSS floor at relative machine precision
    double sumWz2 = 0.0;
    for (size_t k = 0; k < m; ++k) {
        Complex v = wts[k] * z[k];
        sumWz2 += v.real() * v.real() + v.imag() * v.imag();
    }
    double rssFloor = (1e-13) * (1e-13) * sumWz2;

    std::vector<RationalModel> models;
    models.reserve(maxOrder + 1);
    for (int n = 0; n <= maxOrder; ++n) {
        ComplexV poles = skFitOrder(s, z, wts, n, omega0, nIters);
        RationalModel model = refitResidues(s, z, wts, poles, n, omega0, {wmin, wmax});
        model.rss = std::max(model.rss, rssFloor);
        model.selAicc = aicc(model.rss, (int)(2 * m), model.nUnknowns);
        models.push_back(std::move(model));
    }

    // order selection by the discrepancy principle (D10)
    int nObs = 2 * (int)m;
    double sigma2Hat = std::numeric_limits<double>::infinity();
    for (const auto& mm : models) {
        double pd = mm.rss / std::max((double)(nObs - mm.nUnknowns), 1.0);
        sigma2Hat = std::min(sigma2Hat, pd);
    }
    double margin = 3.0 * std::sqrt(2.0 / (double)nObs);
    double threshold = sigma2Hat * (1.0 + margin);
    RationalModel best = models[0];
    bool found = false;
    for (const auto& mm : models) {  // ascending order: first adequate
        if (mm.rss / std::max((double)(nObs - mm.nUnknowns), 1.0) <= threshold) {
            best = mm;
            found = true;
            break;
        }
    }
    if (!found) {
        double bestA = std::numeric_limits<double>::infinity();
        for (const auto& mm : models) {
            if (mm.selAicc < bestA) {
                bestA = mm.selAicc;
                best = mm;
            }
        }
    }
    best.alternatives = models;
    return best;
}

int conservativeEnergyBound(const RationalModel& model, const ComplexV& s, double deltaAicc) {
    // pool = alternatives when present, otherwise the model itself (the
    // alternatives list always contains the selected model as well)
    std::vector<const RationalModel*> pool;
    if (!model.alternatives.empty()) {
        for (const auto& m : model.alternatives) pool.push_back(&m);
    } else {
        pool.push_back(&model);
    }
    double bestSel = std::numeric_limits<double>::infinity();
    for (const auto* m : pool) bestSel = std::min(bestSel, m->selAicc);
    int bound = std::numeric_limits<int>::max();
    bool any = false;
    for (const auto* m : pool) {
        if (m->selAicc <= bestSel + deltaAicc) {
            int degree, nPair, nReal;
            m->poleStructure(s, degree, nPair, nReal);
            bound = std::min(bound, nReal + 2 * nPair);
            any = true;
        }
    }
    return any ? bound : 0;
}

FosterResult fosterCandidates(const std::vector<double>& w, const ComplexV& z,
                              const std::vector<double>& wts, int maxOrder, int nIters) {
    const size_t m = z.size();
    ComplexV s(m);
    for (size_t k = 0; k < m; ++k) s[k] = Complex(0.0, 1.0) * w[k];

    FosterResult result;
    result.zModel = skRationalFit(w, z, wts, maxOrder, nIters);
    ComplexV zInv(m);
    std::vector<double> wtsY(m);
    for (size_t k = 0; k < m; ++k) {
        zInv[k] = Complex(1.0, 0.0) / z[k];
        double az = std::abs(z[k]);
        wtsY[k] = wts[k] * az * az;
    }
    result.yModel = skRationalFit(w, zInv, wtsY, maxOrder, nIters);

    struct Job {
        const RationalModel* model;
        bool admittance;
        NK root;
        const char* name;
    };
    Job jobs[2] = {{&result.zModel, false, NK::Ser, "Foster-I"},
                   {&result.yModel, true, NK::Par, "Foster-II"}};
    for (const auto& job : jobs) {
        double sigmaHat =
            std::sqrt(job.model->rss /
                      std::max(2.0 * (double)m - (double)job.model->nUnknowns, 1.0));
        FosterSections fs = fosterSections(*job.model, s, z, job.admittance, sigmaHat);
        if (!fs.ok) {
            Candidate c;
            c.tree = Tree::makeLeaf('R');
            c.theta = {0.0};
            c.rss = std::numeric_limits<double>::infinity();
            c.aiccVal = std::numeric_limits<double>::infinity();
            c.wrmse = std::numeric_limits<double>::infinity();
            c.maxRelErr = std::numeric_limits<double>::infinity();
            c.engine = "B";
            std::string note = std::string(job.name) + ": ";
            for (size_t i = 0; i < fs.notes.size(); ++i) {
                if (i) note += "; ";
                note += fs.notes[i];
            }
            c.note = note;
            c.skipped = true;
            result.candidates.push_back(std::move(c));
            continue;
        }
        auto cand = sectionsToCandidate(fs.sections, job.root, s, z, wts, job.name);
        if (!cand.has_value()) continue;
        // D8 validation: poor synthesis fit -> skipped, not an invalid result
        if (cand->wrmse > 0.05) {
            Candidate c = *cand;
            c.rss = std::numeric_limits<double>::infinity();
            c.aiccVal = std::numeric_limits<double>::infinity();
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "%s: synthesis mismatch (wRMSE=%.2g), skipped (D8)", job.name,
                          cand->wrmse);
            c.note = buf;
            c.skipped = true;
            result.candidates.push_back(std::move(c));
            continue;
        }
        if (!fs.notes.empty()) {
            std::string note = std::string(job.name) + ": ";
            for (size_t i = 0; i < fs.notes.size(); ++i) {
                if (i) note += "; ";
                note += fs.notes[i];
            }
            cand->note = note;
        }
        result.candidates.push_back(std::move(*cand));
    }
    return result;
}

}  // namespace rlc
