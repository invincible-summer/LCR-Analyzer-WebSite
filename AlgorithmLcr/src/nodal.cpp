#include "lcr/lcr.hpp"
#include <algorithm>
#include <stdexcept>
namespace lcr {
std::string name(SolveStatus s) {
  switch (s) {
  case SolveStatus::OK:
    return "OK";
  case SolveStatus::PORT_OPEN:
    return "PORT_OPEN";
  case SolveStatus::SINGULAR:
    return "SINGULAR";
  case SolveStatus::ILL_CONDITIONED:
    return "ILL_CONDITIONED";
  default:
    return "NONFINITE";
  }
}
Complex impedance(const Edge &e, double f) {
  Complex s(0, 2 * pi * f);
  if (e.type == 'R')
    return e.parameter;
  if (e.type == 'C')
    return 1.0 / (s * e.parameter);
  return e.parameterOfCapacitanceDCResistance + s * e.parameter;
}
Forward forward(const Graph &g, double f, bool derivatives) {
  validate(g);
  if (!(f > 0) || !std::isfinite(f))
    throw std::invalid_argument("invalid frequency");
  Forward out;
  std::vector<int> live;
  try {
    live = liveEdges(g);
  } catch (const std::runtime_error &) {
    out.status = SolveStatus::PORT_OPEN;
    return out;
  }
  std::vector<int> map(g.vertices, -1);
  int n = 0;
  for (auto i : live)
    for (int u : {g.edges[i].u, g.edges[i].v})
      if (u != 0 && map[u] < 0)
        map[u] = n++;
  Eigen::MatrixXcd Y = Eigen::MatrixXcd::Zero(n, n);
  Eigen::MatrixXd stampMagnitude = Eigen::MatrixXd::Zero(n, n);
  Eigen::VectorXcd b = Eigen::VectorXcd::Zero(n);
  b[map[1]] = 1;
  for (int i : live) {
    auto e = g.edges[i];
    Complex y = 1.0 / impedance(e.element, f);
    int u = map[e.u], v = map[e.v];
    if (u >= 0) {
      Y(u, u) += y;
      stampMagnitude(u, u) += std::abs(y);
    }
    if (v >= 0) {
      Y(v, v) += y;
      stampMagnitude(v, v) += std::abs(y);
    }
    if (u >= 0 && v >= 0) {
      Y(u, v) -= y;
      Y(v, u) -= y;
      stampMagnitude(u, v) += std::abs(y);
      stampMagnitude(v, u) += std::abs(y);
    }
  }
  if (!Y.allFinite() || !stampMagnitude.allFinite()) {
    out.status = SolveStatus::NONFINITE;
    return out;
  }
  double scale = Y.cwiseAbs().maxCoeff();
  if (!(scale > 0)) {
    out.status = SolveStatus::SINGULAR;
    return out;
  }
  Eigen::MatrixXcd A = Y / scale;
  Eigen::FullPivLU<Eigen::MatrixXcd> lu(A);
  lu.setThreshold(1e-15);
  if (!lu.isInvertible()) {
    out.status = SolveStatus::SINGULAR;
    out.rcond = 0;
    return out;
  }
  Eigen::VectorXcd v = lu.solve(b / scale);
  // Scalar Y has condition number one even when large LC stamps nearly cancel.
  // Include assembly cancellation in the reported effective reciprocal
  // estimate.
  out.rcond = lu.rcond() * std::min(1., scale / stampMagnitude.maxCoeff());
  out.z = v[map[1]];
  out.backwardError = (Y * v - b).norm() / (Y.norm() * v.norm() + b.norm());
  if (!v.allFinite() || !std::isfinite(out.backwardError)) {
    out.status = SolveStatus::NONFINITE;
    return out;
  }
  if (out.rcond < 1e-12 || out.backwardError > 1e-10)
    out.status = SolveStatus::ILL_CONDITIONED;
  if (derivatives)
    for (size_t i = 0; i < g.edges.size(); ++i) {
      auto e = g.edges[i];
      bool active = std::find(live.begin(), live.end(), int(i)) != live.end();
      Complex dv = active ? ((e.u == 0 ? Complex(0) : v[map[e.u]]) -
                             (e.v == 0 ? Complex(0) : v[map[e.v]]))
                          : Complex(0);
      Complex s(0, 2 * pi * f), z = impedance(e.element, f), dy;
      if (e.element.type == 'R')
        dy = -1.0 / (z * z);
      else if (e.element.type == 'C')
        dy = s;
      else
        dy = -s / (z * z);
      out.jacobian.push_back(-dy * dv * dv);
      if (e.element.type == 'L')
        out.jacobian.push_back(dv * dv / (z * z));
    }
  return out;
}
} // namespace lcr
