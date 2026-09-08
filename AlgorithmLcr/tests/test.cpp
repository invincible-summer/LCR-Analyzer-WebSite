#include "lcr/lcr.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
using namespace lcr;
namespace {
int checks = 0;
void require(bool b, const std::string &s) {
  ++checks;
  if (!b)
    throw std::runtime_error(s);
}
template <class F> void rejects(F f) {
  bool failed = false;
  try {
    f();
  } catch (const std::exception &) {
    failed = true;
  }
  require(failed, "expected rejection");
}
void close(Complex a, Complex b, double tolerance = 1e-9) {
  if (std::abs(a - b) > tolerance * std::max(1., std::abs(b)))
    std::cerr << "actual=" << a << " expected=" << b
              << " relative=" << std::abs(a - b) / std::max(1., std::abs(b))
              << "\n";
  require(std::abs(a - b) <= tolerance * std::max(1., std::abs(b)),
          "complex mismatch");
}
Data sample(const Graph &g, int n = 32) {
  Data d;
  for (int i = 0; i < n; ++i) {
    double f = 10 * std::pow(1e4, double(i) / (n - 1));
    auto a = forward(g, f, false);
    require(a.status == SolveStatus::OK, "sample solver");
    d.push_back({f, a.z});
  }
  return d;
}
// Independent long-double Gaussian elimination, grounded at terminal 1.
std::complex<long double> oracle(const Graph &g, double f) {
  using C = std::complex<long double>;
  int n = g.vertices - 1;
  std::vector<std::vector<C>> a(n, std::vector<C>(n + 1));
  auto idx = [](int v) { return v == 0 ? 0 : v - 1; };
  a[0][n] = 1;
  for (auto b : g.edges) {
    C s(0, 2 * static_cast<long double>(pi) * f), y;
    auto e = b.element;
    if (e.type == 'R')
      y = 1.L / e.parameter;
    else if (e.type == 'C')
      y = s * static_cast<long double>(e.parameter);
    else
      y = 1.L /
          (static_cast<long double>(e.parameterOfCapacitanceDCResistance) +
           s * static_cast<long double>(e.parameter));
    if (b.u != 1)
      a[idx(b.u)][idx(b.u)] += y;
    if (b.v != 1)
      a[idx(b.v)][idx(b.v)] += y;
    if (b.u != 1 && b.v != 1) {
      a[idx(b.u)][idx(b.v)] -= y;
      a[idx(b.v)][idx(b.u)] -= y;
    }
  }
  for (int k = 0; k < n; ++k) {
    int pivot = k;
    for (int i = k + 1; i < n; ++i)
      if (std::abs(a[i][k]) > std::abs(a[pivot][k]))
        pivot = i;
    std::swap(a[k], a[pivot]);
    if (std::abs(a[k][k]) < 1e-30L)
      throw std::runtime_error("oracle singular");
    for (int i = k + 1; i < n; ++i) {
      C m = a[i][k] / a[k][k];
      for (int j = k; j <= n; ++j)
        a[i][j] -= m * a[k][j];
    }
  }
  std::vector<C> x(n);
  for (int i = n - 1; i >= 0; --i) {
    C b = a[i][n];
    for (int j = i + 1; j < n; ++j)
      b -= a[i][j] * x[j];
    x[i] = b / a[i][i];
  }
  return x[0];
}
void io() {
  std::istringstream f("# comment\n2\n1.25 2 -3\n2e3 4.25 6 # x\n");
  auto d = loadMeasurements(f);
  std::ostringstream s;
  dumpMeasurements(s, d);
  std::istringstream in(s.str());
  auto d2 = loadMeasurements(in);
  require(d2[1].z == d[1].z, "round trip");
  for (auto text : {"1\n0 2 3", "1\n1 nan 3", "2\n1 2 3", "1\n1 2 3 4",
                    "1\n1 inf 2", "1\n0x1p0 1 2"})
    rejects([&] {
      std::istringstream x(text);
      loadMeasurements(x);
    });
  std::istringstream cs("L .001 0\nR 5\nR 5\n");
  auto es = loadComponents(cs);
  require(es.size() == 3 && es[0].parameterOfCapacitanceDCResistance == 0,
          "components");
  rejects([] {
    std::istringstream x("R 1 2");
    loadComponents(x);
  });
  std::istringstream topo("3\n0 1\n2\nL\nC\nR");
  auto g = loadTopology(topo);
  require(g.edges.size() == 3 && g.edges[1].u == 1, "topology order");
  std::istringstream count("3 # canonical\n");
  require(loadCount(count) == 3, "count");
  rejects([] {
    std::istringstream x("3.0");
    loadCount(x);
  });
  std::ostringstream o;
  printAdjacency(o, {2, {{0, 1, {'L', .001, 0}}}});
  require(o.str() == "adjacency[1] V=2 (ports 0,1):\n  (0,1): L 1.000e-03\n",
          "print format");
}
void physics() {
  for (char t : std::string("RCL")) {
    Graph g{2,
            {{0,
              1,
              {t,
               t == 'C'   ? 1e-6
               : t == 'L' ? .001
                          : 100,
               t == 'L' ? 0. : 0.}}}};
    close(forward(g, 1000).z, impedance(g.edges[0].element, 1000));
  }
  Graph bridge{4,
               {{0, 2, {'R', 100, 0}},
                {2, 1, {'R', 100, 0}},
                {0, 3, {'R', 100, 0}},
                {3, 1, {'R', 100, 0}},
                {2, 3, {'L', .001, 5}}}};
  close(forward(bridge, 1000).z, 100.);
  std::mt19937 rng(42);
  std::uniform_real_distribution<double> u(-1, 1);
  for (int test = 0; test < 100; ++test) {
    Graph g = bridge;
    for (auto &b : g.edges) {
      char t = "RCL"[rng() % 3];
      b.element = {t,
                   (t == 'R'   ? 100.
                    : t == 'C' ? 1e-6
                               : .001) *
                       std::pow(10, u(rng)),
                   t == 'L' ? 1. : 0.};
    }
    double f = 1000 * std::pow(10, u(rng));
    auto a = forward(g, f);
    close(a.z, Complex(oracle(g, f)), 1e-10);
    size_t j = 0;
    for (size_t i = 0; i < g.edges.size(); ++i) {
      auto pert = g;
      double v = g.edges[i].element.parameter, h = v * 1e-3;
      pert.edges[i].element.parameter = v + h;
      auto zp = oracle(pert, f);
      pert.edges[i].element.parameter = v - h;
      auto zm = oracle(pert, f);
      pert.edges[i].element.parameter = v + 2 * h;
      auto zp2 = oracle(pert, f);
      pert.edges[i].element.parameter = v - 2 * h;
      auto zm2 = oracle(pert, f);
      close(a.jacobian[j++],
            Complex((zm2 - 8.L * zm + 8.L * zp - zp2) /
                    (12 * static_cast<long double>(h))),
            1e-6);
      if (g.edges[i].element.type == 'L') {
        pert = g;
        h = 1e-4;
        pert.edges[i].element.parameterOfCapacitanceDCResistance += h;
        zp = oracle(pert, f);
        pert.edges[i].element.parameterOfCapacitanceDCResistance -= 2 * h;
        zm = oracle(pert, f);
        close(a.jacobian[j++],
              Complex((zp - zm) / (2 * static_cast<long double>(h))), 1e-6);
      }
    }
  }
  Graph dead{5,
             {{0, 1, {'R', 10, 0}},
              {0, 2, {'R', 1, 0}},
              {2, 3, {'R', 2, 0}},
              {3, 0, {'R', 3, 0}}}};
  auto red = reduce(dead);
  require(red.graph.edges.size() == 1 && red.dropped.size() == 3,
          "pendant triangle");
  close(forward(dead, 20).z, 10);
  Graph ser{
      4, {{0, 2, {'R', 5, 0}}, {2, 3, {'L', .001, 0}}, {3, 1, {'L', .002, 1}}}};
  auto r = reduce(ser);
  require(r.graph.edges.size() == 1 && r.graph.vertices == 4,
          "series reduce/sparse nodes");
  close(forward(r.graph, 1e3).z, forward(ser, 1e3).z);
  Graph pl{2, {{0, 1, {'L', 1e-3, 1}}, {0, 1, {'L', 2e-3, 3}}}};
  require(reduce(pl).graph.edges.size() == 2,
          "parallel physical inductors retained");
  Graph ideal{2, {{0, 1, {'L', 1, 0}}, {0, 1, {'C', 1, 0}}}};
  require(forward(ideal, 1 / (2 * pi)).status == SolveStatus::SINGULAR,
          "exact antiresonance");
  auto nearResonance = forward(ideal, (1 + 1e-14) / (2 * pi));
  require(nearResonance.status == SolveStatus::ILL_CONDITIONED &&
              nearResonance.rcond < 1e-12,
          "scalar LC cancellation diagnosis");
  require(forward({3, {{0, 2, {'R', 1, 0}}}}, 10).status ==
              SolveStatus::PORT_OPEN,
          "open port");
}
// Independent edge-labeled assignment oracle: all ordered endpoints; no
// structural pruning.
std::string referenceSignature(const Graph &g) {
  std::vector<int> labels(g.vertices);
  std::iota(labels.begin(), labels.end(), 0);
  std::string best;
  do {
    if (labels[0] > 1 || labels[1] > 1)
      continue;
    std::vector<std::string> edges;
    for (auto b : g.edges) {
      int u = labels[b.u], v = labels[b.v];
      if (u > v)
        std::swap(u, v);
      std::ostringstream str;
      str << u << ',' << v << ':' << b.element.type << ':' << std::hexfloat
          << b.element.parameter << ':'
          << b.element.parameterOfCapacitanceDCResistance;
      edges.push_back(str.str());
    }
    std::sort(edges.begin(), edges.end());
    std::string key;
    for (auto e : edges)
      key += e + ';';
    if (best.empty() || key < best)
      best = key;
  } while (std::next_permutation(labels.begin(), labels.end()));
  return best;
}
std::set<std::string> enumerationOracle(int E,
                                        const std::vector<Edge> &colored = {}) {
  std::set<std::string> out;
  for (int V = 2; V <= E + 1; ++V) {
    Graph g;
    g.vertices = V;
    std::function<void(int, int)> dfs = [&](int k, int first) {
      if (k == E) {
        std::vector<bool> used(V);
        for (auto b : g.edges) {
          used[b.u] = used[b.v] = true;
        }
        if (std::find(used.begin(), used.end(), false) != used.end())
          return;
        // Independent liveness oracle: mark edges on any simple terminal path.
        std::vector<bool> seen(V), live(E);
        std::vector<int> path;
        std::function<void(int)> walk = [&](int u) {
          if (u == 1) {
            for (int e : path)
              live[e] = true;
            return;
          }
          seen[u] = true;
          for (int j = 0; j < E; ++j) {
            auto b = g.edges[j];
            int v = b.u == u ? b.v : b.v == u ? b.u : -1;
            if (v >= 0 && !seen[v]) {
              path.push_back(j);
              walk(v);
              path.pop_back();
            }
          }
          seen[u] = false;
        };
        walk(0);
        if (std::find(live.begin(), live.end(), false) == live.end())
          out.insert(referenceSignature(g));
        return;
      }
      int s = 0;
      for (int u = 0; u < V; ++u)
        for (int v = u + 1; v < V; ++v, ++s)
          if (s >= first) {
            g.edges.push_back(
                {u, v, colored.empty() ? Edge{'R', 1, 0} : colored[k]});
            dfs(k + 1, colored.empty() ? s : 0);
            g.edges.pop_back();
          }
    };
    dfs(0, 0);
  }
  return out;
}
void enumeration() {
  for (int E = 1; E <= 5; ++E) {
    std::set<std::string> found;
    enumerate(std::vector<Edge>(E, {'R', 1, 0}), [&](auto g) {
      require(found.insert(canonical(g)).second, "enumeration duplicates");
      return true;
    });
    require(found == enumerationOracle(E),
            "complete signatures E=" + std::to_string(E));
  }
  std::vector<Edge> colored{{'R', 10, 0}, {'L', .001, 0}, {'C', 1e-6, 0}};
  std::set<std::string> found;
  enumerate(colored, [&](auto g) {
    require(canonical(g) == referenceSignature(g), "colored canonical oracle");
    found.insert(canonical(g));
    return true;
  });
  require(found == enumerationOracle(3, colored),
          "colored assignment completeness");
  Graph target{2, {{0, 1, {'R', 120, 0}}, {0, 1, {'C', 1e-6, 0}}}};
  auto d = sample(target);
  Config c;
  c.topK = 8;
  auto r = try2(d, {target.edges[0].element, target.edges[1].element}, c);
  require(r.enumerationComplete && r.continuousGlobalCertified,
          "strict certification");
  require(r.candidates.size() == 2 && r.candidates[0].metrics.wrmse < 1e-12,
          "Top-K classes");
  c.candidateBudget = 1;
  r = try2(d, {target.edges[0].element, target.edges[1].element}, c);
  require(!r.enumerationComplete && r.termination == "budget_exhausted",
          "budget status");
}
void fitting() {
  Config c;
  c.starts = 12;
  c.iterations = 180;
  Graph g{2, {{0, 1, {'R', 1200, 0}}, {0, 1, {'C', 2e-7, 0}}}};
  auto d = sample(g);
  require(!rationalStarts(d, 4).empty(), "rational RC synthesis");
  Graph tank{
      2,
      {{0, 1, {'R', 100, 0}}, {0, 1, {'L', .001, 0}}, {0, 1, {'C', 1e-6, 0}}}};
  require(!rationalStarts(sample(tank), 4).empty(),
          "rational resonator synthesis");
  auto r = try3(d, g, c);
  require(r.candidates[0].metrics.wrmse < 1e-7, "RC recovery");
  close(r.candidates[0].graph.edges[0].element.parameter, 1200, 1e-5);
  require(r.candidates[0].diagnostics.rank == 2, "RC rank");
  require(r.candidates[0].diagnostics.confidenceIntervals95.size() == 2,
          "RC approximate intervals");
  auto exactRobust = c;
  exactRobust.robust = true;
  rejects([&] { try2(d, {{'R', 1200, 0}, {'C', 2e-7, 0}}, exactRobust); });
  auto invalidTolerance = c;
  invalidTolerance.dcrAbsoluteTolerance = 1;
  rejects([&] { validate(d, invalidTolerance); });
  Graph ideal{2, {{0, 1, {'L', .001, 0}}}};
  auto a = try3(sample(ideal), ideal, c).candidates[0];
  require(a.metrics.wrmse < 1e-7 &&
              a.graph.edges[0].element.parameterOfCapacitanceDCResistance <
                  1e-8,
          "zero DCR boundary");
  c.exactN = 2;
  auto t = try1(d, c);
  require(t.enumerationComplete && t.candidates[0].metrics.wrmse < 1e-7 &&
              t.candidates[0].graph.edges.size() == 2,
          "Try1 count recovery");
  c.exactN.reset();
  auto p = try25(d, {'R', 'C'}, c);
  require(p.enumerationComplete && !p.continuousGlobalCertified &&
              p.candidates[0].metrics.wrmse < 1e-7,
          "Try2.5 composition");
  auto nominal = std::vector<Edge>{{'R', 1100, 0}, {'C', 2.1e-7, 0}};
  c.tolerance = .2;
  auto tol = try2(d, nominal, c);
  require(tol.candidates[0].refined && tol.candidates[0].metrics.wrmse < 1e-7 &&
              !tol.continuousGlobalCertified,
          "tolerance");
  c.tolerance = 0;
  rejects([&] { metrics({}, {}, 0, c); });
  Config correlated = c;
  Eigen::Matrix2d sigma;
  sigma << 2, .5, .5, 1;
  correlated.covariance = {sigma};
  require(std::abs(metrics({{1, {0, 0}}}, {{1, 2}}, 0, correlated).rss - 4) <
              1e-12,
          "correlated GLS objective");
  Data small{{1, {1, 0}}};
  auto m = metrics(small, {{1, 0}}, 2, c);
  require(!m.aicc, "invalid AICc");
  c.covariance.assign(d.size(), Eigen::Matrix2d::Identity());
  c.covariance[0](0, 0) = -1;
  rejects([&] { try3(d, g, c); });
  c.covariance.assign(d.size(), Eigen::Matrix2d::Identity());
  require(try3(d, g, c).candidates[0].metrics.wrmse < 1e-7, "GLS recovery");
  c.covariance.clear();
  auto bad = d;
  bad[8].z *= 3.;
  c.robust = true;
  auto rob = try3(bad, g, c).candidates[0];
  require(rob.diagnostics.robustUsed && rob.diagnostics.outliers > 0,
          "robust diagnostics");
  require(rob.metrics.maxRel > .5, "raw metrics retained");
  Config budget = c;
  budget.robust = false;
  budget.seconds = 1e-3;
  budget.starts = 1000;
  auto interrupted = try3(d, g, budget);
  require(!interrupted.enumerationComplete, "LM time budget");
  auto triangle = adjacency(g);
  Graph rebuilt;
  rebuilt.vertices = g.vertices;
  for (int u = 0; u < g.vertices; ++u)
    for (int v = u + 1; v < g.vertices; ++v)
      for (auto e : triangle[u][v - u - 1])
        rebuilt.edges.push_back({u, v, e});
  close(Complex(oracle(rebuilt, 1234)), forward(g, 1234).z, 1e-10);
  std::ostringstream json;
  report(json, r, d, c, true);
  require(json.str().find("\"schema\":\"lcr.native.v4\"") !=
                  std::string::npos &&
              json.str().find("nan") == std::string::npos,
          "JSON diagnostics");
}
} // namespace
int main() {
  try {
    io();
    physics();
    enumeration();
    fitting();
    std::cout << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
    return 1;
  }
}
