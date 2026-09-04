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
