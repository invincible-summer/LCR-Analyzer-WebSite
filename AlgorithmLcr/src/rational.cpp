#include "lcr/lcr.hpp"
#include <algorithm>
namespace lcr {
namespace {
struct Pole {
  Complex p;
  bool pair;
};
std::vector<Complex> basis(Complex s, const std::vector<Pole> &poles) {
  std::vector<Complex> b;
  for (auto p : poles)
    if (p.pair) {
      b.push_back(1.0 / (s - p.p) + 1.0 / (s - std::conj(p.p)));
      b.push_back(Complex(0, 1) / (s - p.p) -
                  Complex(0, 1) / (s - std::conj(p.p)));
    } else
      b.push_back(1.0 / (s - p.p));
  return b;
}
std::vector<Pole> regroup(const Eigen::VectorXcd &eig) {
  std::vector<Pole> out;
  for (int i = 0; i < eig.size(); ++i) {
    Complex p = eig[i];
    if (!std::isfinite(p.real()) || !std::isfinite(p.imag()))
      return {};
    p.real(-std::max(1e-8, std::abs(p.real())));
    if (std::abs(p.imag()) < 1e-7 * std::max(1., std::abs(p)))
      out.push_back({{p.real(), 0}, false});
    else if (p.imag() > 0)
      out.push_back({p, true});
  }
  return out;
}
} // namespace
std::vector<Graph> rationalStarts(const Data &data, int maxDevices) {
  if (data.size() < 4)
    return {};
  double lo = inf, hi = 0, mag = 0;
  for (auto p : data) {
    lo = std::min(lo, p.f);
    hi = std::max(hi, p.f);
    mag += std::abs(p.z);
  }
  double w0 = 2 * pi * std::sqrt(lo * hi),
         z0 = std::max(1e-12, mag / data.size());
  std::vector<Graph> result;
  for (int family = 0; family < 2; ++family)
    for (int order = 1; order <= std::min(4, maxDevices); ++order) {
      std::vector<Pole> poles;
      for (int j = 0; j < order; ++j) {
        double w = std::pow(hi / lo, (j + .5) / order - .5);
        poles.push_back(family == 0 ? Pole{Complex(-w, 0), false}
                                    : Pole{Complex(-.1 * w, w), true});
      }
      for (int iteration = 0; iteration < 6; ++iteration) {
        int m = int(basis(Complex(0, 1), poles).size());
        if (!m)
          break;
        Eigen::MatrixXd A(2 * data.size(), 3 + 2 * m);
        Eigen::VectorXd rhs(2 * data.size());
        for (size_t k = 0; k < data.size(); ++k) {
          Complex s(0, 2 * pi * data[k].f / w0), z = data[k].z / z0;
          auto b = basis(s, poles);
          std::vector<Complex> row{1., s, 1. / s};
          row.insert(row.end(), b.begin(), b.end());
          for (auto v : b)
            row.push_back(-z * v);
          double w = 1 / std::max(std::abs(z), 1e-9);
          for (size_t j = 0; j < row.size(); ++j) {
            A(2 * k, j) = w * row[j].real();
            A(2 * k + 1, j) = w * row[j].imag();
          }
          rhs[2 * k] = w * z.real();
          rhs[2 * k + 1] = w * z.imag();
        }
        Eigen::VectorXd x = A.colPivHouseholderQr().solve(rhs);
        if (!x.allFinite())
          break;
        Eigen::VectorXcd pp(m), cc(m);
        int q = 0;
        for (auto p : poles) {
          if (p.pair) {
            pp[q] = p.p;
            pp[q + 1] = std::conj(p.p);
            cc[q] = Complex(x[3 + m + q], x[3 + m + q + 1]);
            cc[q + 1] = std::conj(cc[q]);
            q += 2;
          } else {
            pp[q] = p.p;
            cc[q] = x[3 + m + q];
            ++q;
          }
        }
        Eigen::MatrixXcd relocated = pp.asDiagonal();
        relocated -= Eigen::VectorXcd::Ones(m) * cc.transpose();
        Eigen::ComplexEigenSolver<Eigen::MatrixXcd> eig(relocated, false);
        if (eig.info() != Eigen::Success)
          break;
        auto next = regroup(eig.eigenvalues());
        if (int(basis(Complex(0, 1), next).size()) != m)
          break;
        poles = std::move(next);
      }
      int m = int(basis(Complex(0, 1), poles).size());
      if (!m)
        continue;
      Eigen::MatrixXd A(2 * data.size(), 3 + m);
      Eigen::VectorXd rhs(2 * data.size());
      for (size_t k = 0; k < data.size(); ++k) {
        Complex s(0, 2 * pi * data[k].f / w0), z = data[k].z / z0;
        auto b = basis(s, poles);
        std::vector<Complex> row{1., s, 1. / s};
        row.insert(row.end(), b.begin(), b.end());
        double w = 1 / std::max(std::abs(z), 1e-9);
        for (size_t j = 0; j < row.size(); ++j) {
          A(2 * k, j) = w * row[j].real();
          A(2 * k + 1, j) = w * row[j].imag();
        }
        rhs[2 * k] = w * z.real();
        rhs[2 * k + 1] = w * z.imag();
      }
      Eigen::VectorXd x = A.colPivHouseholderQr().solve(rhs);
      if (!x.allFinite())
        continue;
      std::vector<std::vector<Edge>> sections;
      bool physical = true;
      double eps = 1e-7 * std::max(1., x.cwiseAbs().maxCoeff());
      for (int j = 0; j < 3; ++j)
        if (x[j] < -eps)
          physical = false;
      if (x[0] > eps)
        sections.push_back({{'R', x[0] * z0, 0}});
      if (x[1] > eps)
        sections.push_back({{'L', x[1] * z0 / w0, 0}});
      if (x[2] > eps)
        sections.push_back({{'C', 1 / (x[2] * z0 * w0), 0}});
      int q = 3;
      for (auto p : poles) {
        if (p.pair) {
          Complex r(x[q], x[q + 1]);
          q += 2;
          double K = 2 * r.real(), constant = -2 * (r * std::conj(p.p)).real(),
                 a = -2 * p.p.real(), b = std::norm(p.p);
          if (K <= eps || std::abs(constant) > 1e-6 * std::max(K, 1e-12)) {
            physical = false;
            break;
          }
          sections.push_back({{'R', z0 * K / a, 0},
                              {'L', z0 * K / (b * w0), 0},
                              {'C', 1 / (z0 * K * w0), 0}});
        } else {
          double r = x[q++], a = -p.p.real();
          if (r <= eps) {
            physical = false;
            break;
          }
          sections.push_back(
              {{'R', z0 * r / a, 0}, {'C', 1 / (z0 * r * w0), 0}});
        }
      }
      if (!physical || sections.empty())
        continue;
      Graph g;
      int u = 0;
      for (size_t i = 0; i < sections.size(); ++i) {
        int v = i + 1 == sections.size() ? 1 : g.vertices++;
        for (auto e : sections[i])
          g.edges.push_back({u, v, e});
        u = v;
      }
      if (g.edges.size() > size_t(maxDevices))
        continue;
      // Certify the physical realization against the rational function, not the
      // data.
      bool matches = true;
      for (auto p : data) {
        Complex s(0, 2 * pi * p.f / w0), z = x[0] + x[1] * s + x[2] / s;
        auto b = basis(s, poles);
        for (int j = 0; j < m; ++j)
          z += x[3 + j] * b[j];
        auto f = forward(g, p.f, false);
        if (f.status != SolveStatus::OK ||
            std::abs(f.z - z * z0) > 1e-5 * std::max(1e-12, std::abs(z * z0))) {
          matches = false;
          break;
        }
      }
      if (matches)
        result.push_back(std::move(g));
    }
  return result;
}
} // namespace lcr
