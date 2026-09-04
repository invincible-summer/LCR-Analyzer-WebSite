#include "metric.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace tf {

std::vector<double> defaultWeights(const std::vector<Complex>& z) {
    std::vector<double> w(z.size());
    for (size_t k = 0; k < z.size(); ++k) w[k] = 1.0 / std::abs(z[k]);
    return w;
}

std::vector<double> residualVector(const std::vector<Complex>& z,
                                   const std::vector<Complex>& zModel,
                                   const std::vector<double>& w) {
    std::vector<double> out(2 * z.size());
    for (size_t k = 0; k < z.size(); ++k) {
        Complex r = w[k] * (z[k] - zModel[k]);
        out[2 * k] = r.real();
        out[2 * k + 1] = r.imag();
    }
    return out;
}

double rssOf(const std::vector<double>& residual) {
    double s = 0.0;
    for (double v : residual) s += v * v;
    return s;
}

double estimateRelativeNoise(const std::vector<double>& w,
                             const std::vector<Complex>& z) {
    (void)w;
    const size_t m = z.size();
    if (m < 7) return -1.0;
    std::vector<double> y(m), ph(m);
    for (size_t k = 0; k < m; ++k) {
        y[k] = std::log(std::max(std::abs(z[k]), 1e-300));
        ph[k] = std::atan2(z[k].imag(), z[k].real());
    }
    // 5-point quadratic detrending (2 residual dof; orthogonal basis {1, t,
    // t^2-2} on t in {-2..2}), median over windows
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
        std::vector<double> s2;
        for (size_t i = 2; i + 2 < n; ++i) {
            double win[5] = {v[i - 2], v[i - 1], v[i], v[i + 1], v[i + 2]};
            s2.push_back(windowSse(win) / 2.0);
        }
        if (s2.size() < 3) return 0.0;
        std::sort(s2.begin(), s2.end());
        double s5 = s2[s2.size() / 2];
        // 3-point second-difference MAD estimate; take the smaller of the two
        // (curvature leakage is >= 0 in both)
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
        double s2min = std::min(s5, s3 * s3);
        if (!(s2min > 0.0)) return 0.0;
        return std::sqrt(s2min);
    };
    double sLn = robustSigma(y);
    double sPhi = robustSigma(ph);
    double s = std::sqrt(sLn * sLn + sPhi * sPhi);
    if (!std::isfinite(s)) return -1.0;
    return std::min(std::max(s, 1e-9), 0.5);
}

double aicc(double rss, int nObs, int p) {
    int k = p + 1;
    rss = std::max(rss, 1e-300);
    double denom = std::max((double)(nObs - k - 1), 1.0);
    return nObs * std::log(rss / nObs) + 2 * k + 2 * k * (k + 1) / denom;
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

double curveMaxRel(const std::vector<Complex>& zTrue, const std::vector<Complex>& zFit) {
    double mx = 0.0;
    for (size_t k = 0; k < zTrue.size(); ++k) {
        double rel = std::abs((zTrue[k] - zFit[k]) / zTrue[k]);
        mx = std::max(mx, rel);
    }
    return mx;
}

double curveMaxRelFloored(const std::vector<Complex>& zTrue,
                          const std::vector<Complex>& zFit, double floorFrac) {
    std::vector<double> mags(zTrue.size());
    for (size_t k = 0; k < zTrue.size(); ++k) mags[k] = std::abs(zTrue[k]);
    double med = median(mags);
    double mx = 0.0;
    for (size_t k = 0; k < zTrue.size(); ++k) {
        double den = std::max(std::abs(zTrue[k]), floorFrac * med);
        double rel = std::abs(zTrue[k] - zFit[k]) / den;
        mx = std::max(mx, rel);
    }
    return mx;
}

std::pair<std::vector<double>, std::vector<std::string>> matchedGroupErrors(const std::vector<Value>& fitVals, const std::vector<Value>& trueVals,
                   const std::vector<ReducedEdge>& reducedEdges) {
    // classes keyed by (min(u,v), max(u,v), kind), first-encounter order
    std::vector<std::pair<std::tuple<int, int, char>,
                          std::vector<std::pair<Value, Value>>>> classes;
    std::map<std::tuple<int, int, char>, int> idx;
    for (size_t g = 0; g < reducedEdges.size(); ++g) {
        auto key = std::make_tuple(std::min(reducedEdges[g].u, reducedEdges[g].v),
                                   std::max(reducedEdges[g].u, reducedEdges[g].v),
                                   reducedEdges[g].kind);
        auto it = idx.find(key);
        if (it == idx.end()) {
            idx[key] = (int)classes.size();
            classes.push_back({key, {{fitVals[g], trueVals[g]}}});
        } else {
            classes[(size_t)it->second].second.push_back({fitVals[g], trueVals[g]});
        }
    }
    std::vector<double> errors;
    std::vector<std::string> labels;
    for (auto& [key, lst] : classes) {
        std::vector<Value> fits, trues;
        for (auto& [f, t] : lst) {
            fits.push_back(f);
            trues.push_back(t);
        }
        // sort both by (v1, v2): within a same-slot same-kind group the
        // members are interchangeable
        auto byVal = [](const Value& a, const Value& b) {
            if (a.v1 != b.v1) return a.v1 < b.v1;
            return a.v2 < b.v2;
        };
        std::sort(fits.begin(), fits.end(), byVal);
        std::sort(trues.begin(), trues.end(), byVal);
        for (size_t i = 0; i < fits.size(); ++i) {
            const Value& fv = fits[i];
            const Value& tv = trues[i];
            for (int j = 1; j < (std::get<2>(key) == 'L' ? 3 : 2); ++j) {
                double t = j == 1 ? tv.v1 : tv.v2;
                double f = j == 1 ? fv.v1 : fv.v2;
                errors.push_back(std::abs(f - t) / t);
                std::string nm = (std::get<2>(key) == 'L' && j == 2) ? "Rd" : "v";
                labels.push_back(std::to_string(std::get<0>(key)) + "," +
                                 std::to_string(std::get<1>(key)) + "," +
                                 std::get<2>(key) + ":" + nm);
            }
        }
    }
    return {errors, labels};
}

}  // namespace tf
