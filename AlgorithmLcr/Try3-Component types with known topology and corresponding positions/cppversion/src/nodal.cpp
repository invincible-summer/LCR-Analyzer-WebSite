#include "nodal.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace tf {

NodalModel NodalModel::fromEdges(const std::vector<std::tuple<int, int, char>>& edges) {
    std::set<int> nodes;
    for (const auto& [u, v, k] : edges) {
        (void)k;
        nodes.insert(u);
        nodes.insert(v);
    }
    for (int t = 0; t < 2; ++t)
        if (!nodes.count(t))
            throw PortOpenError("port node " + std::to_string(t) +
                                " not incident to any edge");
    std::vector<int> others;
    for (int n : nodes)
        if (n != 0 && n != 1) others.push_back(n);
    std::sort(others.begin(), others.end());
    std::vector<int> nodeOf{1};
    nodeOf.insert(nodeOf.end(), others.begin(), others.end());
    std::map<int, int> indexOf;
    for (size_t i = 0; i < nodeOf.size(); ++i) indexOf[nodeOf[i]] = (int)i;

    NodalModel m;
    m.edges = edges;
    m.nodeOf = nodeOf;
    for (const auto& [u, v, kind] : edges) {
        int cnt = kind == 'L' ? 2 : 1;
        m.edgeParamSlices.push_back({m.nParams, cnt});
        m.nParams += cnt;
        auto itU = indexOf.find(u);
        auto itV = indexOf.find(v);
        m.riList.push_back(itU == indexOf.end() ? -1 : itU->second);
        m.rjList.push_back(itV == indexOf.end() ? -1 : itV->second);
    }
    return m;
}

namespace {

// y(s) and dy/dlog10param for one edge at one frequency (vals = 10^theta).
inline void edgeYdY(char kind, double v1, double v2, Complex s, Complex& y,
                    Complex& dy1, Complex& dy2) {
    if (kind == 'R') {
        y = Complex(1.0 / v1, 0.0);
        dy1 = -kLn10 * y;
        dy2 = Complex(0.0, 0.0);
        return;
    }
    if (kind == 'C') {
        y = s * v1;
        dy1 = kLn10 * y;
        dy2 = Complex(0.0, 0.0);
        return;
    }
    Complex z(v2, 0.0);
    z += s * v1;
    y = 1.0 / z;
    Complex y2 = y * y;
    dy1 = (-kLn10 * v1 * s) * y2;
    dy2 = Complex(-kLn10 * v2, 0.0) * y2;
}

}  // namespace

void NodalModel::zAndJac(const std::vector<double>& theta,
                         const std::vector<Complex>& s, std::vector<Complex>& Z,
                         std::vector<Complex>& J) const {
    const int M = (int)s.size();
    const int k = nNodesRed();
    const int p = nParams;
    std::vector<double> vals(p);
    for (int t = 0; t < p; ++t) vals[t] = std::pow(10.0, theta[(size_t)t]);

    Z.assign((size_t)M, Complex(0.0, 0.0));
    J.assign((size_t)p * M, Complex(0.0, 0.0));

    for (int m = 0; m < M; ++m) {
        std::vector<Complex> Y((size_t)k * k, Complex(0.0, 0.0));
        // per-edge admittances and log-derivatives at this frequency
        std::vector<Complex> yE(edges.size()), dE1(edges.size()), dE2(edges.size());
        for (size_t e = 0; e < edges.size(); ++e) {
            auto [sl, cnt] = edgeParamSlices[e];
            edgeYdY(std::get<2>(edges[e]), vals[(size_t)sl],
                    cnt == 2 ? vals[(size_t)sl + 1] : 0.0, s[(size_t)m], yE[e], dE1[e],
                    dE2[e]);
            int ri = riList[e], rj = rjList[e];
            if (ri >= 0) Y[(size_t)ri * k + ri] += yE[e];
            if (rj >= 0) Y[(size_t)rj * k + rj] += yE[e];
            if (ri >= 0 && rj >= 0) {
                Y[(size_t)ri * k + rj] -= yE[e];
                Y[(size_t)rj * k + ri] -= yE[e];
            }
        }
        std::vector<Complex> x((size_t)k, Complex(0.0, 0.0));
        std::vector<Complex> b((size_t)k, Complex(0.0, 0.0));
        b[0] = Complex(1.0, 0.0);
        std::vector<Complex> A = Y;
        if (!luSolveComplex(A, k, b)) {
            // singular: sanitized big-Z, Jacobian rows zeroed (measure-zero)
            Z[(size_t)m] = Complex(kBigZ, 0.0);
            continue;
        }
        x = b;
        Complex z0 = x[0];
        if (!std::isfinite(z0.real()) || !std::isfinite(z0.imag()) ||
            std::abs(z0) > kBigZ)
            z0 = Complex(kBigZ, 0.0);
        Z[(size_t)m] = z0;
        for (size_t e = 0; e < edges.size(); ++e) {
            Complex d(0.0, 0.0);
            if (riList[e] >= 0) d += x[(size_t)riList[e]];
            if (rjList[e] >= 0) d -= x[(size_t)rjList[e]];
            Complex d2 = d * d;
            auto [sl, cnt] = edgeParamSlices[e];
            Complex dys[2] = {dE1[e], dE2[e]};
            for (int j = 0; j < cnt; ++j) {
                Complex dz = -(dys[j] * d2);
                if (!std::isfinite(dz.real()) || !std::isfinite(dz.imag()) ||
                    std::abs(z0) >= kBigZ)
                    dz = Complex(0.0, 0.0);
                J[(size_t)(sl + j) * M + (size_t)m] = dz;
            }
        }
    }
}

std::vector<Complex> NodalModel::zLinear(const std::vector<double>& vals,
                                         const std::vector<Complex>& s) const {
    for (double v : vals)
        if (!(v > 0.0)) throw std::invalid_argument("linear values must be positive");
    std::vector<double> theta(vals.size());
    for (size_t i = 0; i < vals.size(); ++i) theta[i] = std::log10(vals[i]);
    std::vector<Complex> Z, J;
    zAndJac(theta, s, Z, J);
    return Z;
}

void NodalModel::elasticity(const std::vector<double>& theta,
                            const std::vector<Complex>& s,
                            std::vector<Complex>& E) const {
    std::vector<Complex> Z, J;
    zAndJac(theta, s, Z, J);
    const int M = (int)s.size();
    E.assign(J.size(), Complex(0.0, 0.0));
    for (int t = 0; t < nParams; ++t)
        for (int m = 0; m < M; ++m)
            E[(size_t)t * M + (size_t)m] =
                J[(size_t)t * M + (size_t)m] / (Z[(size_t)m] * kLn10);
}

NodalModel modelFromReduced(const ReductionResult& red) {
    std::vector<std::tuple<int, int, char>> edges;
    edges.reserve(red.edges.size());
    for (const auto& e : red.edges) edges.push_back({e.u, e.v, e.kind});
    return NodalModel::fromEdges(edges);
}

}  // namespace tf
