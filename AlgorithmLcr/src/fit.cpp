#include "lcr/lcr.hpp"
#include "numerics.hpp"
#include <algorithm>
#include <chrono>
#include <numeric>
#include <random>
#include <stdexcept>
namespace lcr {
namespace {
double median(std::vector<double> v) {
  auto m = v.begin() + v.size() / 2;
  std::nth_element(v.begin(), m, v.end());
  return *m;
}
std::vector<Eigen::Matrix2d> whiteners(const Data &d, const Config &c) {
  std::vector<Eigen::Matrix2d> w;
  std::vector<double> mag;
  for (auto p : d)
    mag.push_back(std::abs(p.z));
  double floor = std::max(1e-15, median(mag) * c.relativeFloor);
  for (size_t i = 0; i < d.size(); ++i) {
    if (c.covariance.empty())
      w.push_back(Eigen::Matrix2d::Identity() /
                  std::max(floor, std::abs(d[i].z)));
    else {
      Eigen::LLT<Eigen::Matrix2d> llt(c.covariance[i]);
      w.push_back(llt.matrixL().solve(Eigen::Matrix2d::Identity()));
    }
  }
  return w;
}
struct Param {
  size_t edge;
  bool dcr;
  double lo, hi, scale;
};
struct Model {
  Graph base;
  std::vector<Param> params;
  Graph decode(const Eigen::VectorXd &x) const {
    Graph g = base;
    for (size_t j = 0; j < params.size(); ++j) {
      auto p = params[j];
      double q = p.dcr ? x[j] * p.scale : std::pow(10., x[j]);
      if (p.dcr)
        g.edges[p.edge].element.parameterOfCapacitanceDCResistance = q;
      else
        g.edges[p.edge].element.parameter = q;
    }
    return g;
  }
  Eigen::VectorXd encode(const Graph &g) const {
    Eigen::VectorXd x(params.size());
    for (size_t j = 0; j < params.size(); ++j) {
      auto p = params[j];
      auto e = g.edges[p.edge].element;
      double q = p.dcr ? e.parameterOfCapacitanceDCResistance : e.parameter;
      x[j] = std::clamp(p.dcr ? q / p.scale : std::log10(q), p.lo, p.hi);
    }
    return x;
  }
  Eigen::VectorXd project(Eigen::VectorXd x) const {
    for (size_t j = 0; j < params.size(); ++j)
      x[j] = std::clamp(x[j], params[j].lo, params[j].hi);
    return x;
  }
};
struct Evaluation {
  bool ok = true;
  Eigen::VectorXd r;
  Eigen::MatrixXd j;
  std::vector<Complex> z;
  double backward = 0, rcond = 1;
};
Evaluation evaluate(const Model &m, const Eigen::VectorXd &x, const Data &d,
                    const std::vector<Eigen::Matrix2d> &w,
                    const std::vector<double> &robust) {
  Evaluation a;
  a.r.resize(2 * d.size());
  a.j.resize(2 * d.size(), x.size());
  Graph g = m.decode(x);
  for (size_t k = 0; k < d.size(); ++k) {
    auto f = forward(g, d[k].f);
    a.backward = std::max(a.backward, f.backwardError);
    a.rcond = std::min(a.rcond, f.rcond);
    if (f.status != SolveStatus::OK &&
        f.status != SolveStatus::ILL_CONDITIONED) {
      a.ok = false;
      return a;
    }
    if (f.backwardError > numerics::policy.backwardReject ||
        f.rcond < numerics::policy.rcondReject) {
      a.ok = false;
      return a;
    }
    a.z.push_back(f.z);
    Eigen::Matrix2d W = w[k] * std::sqrt(robust[k]);
    Complex err = f.z - d[k].z;
    a.r.segment<2>(2 * k) = W * Eigen::Vector2d(err.real(), err.imag());
    for (size_t j = 0; j < m.params.size(); ++j) {
      auto p = m.params[j];
      double dq =
          p.dcr ? p.scale : std::log(10.) * g.edges[p.edge].element.parameter;
      Complex dz = f.jacobian[j] * dq;
      a.j.block<2, 1>(2 * k, j) = W * Eigen::Vector2d(dz.real(), dz.imag());
    }
  }
  a.ok = a.r.allFinite() && a.j.allFinite();
  return a;
}
struct Optimum {
  Eigen::VectorXd x;
  Evaluation eval;
  std::string status = "max_iterations";
};
Optimum optimize(const Model &model, Eigen::VectorXd x, const Data &d,
                 const std::vector<Eigen::Matrix2d> &w,
                 const std::vector<double> &robust, int iterations,
                 const std::function<bool()> &stop) {
  Optimum out;
  double lambda = 1e-3;
  auto a = evaluate(model, x, d, w, robust);
  if (!a.ok) {
    out.x = x;
    out.eval = a;
    out.status = "numerical_failure";
    return out;
  }
  for (int it = 0; it < iterations; ++it) {
    if (stop()) {
      out.status = "budget_exhausted";
      break;
    }
    Eigen::VectorXd scale(a.j.cols());
    for (int j = 0; j < a.j.cols(); ++j)
      scale[j] = std::max(1e-8, a.j.col(j).norm());
    Eigen::VectorXd grad = a.j.transpose() * a.r;
    for (int j = 0; j < x.size(); ++j)
      if ((x[j] <= model.params[j].lo + 1e-14 && grad[j] > 0) ||
          (x[j] >= model.params[j].hi - 1e-14 && grad[j] < 0))
        grad[j] = 0;
    if ((grad.array() / scale.array()).abs().maxCoeff() < 1e-10 ||
        a.r.squaredNorm() < 1e-24) {
      out.status = "converged_gradient";
      break;
    }
    Eigen::MatrixXd aug =
        Eigen::MatrixXd::Zero(a.j.rows() + x.size(), x.size());
    aug.topRows(a.j.rows()) = a.j;
    // Fixed coordinates must not absorb residual in an unconstrained trial
    // step.
    for (int j = 0; j < x.size(); ++j)
      if (model.params[j].lo == model.params[j].hi)
        aug.col(j).setZero();
    aug.bottomRows(x.size()).diagonal() = std::sqrt(lambda) * scale;
    Eigen::VectorXd rhs = Eigen::VectorXd::Zero(aug.rows());
    rhs.head(a.r.size()) = -a.r;
    Eigen::VectorXd step =
        aug.jacobiSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).solve(rhs);
    // Keep trial steps within a bounded trust radius in optimization
    // coordinates.
    double largest = step.cwiseAbs().maxCoeff();
    if (largest > 2)
      step *= 2 / largest;
    Eigen::VectorXd trial = model.project(x + step);
    auto b = evaluate(model, trial, d, w, robust);
    if (b.ok && b.r.squaredNorm() < a.r.squaredNorm()) {
      double gain = a.r.squaredNorm() - b.r.squaredNorm(),
             cost = a.r.squaredNorm();
      x = trial;
      a = std::move(b);
      lambda = std::max(1e-12, lambda / 3);
      if (gain < 1e-12 * std::max(cost, 1e-20)) {
        out.status = "converged_cost";
        break;
      }
    } else {
      lambda *= 10;
      if (lambda > 1e14) {
        out.status = "stalled";
        break;
      }
    }
  }
  out.x = x;
  out.eval = std::move(a);
  return out;
}
} // namespace
Metrics metrics(const Data &d, const std::vector<Complex> &z, int parameters,
                const Config &c) {
  validate(d, c);
  if (parameters < 0)
    throw std::invalid_argument("negative parameter count");
  Metrics m;
  if (z.size() != d.size())
    return m;
  auto w = whiteners(d, c);
  Config relative = c;
  relative.covariance.clear();
  auto wr = whiteners(d, relative);
  m.rss = 0;
  m.wrmse = 0;
  m.maxRel = 0;
  for (size_t i = 0; i < d.size(); ++i) {
    auto e = z[i] - d[i].z;
    Eigen::Vector2d r(e.real(), e.imag());
    double rel = (wr[i] * r).norm();
    m.rss += (w[i] * r).squaredNorm();
    m.wrmse += rel * rel;
    m.maxRel = std::max(m.maxRel, rel);
  }
  m.wrmse = std::sqrt(m.wrmse / d.size());
  // Unknown common variance is one fitted statistical parameter in relative
  // mode.
  int n = int(2 * d.size()), k = parameters + (c.covariance.empty() ? 1 : 0);
  if (n > k + 1) {
    double likelihood = c.covariance.empty()
                            ? n * std::log(std::max(m.rss / n, 1e-300))
                            : m.rss;
    m.aicc = likelihood + 2 * k + 2. * k * (k + 1) / (n - k - 1);
  }
  return m;
}
Candidate fit(const PreparedNetwork &prepared, const Data &d, const Config &c,
              const std::vector<Graph> &initial) {
  const Graph &graph = prepared.effective;
  validate(graph);
  validate(d, c);
  const auto started = std::chrono::steady_clock::now();
  auto stop = [&] {
    return (c.cancelled && c.cancelled()) ||
           (c.seconds > 0 && std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - started)
                                     .count() >= c.seconds);
  };
  liveEdges(graph);
  Model model;
  model.base = graph;
  std::vector<double> mags, freqs;
  for (auto p : d) {
    mags.push_back(std::abs(p.z));
    freqs.push_back(p.f);
  }
  double zscale = std::max(1e-9, median(mags)),
         omega = 2 * pi *
                 std::sqrt(*std::min_element(freqs.begin(), freqs.end()) *
                           *std::max_element(freqs.begin(), freqs.end()));
  Graph guess = graph;
  for (size_t i = 0; i < graph.edges.size(); ++i) {
    auto e = graph.edges[i].element;
    // Bounds come exclusively from the propagated effective-edge domain:
    // aggregates may legitimately exceed any single-device global bound.
    const EdgeDomain &dom = prepared.domains.at(i);
    model.params.push_back(
        {i, false, std::log10(dom.value.lo), std::log10(dom.value.hi), 1});
    guess.edges[i].element.parameter =
        c.tolerance > 0 ? e.parameter
                        : (e.type == 'R'   ? zscale
                           : e.type == 'L' ? zscale / omega
                                           : 1 / (zscale * omega));
    if (e.type == 'L') {
      if (!dom.dcr)
        throw std::invalid_argument("inductor edge missing DCR domain");
      model.params.push_back(
          {i, true, dom.dcr->lo / zscale, dom.dcr->hi / zscale, zscale});
      guess.edges[i].element.parameterOfCapacitanceDCResistance =
          c.tolerance > 0 ? e.parameterOfCapacitanceDCResistance : zscale * .1;
    }
  }
  if (model.params.empty())
    throw std::invalid_argument("empty active graph");
  auto w = whiteners(d, c);
  std::vector<double> robust(d.size(), 1);
  std::mt19937 rng(c.seed);
  std::uniform_real_distribution<double> jitter(-2.5, 2.5);
  // best stays empty until a start produces a valid evaluation; a failed
  // start never contributes a usable Model coordinate.
  std::optional<Optimum> bestOpt;
  int bestIndex = -1, converged = 0;
  std::vector<double> finalCosts;
  std::vector<Eigen::VectorXd> finalCoordinates;
  auto base = model.encode(guess);
  int total = std::max(c.starts, int(initial.size()) + 1);
  int usedStarts = 0;
  for (int start = 0; start < total; ++start) {
    if (stop())
      break;
    ++usedStarts;
    Eigen::VectorXd x = base;
    if (start > 0 && start <= int(initial.size())) {
      if (initial[start - 1].edges.size() != graph.edges.size())
        throw std::invalid_argument("initial graph shape mismatch");
      for (size_t j = 0; j < graph.edges.size(); ++j)
        if (initial[start - 1].edges[j].element.type !=
            graph.edges[j].element.type)
          throw std::invalid_argument("initial graph type mismatch");
      x = model.encode(initial[start - 1]);
    } else if (start > 0)
      for (size_t j = 0; j < model.params.size(); ++j) {
        auto p = model.params[j];
        if (c.tolerance > 0) {
          std::uniform_real_distribution<double> uniform(p.lo, p.hi);
          x[j] = uniform(rng);
        } else if (p.dcr)
          x[j] = (start % 4 == 1 ? 0 : std::pow(10., jitter(rng) - 1));
        else
          x[j] += jitter(rng);
      }
    auto o =
        optimize(model, model.project(x), d, w, robust, c.iterations, stop);
    if (o.status.find("converged") == 0)
      ++converged;
    finalCosts.push_back(o.eval.ok ? o.eval.r.squaredNorm() : inf);
    finalCoordinates.push_back(o.x);
    if (o.eval.ok &&
        (!bestOpt || o.eval.r.squaredNorm() < bestOpt->eval.r.squaredNorm())) {
      bestOpt = std::move(o);
      bestIndex = start;
    }
  }
  Candidate result;
  result.graph = graph;
  result.nParams = 0;
  for (auto p : model.params)
    if (p.hi > p.lo)
      ++result.nParams;
  auto &diag = result.diagnostics;
  diag.starts = usedStarts;
  diag.bestStart = bestIndex;
  diag.convergedStarts = converged;
  // Explicit id -> edge/quantity/value/domain/state descriptors; the web and
  // CLI consume these instead of inferring parameter order.
  auto fillParameters = [&](const Eigen::VectorXd &x) {
    Graph fitted = model.decode(x);
    for (size_t j = 0; j < model.params.size(); ++j) {
      auto p = model.params[j];
      ParameterDiagnostic pd;
      pd.id = int(j);
      pd.edge = int(p.edge);
      pd.quantity = p.dcr ? ParamQuantity::Dcr : ParamQuantity::Value;
      pd.kind = graph.edges[p.edge].element.type;
      const EdgeDomain &dom = prepared.domains.at(p.edge);
      pd.lower = p.dcr ? dom.dcr->lo : dom.value.lo;
      pd.upper = p.dcr ? dom.dcr->hi : dom.value.hi;
      pd.value =
          p.dcr
              ? fitted.edges[p.edge].element.parameterOfCapacitanceDCResistance
              : fitted.edges[p.edge].element.parameter;
      pd.free = p.hi > p.lo;
      pd.fixed = !pd.free;
      diag.parameters.push_back(std::move(pd));
    }
  };
  if (!bestOpt) {
    // Total-state-safe failure: decode the projected initial guess so the
    // descriptor contract holds even when no start returned a valid point.
    const Eigen::VectorXd fallback = model.project(base);
    bool exhausted = usedStarts == 0 && stop();
    result.graph = model.decode(fallback);
    fillParameters(fallback);
    diag.optimizer = exhausted ? "budget_exhausted" : "numerical_failure";
    diag.numericalStatus = "FAIL";
    diag.identifiabilityStatus = "NOT_EVALUATED";
    diag.verdict = exhausted ? "LOCAL_FIT_UNCONFIRMED" : "NUMERICALLY_UNSTABLE";
    result.reduction = prepared.reduction;
    result.reduction.graph = result.graph;
    return result;
  }
  Optimum &best = *bestOpt;
  for (size_t i = 0; i < finalCosts.size(); ++i) {
    if (!std::isfinite(finalCosts[i]))
      continue;
    bool sameBasin = true;
    for (int j = 0; j < best.x.size(); ++j) {
      double tolerance = 1e-3 * std::max(1., std::abs(best.x[j]));
      if (std::abs(finalCoordinates[i][j] - best.x[j]) > tolerance)
        sameBasin = false;
    }
    if (sameBasin && std::abs(finalCosts[i] - best.eval.r.squaredNorm()) <=
                         1e-8 * std::max(1., best.eval.r.squaredNorm()))
      ++diag.agreeingStarts;
  }
  if (c.robust && !stop()) {
    // Huber IRLS on whitened complex residual norms, with an inlier-majority
    // guard.
    for (int pass = 0; pass < 3 && !stop(); ++pass) {
      auto raw =
          evaluate(model, best.x, d, w, std::vector<double>(d.size(), 1));
      std::vector<double> norms;
      for (size_t k = 0; k < d.size(); ++k)
        norms.push_back(raw.r.segment<2>(2 * k).norm());
      double cutoff = std::max(1e-12, 2.5 * median(norms));
      int outliers = 0;
      for (size_t k = 0; k < d.size(); ++k) {
        robust[k] = std::min(1., cutoff / std::max(norms[k], 1e-300));
        if (robust[k] < 1)
          ++outliers;
      }
      if (outliers == 0 || outliers > int(d.size() / 2))
        break;
      auto o = optimize(model, best.x, d, w, robust, c.iterations, stop);
      if (!o.eval.ok)
        break;
      best = std::move(o);
      diag.robustUsed = true;
      diag.outliers = outliers;
    }
  }
  result.graph = model.decode(best.x);
  auto raw = evaluate(model, best.x, d, w, std::vector<double>(d.size(), 1));
  result.predicted = raw.z;
  result.metrics = metrics(d, raw.z, result.nParams, c);
  if (stop())
    best.status = "budget_exhausted";
  diag.optimizer = best.status;
  diag.fitObjective = best.eval.r.squaredNorm();
  diag.worstBackwardError = raw.backward;
  diag.worstRcond = raw.rcond;
  fillParameters(best.x);
  // Remove fixed parameters before computing numerical rank/covariance.
  std::vector<int> free;
  for (size_t j = 0; j < model.params.size(); ++j)
    if (model.params[j].hi > model.params[j].lo)
      free.push_back(int(j));
  Eigen::MatrixXd J(raw.j.rows(), free.size());
  for (size_t j = 0; j < free.size(); ++j)
    J.col(j) = raw.j.col(free[j]);
  Eigen::JacobiSVD<Eigen::MatrixXd> svd(J, Eigen::ComputeThinU |
                                               Eigen::ComputeThinV);
  auto s = svd.singularValues();
  double tol = s.size()
                   ? std::max(J.rows(), J.cols()) *
                         std::numeric_limits<double>::epsilon() * s[0] * 100
                   : 0;
  for (int j = 0; j < s.size(); ++j) {
    diag.singularValues.push_back(s[j]);
    if (s[j] > tol)
      ++diag.rank;
  }
  diag.condition =
      diag.rank == int(free.size()) && s.size() ? s[0] / s[s.size() - 1] : inf;
  for (size_t j = 0; j < model.params.size(); ++j) {
    auto p = model.params[j];
    if (!(p.hi > p.lo))
      continue; // fixed parameters are an independent state, never at-bound
    if (best.x[j] - p.lo < 1e-7 || p.hi - best.x[j] < 1e-7) {
      diag.atBound.push_back(int(j));
      diag.parameters[j].atBound = true;
    }
    double elasticity = 0;
    for (size_t k = 0; k < d.size(); ++k) {
      auto f = forward(result.graph, d[k].f);
      double q = p.dcr ? result.graph.edges[p.edge]
                             .element.parameterOfCapacitanceDCResistance
                       : result.graph.edges[p.edge].element.parameter;
      elasticity = std::max(elasticity, std::abs(f.jacobian[j] * q) /
                                            std::max(std::abs(f.z), 1e-15));
    }
    if (elasticity < numerics::policy.weakElasticity) {
      diag.weak.push_back(int(j));
      diag.parameters[j].weak = true;
    }
  }
  if (diag.rank == int(free.size()) && J.rows() > J.cols() &&
      !diag.robustUsed && diag.atBound.empty()) {
    Eigen::MatrixXd cov = svd.matrixV() *
                          s.array().square().inverse().matrix().asDiagonal() *
                          svd.matrixV().transpose();
    if (c.covariance.empty())
      cov *= result.metrics.rss / (J.rows() - J.cols());
    for (size_t j = 0; j < free.size(); ++j) {
      auto p = model.params[free[j]];
      double dq =
          p.dcr ? p.scale
                : std::log(10.) * result.graph.edges[p.edge].element.parameter;
      double se = std::sqrt(std::max(0., cov(j, j))) * dq;
      const double half = 1.959963984540054 * std::sqrt(std::max(0., cov(j, j)));
      const double center = best.x[free[j]];
      std::array<double, 2> ci =
          p.dcr ? std::array<double, 2>{(center - half) * p.scale,
                                        (center + half) * p.scale}
                : std::array<double, 2>{std::pow(10., center - half),
                                        std::pow(10., center + half)};
      diag.standardErrors.push_back(se);
      diag.confidenceIntervals95.push_back(ci);
      diag.parameters[free[j]].standardError = se;
      diag.parameters[free[j]].ci95 = ci;
    }
  }
  if (!std::isfinite(result.metrics.rss) ||
      !std::isfinite(result.metrics.wrmse) ||
      !std::isfinite(result.metrics.maxRel))
    diag.numericalStatus = "FAIL";
  else if (diag.condition > numerics::policy.identConditionWarn ||
           diag.worstRcond < numerics::policy.rcondWarn)
    diag.numericalStatus = "WARN";
  else
    diag.numericalStatus = "OK";
  if (int(2 * d.size()) <= result.nParams)
    diag.identifiabilityStatus = "DATA_INSUFFICIENT";
  else if (diag.rank < result.nParams)
    diag.identifiabilityStatus = "RANK_DEFICIENT";
  else
    diag.identifiabilityStatus = "FULL_RANK";
  if (int(2 * d.size()) <= result.nParams)
    diag.verdict = "DATA_INSUFFICIENT";
  else if (diag.rank < result.nParams)
    diag.verdict = "AMBIGUOUS_EQUIVALENCE_CLASS";
  else if (diag.condition > numerics::policy.identConditionWarn ||
           raw.rcond < numerics::policy.rcondWarn)
    diag.verdict = "NUMERICALLY_UNSTABLE";
  else if (best.status.find("converged") != 0 || !diag.atBound.empty() ||
           diag.agreeingStarts < 2)
    diag.verdict = "LOCAL_FIT_UNCONFIRMED";
  else
    diag.verdict = "IDENTIFIABLE_LOCAL";
  result.reduction = prepared.reduction;
  result.reduction.graph = result.graph;
  return result;
}
Candidate fit(const Graph &graph, const Data &d, const Config &c,
              const std::vector<Graph> &initial) {
  return fit(prepareForFit(graph, c, ReductionPolicy::None), d, c, initial);
}
} // namespace lcr
