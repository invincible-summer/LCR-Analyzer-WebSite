#include "metric.hpp"

#include <cmath>

namespace ng {

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

std::vector<double> weightedRssBatch(const std::vector<Complex>& z,
                                     const std::vector<std::vector<Complex>>& zModel,
                                     const std::vector<double>& w) {
    std::vector<double> out(zModel.size(), 0.0);
    for (size_t c = 0; c < zModel.size(); ++c) {
        double s = 0.0;
        for (size_t k = 0; k < z.size(); ++k) {
            double r = w[k] * std::abs(z[k] - zModel[c][k]);
            s += r * r;
        }
        out[c] = s;
    }
    return out;
}

double weightedRss(const std::vector<Complex>& z, const std::vector<Complex>& zModel,
                   const std::vector<double>& w) {
    return weightedRssBatch(z, {zModel}, w)[0];
}

}  // namespace ng
