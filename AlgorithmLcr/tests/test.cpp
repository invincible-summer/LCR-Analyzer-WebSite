#include "lcr/lcr.hpp"
#include "../src/numerics.hpp"
#include "../src/selection.hpp"
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
std::string exprKey(const ReductionExpr &e) {
  switch (e.op) {
  case ExprOp::PrimitiveValue:
    return "v" + std::to_string(e.sourceEdge);
  case ExprOp::PrimitiveDcr:
    return "d" + std::to_string(e.sourceEdge);
  case ExprOp::Sum: {
    std::string s = "(+";
    for (auto &c : e.children)
      s += ' ' + exprKey(c);
    return s + ')';
  }
  default: {
    std::string s = "(h";
    for (auto &c : e.children)
      s += ' ' + exprKey(c);
    return s + ')';
  }
  }
}
bool contains(const Interval &iv, double v) {
  return iv.lo <= v && v <= iv.hi;
}
void sameImpedance(const Graph &original, const Reduction &r,
                   const std::string &what, int points = 24) {
  require(r.graph.edges.size() >= 1, what + ": nonempty reduction");
  require(r.groups.size() == r.graph.edges.size() &&
              r.domains.size() == r.graph.edges.size(),
          what + ": groups/domains aligned with edges");
  for (int i = 0; i < points; ++i) {
    double f = 10 * std::pow(1e5, double(i) / (points - 1));
    auto a = forward(original, f, false), b = forward(r.graph, f, false);
    require(a.status == SolveStatus::OK && b.status == SolveStatus::OK,
            what + ": solver status");
    double rel = std::abs(a.z - b.z) / std::max(1., std::abs(a.z));
    require(rel <= 1e-11,
            what + ": Z preserved, rel=" + std::to_string(rel) + " @f=" +
                std::to_string(f));
  }
}
std::vector<EdgeDomain> leafDomains(const Graph &g, const Config &c) {
  std::vector<EdgeDomain> src;
  for (auto b : g.edges)
    src.push_back(edgeDomain(b.element, c));
  return src;
}
void reductionProperties() {
  Config c;
  // --- exact reductions preserve Z and carry explicit expressions ---
  struct Case {
    std::string name;
    Graph g;
    std::string valueKey;
    std::string dcrKey; // empty when no DCR aggregate exists
    std::vector<int> members;
  };
  std::vector<Case> cases{
      {"parR",
       {2, {{0, 1, {'R', 100, 0}}, {0, 1, {'R', 330, 0}}}},
       "(h v0 v1)",
       "",
       {0, 1}},
      {"parC",
       {2, {{0, 1, {'C', 1e-7, 0}}, {0, 1, {'C', 2.2e-7, 0}}}},
       "(+ v0 v1)",
       "",
       {0, 1}},
      {"serR",
       {3, {{0, 2, {'R', 100, 0}}, {2, 1, {'R', 330, 0}}}},
       "(+ v0 v1)",
       "",
       {0, 1}},
      {"serC",
       {3, {{0, 2, {'C', 1e-7, 0}}, {2, 1, {'C', 2.2e-7, 0}}}},
       "(h v0 v1)",
       "",
       {0, 1}},
      {"serL",
       {4, {{0, 2, {'L', 1e-3, 2}}, {2, 1, {'L', 2e-3, 3}}}},
       "(+ v0 v1)",
       "(+ d0 d1)",
       {0, 1}},
      {"serRL",
       {3, {{0, 2, {'R', 47, 0}}, {2, 1, {'L', 1e-3, 2}}}},
       "v1",
       "(+ d1 v0)",
       {0, 1}},
      {"nestedRR",
       {3,
        {{0, 2, {'R', 100, 0}},
         {0, 2, {'R', 100, 0}},
         {2, 1, {'R', 50, 0}}}},
       "(+ (h v0 v1) v2)",
       "",
       {0, 1, 2}},
      {"nestedCC",
       {3,
        {{0, 2, {'C', 1e-7, 0}},
         {0, 2, {'C', 1e-7, 0}},
         {2, 1, {'C', 1e-7, 0}}}},
       "(h (+ v0 v1) v2)",
       "",
       {0, 1, 2}},
      {"nestedLLR",
       {5,
        {{0, 2, {'L', 1e-3, 2}},
         {2, 3, {'L', 2e-3, 3}},
         {3, 1, {'R', 50, 0}}}},
       "(+ v0 v1)",
       "(+ (+ d0 d1) v2)",
       {0, 1, 2}}};
  std::mt19937 rng(7);
  for (auto &k : cases) {
    auto r = reduce(k.g, c);
    sameImpedance(k.g, r, k.name);
    require(r.graph.edges.size() == 1, k.name + ": single effective edge");
    require(exprKey(r.groups[0].valueExpr) == k.valueKey,
            k.name + ": value expression");
    if (k.dcrKey.empty())
      require(!r.groups[0].dcrExpr, k.name + ": no DCR expression");
    else
      require(r.groups[0].dcrExpr &&
                  exprKey(*r.groups[0].dcrExpr) == k.dcrKey,
              k.name + ": DCR expression");
    require(r.groups[0].members == k.members, k.name + ": members");
    // The effective value always lies inside the propagated interval, and
    // random legal leaf combinations stay inside it too.
    auto src = leafDomains(k.g, c);
    const auto &expr = r.groups[0].valueExpr;
    auto vb = bounds(expr, src);
    require(contains(vb, evaluate(expr, k.g)), k.name + ": value in bounds");
    if (r.groups[0].dcrExpr) {
      auto db = bounds(*r.groups[0].dcrExpr, src);
      require(contains(db, evaluate(*r.groups[0].dcrExpr, k.g)),
              k.name + ": DCR in bounds");
      for (int trial = 0; trial < 50; ++trial) {
        Graph q = k.g;
        for (size_t e = 0; e < q.edges.size(); ++e) {
          auto &dom = src[e];
          double w = std::pow(10., std::uniform_real_distribution<double>(
                                        std::log10(dom.value.lo),
                                        std::log10(dom.value.hi))(rng));
          q.edges[e].element.parameter = w;
          if (q.edges[e].element.type == 'L' && dom.dcr)
            q.edges[e].element.parameterOfCapacitanceDCResistance =
                std::uniform_real_distribution<double>(dom.dcr->lo,
                                                       dom.dcr->hi)(rng);
        }
        require(contains(bounds(expr, src), evaluate(expr, q)) &&
                    contains(bounds(*r.groups[0].dcrExpr, src),
                             evaluate(*r.groups[0].dcrExpr, q)),
                k.name + ": random leaf sample inside propagated domain");
      }
    } else
      for (int trial = 0; trial < 50; ++trial) {
        Graph q = k.g;
        for (size_t e = 0; e < q.edges.size(); ++e)
          q.edges[e].element.parameter = std::pow(
              10., std::uniform_real_distribution<double>(
                       std::log10(src[e].value.lo),
                       std::log10(src[e].value.hi))(rng));
        require(contains(bounds(expr, src), evaluate(expr, q)),
                k.name + ": random leaf sample inside propagated domain");
      }
  }
  // --- pendant component dead zone: Z preserved, dropped Jacobian zero ---
  Graph dead{5,
             {{0, 1, {'R', 10, 0}},
              {0, 2, {'R', 1, 0}},
              {2, 3, {'R', 2, 0}},
              {3, 0, {'R', 3, 0}}}};
  auto dr = reduce(dead, c);
  sameImpedance(dead, dr, "pendant");
  auto fr = forward(dead, 123., true);
  require(fr.jacobian.size() == 4, "pendant: jacobian size");
  for (size_t k = 1; k < 4; ++k)
    require(fr.jacobian[k] == Complex(0, 0),
            "pendant: dropped edge jacobian zero");
  // --- aggregate boundary containment: legal member combinations that the
  // old single-device bounds rejected must lie inside the propagated domain ---
  struct Boundary {
    std::string name;
    Graph g;
    double expectValue; // NaN when only the DCR aggregate matters
    double expectDcr;
  };
  std::vector<Boundary> limits{
      {"rMin||rMin",
       {2, {{0, 1, {'R', c.rMin, 0}}, {0, 1, {'R', c.rMin, 0}}}},
       c.rMin / 2,
       -1},
      {"rMax+rMax",
       {3, {{0, 2, {'R', c.rMax, 0}}, {2, 1, {'R', c.rMax, 0}}}},
       2 * c.rMax,
       -1},
      {"cMax||cMax",
       {2, {{0, 1, {'C', c.cMax, 0}}, {0, 1, {'C', c.cMax, 0}}}},
       2 * c.cMax,
       -1},
      {"cMin ser cMin",
       {3, {{0, 2, {'C', c.cMin, 0}}, {2, 1, {'C', c.cMin, 0}}}},
       c.cMin / 2,
       -1},
      {"lMax+lMax",
       {3, {{0, 2, {'L', c.lMax, 0}}, {2, 1, {'L', c.lMax, 0}}}},
       2 * c.lMax,
       -1},
      {"dcrMax+dcrMax",
       {3,
        {{0, 2, {'L', 1e-3, c.dcrMax}}, {2, 1, {'L', 1e-3, c.dcrMax}}}},
       2e-3,
       2 * c.dcrMax},
      {"L(dcrMax)+rMax",
       {3, {{0, 2, {'R', c.rMax, 0}}, {2, 1, {'L', 1e-3, c.dcrMax}}}},
       1e-3,
       c.dcrMax + c.rMax}};
  for (auto &b : limits) {
    auto r = reduce(b.g, c);
    sameImpedance(b.g, r, b.name);
    require(r.graph.edges.size() == 1, b.name + ": single group");
    auto &dom = r.domains[0];
    if (std::isfinite(b.expectValue)) {
      require(contains(dom.value, b.expectValue),
              b.name + ": aggregate value inside domain");
      close(Complex(r.graph.edges[0].element.parameter, 0),
            Complex(b.expectValue, 0), 1e-12);
    }
    if (b.expectDcr >= 0)
      require(dom.dcr && contains(*dom.dcr, b.expectDcr),
              b.name + ": aggregate DCR inside domain");
  }
  // Nested expression evaluated on boundary leaves stays inside the
  // propagated interval.
  Graph nested{3,
               {{0, 2, {'R', c.rMax, 0}},
                {0, 2, {'R', c.rMax, 0}},
                {2, 1, {'R', c.rMax, 0}}}};
  auto nr = reduce(nested, c);
  require(contains(nr.domains[0].value, 1.5 * c.rMax),
          "nested boundary aggregate inside domain");
  // --- nominal tolerance leaf domains ---
  Config t;
  t.tolerance = .2;
  Graph lr{2, {{0, 1, {'L', .001, 0}}, {0, 1, {'R', 1000, 0}}}};
  auto doms = leafDomains(lr, t);
  require(contains(doms[0].value, .001) && doms[0].value.lo >= .001 * .8 &&
              doms[0].value.hi <= .001 * 1.2,
          "tolerance L value box");
  require(doms[0].dcr && doms[0].dcr->lo == 0 && doms[0].dcr->hi == 0,
          "nominal zero DCR with zero absolute tolerance stays fixed");
  require(contains(doms[1].value, 1000) && doms[1].value.lo == 800 &&
              doms[1].value.hi == 1200,
          "tolerance R value box");
  Graph outside{2, {{0, 1, {'R', 1e9, 0}}}};
  rejects([&] { leafDomains(outside, t); });
}

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
  // Try2.5 shares the Try3 prepared inner fit: aggregates collapse.
  Graph ser2{3, {{0, 2, {'R', 100, 0}}, {2, 1, {'R', 300, 0}}}};
  auto p25 = try25(sample(ser2), {'R', 'R'}, c);
  const auto &w25 = p25.candidates[0];
  require(p25.enumerationComplete && w25.nParams == 1 &&
              w25.effectiveDevices == 1 && w25.metrics.wrmse < 1e-7,
          "Try2.5 series R fits one aggregate");
  require(w25.originalTopologyKey != w25.effectiveTopologyKey,
          "Try2.5 keeps original vs effective topology keys");
  Graph serRl{3, {{0, 2, {'R', 47, 0}}, {2, 1, {'L', 1., 2}}}};
  auto q25 = try25(sample(serRl), {'R', 'L'}, c);
  const auto &v25 = q25.candidates[0];
  require(v25.nParams == 2 && v25.effectiveDevices == 1 &&
              v25.metrics.wrmse < 1e-7,
          "Try2.5 R+L series: main L plus aggregate DCR");
  close(Complex(v25.graph.edges[0].element
                    .parameterOfCapacitanceDCResistance,
                0),
        Complex(49., 0), 1e-6);
  // Try3 recovers aggregate equivalents at the domain boundary without
  // single-device clamping and never fabricates per-member values.
  struct Aggregate {
    std::string name;
    Graph g;
    double value, dcr; // expected effective equivalents (dcr<0: none)
  };
  std::vector<Aggregate> aggs{
      {"rMin||rMin",
       {2, {{0, 1, {'R', 1e-3, 0}}, {0, 1, {'R', 1e-3, 0}}}},
       5e-4,
       -1},
      {"rMax+rMax",
       {3, {{0, 2, {'R', 1e7, 0}}, {2, 1, {'R', 1e7, 0}}}},
       2e7,
       -1},
      {"cMax||cMax",
       {2, {{0, 1, {'C', 1e-3, 0}}, {0, 1, {'C', 1e-3, 0}}}},
       2e-3,
       -1},
      {"cMin ser cMin",
       {3, {{0, 2, {'C', 1e-13, 0}}, {2, 1, {'C', 1e-13, 0}}}},
       5e-14,
       -1},
      {"lMax+lMax",
       {3, {{0, 2, {'L', 10, 0}}, {2, 1, {'L', 10, 0}}}},
       20,
       0},
      {"dcrMax+dcrMax",
       {3, {{0, 2, {'L', 1, 1e7}}, {2, 1, {'L', 1, 1e7}}}},
       2,
       2e7},
      {"L(dcrMax)+rMax",
       {3, {{0, 2, {'R', 1e7, 0}}, {2, 1, {'L', 1, 1e7}}}},
       1,
       2e7}};
  for (auto &agg : aggs) {
    auto rr = try3(sample(agg.g), agg.g, c);
    const auto &cand = rr.candidates[0];
    require(cand.metrics.wrmse < 1e-8,
            agg.name + ": Try3 aggregate recovery wrmse");
    const auto &e = cand.graph.edges[0].element;
    close(Complex(e.parameter, 0), Complex(agg.value, 0), 1e-8);
    if (agg.dcr >= 0)
      close(Complex(e.parameterOfCapacitanceDCResistance, 0),
            Complex(agg.dcr, 0), 1e-8);
    require(cand.reduction.groups.size() == 1 &&
                cand.reduction.groups[0].members.size() == 2 &&
                cand.graph.edges.size() == 1,
            agg.name + ": aggregate group, no fabricated member values");
  }
  auto nominal = std::vector<Edge>{{'R', 1100, 0}, {'C', 2.1e-7, 0}};
  c.tolerance = .2;
  auto tol = try2(d, nominal, c);
  require(tol.candidates[0].refined && tol.candidates[0].metrics.wrmse < 1e-7 &&
              !tol.continuousGlobalCertified,
          "tolerance");
  // Fixed nominal-zero DCR must report as fixed, never as at-bound.
  Graph rl{2, {{0, 1, {'L', 1e-3, 0}}, {0, 1, {'R', 100, 0}}}};
  auto rld = sample(rl);
  auto tolLr = try2(rld, {{'L', 1e-3, 0}, {'R', 100, 0}}, c);
  const auto &fc = tolLr.candidates[0];
  require(fc.refined && fc.metrics.wrmse < 1e-7, "fixed DCR tolerance fit");
  require(fc.diagnostics.verdict == "IDENTIFIABLE_LOCAL",
          "fixed DCR is not fit-unconfirmed");
  require(fc.nParams == 2, "fixed parameter excluded from nParams");
  require(fc.diagnostics.rank == 2, "rank counts free parameters only");
  require(fc.diagnostics.atBound.empty(), "no at-bound from fixed parameters");
  const auto &fparams = fc.diagnostics.parameters;
  require(fparams.size() == 3, "L value/DCR and R value descriptors");
  const ParameterDiagnostic *lDcr = nullptr;
  for (auto &pd : fparams)
    if (pd.kind == 'L' && pd.quantity == ParamQuantity::Dcr)
      lDcr = &pd;
  require(lDcr && lDcr->fixed && !lDcr->free && !lDcr->atBound && !lDcr->weak &&
              lDcr->value == 0,
          "nominal zero DCR: fixed state, not at-bound");
  for (auto &pd : fparams)
    if (pd.fixed)
      require(!pd.standardError && !pd.ci95, "fixed parameter has no SE/CI");
    else
      require(pd.standardError && pd.ci95, "free parameter carries SE/CI");
  require(fc.diagnostics.standardErrors.size() == 2 &&
              fc.diagnostics.confidenceIntervals95.size() == 2,
          "legacy SE/CI arrays stay free-only");
  // prepareForFit is the sole model-domain constructor.
  auto identity = prepareForFit(rl, c, ReductionPolicy::None);
  require(identity.effective.edges.size() == 2 &&
              identity.domains.size() == 2 &&
              identity.reduction.groups.size() == 2 &&
              identity.reduction.dropped.empty(),
          "identity prepared network");
  auto physical = prepareForFit(rl, Config{}, ReductionPolicy::ExactElectrical);
  require(physical.effective.edges.size() == 2,
          "parallel L+R stays physically separate");
  // Aggregate fit must recover equivalents beyond single-device bounds.
  Graph twoR{3, {{0, 2, {'R', 6e6, 0}}, {2, 1, {'R', 6e6, 0}}}};
  auto agg = prepareForFit(twoR, Config{}, ReductionPolicy::ExactElectrical);
  require(agg.effective.edges.size() == 1 && agg.domains[0].value.hi > 1e7,
          "series R domain exceeds rMax");
  auto aggFit = fit(agg, sample(twoR), Config{});
  require(aggFit.metrics.wrmse < 1e-8 && aggFit.nParams == 1,
          "aggregate recovery beyond single-device bound");
  close(Complex(aggFit.graph.edges[0].element.parameter, 0),
        Complex(1.2e7, 0), 1e-9);
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
  // --- v4.1.2: no-best failure paths must stay total-state safe ---
  // A cancellation that fires before the first start must not decode an
  // unassigned coordinate.
  Config preCancelled = c;
  preCancelled.robust = false;
  preCancelled.cancelled = [] { return true; };
  auto earlyFit =
      fit(prepareForFit(g, preCancelled, ReductionPolicy::None), d, preCancelled);
  require(earlyFit.diagnostics.optimizer == "budget_exhausted" &&
              earlyFit.diagnostics.numericalStatus == "FAIL" &&
              earlyFit.diagnostics.identifiabilityStatus == "NOT_EVALUATED" &&
              earlyFit.diagnostics.verdict == "LOCAL_FIT_UNCONFIRMED" &&
              !std::isfinite(earlyFit.metrics.rss),
          "pre-cancel fit reports exhausted budget safely");
  require(earlyFit.diagnostics.parameters.size() == 2 &&
              earlyFit.diagnostics.parameters[0].id == 0 &&
              earlyFit.diagnostics.parameters[1].id == 1,
          "pre-cancel keeps complete parameter descriptors");
  auto early = try3(d, g, preCancelled);
  require(early.termination == "budget_exhausted" &&
              !early.enumerationComplete && early.candidates.empty() &&
              early.numericalFailures == 1,
          "pre-cancel try3 stays empty with exhausted budget");
  // Fixed L/C domains pin every multi-start at the same parallel-LC
  // antiresonance, where the forward solve is singular: all starts fail
  // numerically and fit() must still return a well-formed candidate.
  Graph antiresonator{2, {{0, 1, {'L', 1, 0}}, {0, 1, {'C', 1, 0}}}};
  PreparedNetwork pinned;
  pinned.original = pinned.effective = antiresonator;
  pinned.reduction.graph = antiresonator;
  EdgeDomain lv, cv;
  lv.value = Interval{1, 1};
  lv.dcr = Interval{0, 0};
  cv.value = Interval{1, 1};
  pinned.domains = {lv, cv};
  Data atResonance{{1e-2, {1e6, 0}},
                   {1. / (2 * pi), {1e12, 0}},
                   {1., {1e3, 0}},
                   {10., {1e3, 0}}};
  auto failed = fit(pinned, atResonance, c);
  require(failed.diagnostics.optimizer == "numerical_failure" &&
              failed.diagnostics.numericalStatus == "FAIL" &&
              failed.diagnostics.identifiabilityStatus == "NOT_EVALUATED" &&
              failed.diagnostics.verdict == "NUMERICALLY_UNSTABLE" &&
              !std::isfinite(failed.metrics.rss),
          "all-failed starts report numerical failure");
  require(failed.diagnostics.parameters.size() == 3,
          "all-failed starts keep full descriptors");
  require(failed.diagnostics.parameters[0].quantity == ParamQuantity::Value &&
              failed.diagnostics.parameters[1].quantity == ParamQuantity::Dcr &&
              failed.diagnostics.parameters[2].quantity == ParamQuantity::Value &&
              failed.diagnostics.parameters[0].edge == 0 &&
              failed.diagnostics.parameters[1].edge == 0 &&
              failed.diagnostics.parameters[2].edge == 1,
          "failure descriptor ids/edges/quantities complete");

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
  auto text = json.str();
  require(text.find("\"schema\":\"lcr.native.v4\"") != std::string::npos &&
              text.find("\"schema_revision\":2") != std::string::npos &&
              text.find("\"engine_version\":\"4.1.0\"") !=
                  std::string::npos &&
              text.find("nan") == std::string::npos,
          "JSON diagnostics");
  require(text.find("\"parameters\":[{\"id\":0") != std::string::npos &&
              text.find("\"quantity\":\"value\"") != std::string::npos &&
              text.find("\"quantity\":\"dcr\"") == std::string::npos &&
              text.find("\"free\":true") != std::string::npos &&
              text.find("\"fixed\":false") != std::string::npos,
          "JSON parameter descriptors");
  require(text.find("\"selection\":") != std::string::npos &&
              text.find("\"equivalence_metric\":\"relative_curve\"") !=
                  std::string::npos,
          "JSON selection contract");
  require(text.find("\"groups\":[{\"gid\":0") != std::string::npos &&
              text.find("\"value_bounds\":[") != std::string::npos &&
              text.find("\"parameter_ids\":[") != std::string::npos &&
              text.find("\"value_expr\":") != std::string::npos &&
              text.find("\"original_topology_key\":") != std::string::npos,
          "JSON group/topology contract");
}
void selectionTiers() {
  using lcr::selection::Tier;
  auto scored = [](double aicc, double rss, const char *optimizer) {
    Candidate a;
    a.metrics.rss = rss;
    a.metrics.wrmse = rss;
    a.metrics.maxRel = rss;
    a.metrics.aicc = aicc;
    a.diagnostics.optimizer = optimizer;
    a.diagnostics.rank = 1;
    a.nParams = 1;
    a.topology = "t";
    return a;
  };
  // A provisional candidate with better current AICc outranks a qualified
  // candidate; it stays unqualified and never shows a calibrated delta.
  std::vector<Candidate> v{scored(100., 10., "converged_gradient"),
                           scored(50., 5., "max_iterations")};
  lcr::selection::annotate(v, false);
  double floor = lcr::selection::qualifiedFloor(v);
  std::stable_sort(v.begin(), v.end(),
                    [](auto &a, auto &b) { return lcr::selection::aheadOf(a, b, false); });
  lcr::selection::assignDeltas(v, floor);
  auto outcome = lcr::selection::outcomeOf(v, false);
  require(v[0].metrics.aicc == 50. && !v[0].selection.eligible &&
              v[0].selection.criterion == "AICc_PROVISIONAL" &&
              !v[0].selection.delta &&
              std::find(v[0].selection.reasons.begin(),
                        v[0].selection.reasons.end(),
                        "optimizer_not_converged") !=
                  v[0].selection.reasons.end(),
          "provisional candidate outranks worse qualified");
  require(v[1].selection.eligible && v[1].selection.delta == 0.,
          "qualified candidate keeps calibrated zero delta");
  require(outcome.criterion == "AICc_PROVISIONAL_ORDER" && !outcome.qualified,
          "provisional rank-1 does not claim qualification");
  // A qualified candidate with better AICc still wins the run.
  v = {scored(50., 1., "converged_gradient"), scored(100., 5., "stalled")};
  lcr::selection::annotate(v, false);
  std::stable_sort(v.begin(), v.end(),
                    [](auto &a, auto &b) { return lcr::selection::aheadOf(a, b, false); });
  outcome = lcr::selection::outcomeOf(v, false);
  require(v[0].selection.eligible &&
              v[1].selection.criterion == "AICc_PROVISIONAL",
          "qualified candidate first when its AICc is better");
  require(outcome.criterion == "AICc" && outcome.qualified,
          "qualified rank-1 keeps calibrated semantics");
  // Diagnostic-only candidates never precede the scored set, whatever their
  // raw RSS.
  Candidate noAicc;
  noAicc.metrics.rss = .5;
  noAicc.metrics.wrmse = .5;
  noAicc.metrics.maxRel = .5;
  noAicc.diagnostics.optimizer = "converged_gradient";
  noAicc.diagnostics.rank = 1;
  noAicc.nParams = 1;
  noAicc.topology = "d";
  v = {scored(100., 10., "max_iterations"),
       scored(80., 5., "converged_gradient"), noAicc};
  lcr::selection::annotate(v, false);
  std::stable_sort(v.begin(), v.end(),
                    [](auto &a, auto &b) { return lcr::selection::aheadOf(a, b, false); });
  require(lcr::selection::tierOf(v[0]) == Tier::Qualified &&
              lcr::selection::tierOf(v[1]) == Tier::Provisional &&
              lcr::selection::tierOf(v[2]) == Tier::Diagnostic &&
              v[2].metrics.rss < v[0].metrics.rss,
          "scored set precedes diagnostics regardless of RSS");
  require(std::find(v[2].selection.reasons.begin(), v[2].selection.reasons.end(),
                    "aicc_unavailable") != v[2].selection.reasons.end(),
          "null-AICc candidate keeps its reason");
  // Robust runs never expose calibrated AICc semantics.
  v = {scored(10., 1., "converged_gradient")};
  v[0].diagnostics.robustUsed = true;
  lcr::selection::annotate(v, true);
  require(lcr::selection::outcomeOf(v, true).criterion ==
                  "RSS_DIAGNOSTIC_FALLBACK" &&
              !lcr::selection::outcomeOf(v, true).qualified,
          "robust run outcome stays fallback");
  require(!v[0].selection.eligible && v[0].selection.criterion == "NONE" &&
              std::find(v[0].selection.reasons.begin(),
                        v[0].selection.reasons.end(),
                        "robust_run") != v[0].selection.reasons.end(),
          "robust run stays diagnostic");
  // Equivalence-class representatives prefer the most reliable tier.
  Candidate provisionalRep = scored(30., 5., "max_iterations");
  Candidate qualifiedRep = scored(100., 10., "converged_gradient");
  require(lcr::selection::betterRepresentative(qualifiedRep, provisionalRep,
                                               false),
          "qualified member represents over provisional");
  require(!lcr::selection::betterRepresentative(provisionalRep, qualifiedRep,
                                                false),
          "provisional never displaces a qualified representative");
  Candidate tight = scored(20., 4., "converged_gradient");
  require(lcr::selection::betterRepresentative(tight, qualifiedRep, false) &&
              !lcr::selection::betterRepresentative(qualifiedRep, tight, false),
          "same tier prefers smaller AICc");
  Candidate wide = noAicc;
  wide.metrics.rss = 7.;
  require(lcr::selection::betterRepresentative(noAicc, wide, false),
          "same diagnostic tier prefers smaller RSS");
  require(lcr::selection::betterRepresentative(wide, noAicc, true) == false,
          "robust representative comparison ignores tiers");
}

void forwardPolicy() {
  using numerics::ForwardDisposition;
  Forward f;
  f.status = SolveStatus::OK;
  require(numerics::classify(f) == ForwardDisposition::Accept,
          "classify OK accepts");
  f.status = SolveStatus::ILL_CONDITIONED;
  f.rcond = 1e-13;
  f.backwardError = 1e-12;
  require(numerics::classify(f) == ForwardDisposition::Warn,
          "classify rcond warning window");
  f.rcond = numerics::policy.rcondReject / 10;
  f.backwardError = 0;
  require(numerics::classify(f) == ForwardDisposition::Reject,
          "classify below rcondReject");
  f.rcond = 1e-13;
  f.backwardError = numerics::policy.backwardReject * 10;
  require(numerics::classify(f) == ForwardDisposition::Reject,
          "classify backward reject");
  f.status = SolveStatus::SINGULAR;
  f.backwardError = 0;
  require(numerics::classify(f) == ForwardDisposition::Reject,
          "classify singular rejects");
  // A real near-antiresonance point lands exactly in the warning window.
  Graph ideal{2, {{0, 1, {'L', 1, 0}}, {0, 1, {'C', 1, 0}}}};
  auto near = forward(ideal, (1 + 1e-14) / (2 * pi));
  require(near.status == SolveStatus::ILL_CONDITIONED &&
              numerics::classify(near) == ForwardDisposition::Warn,
          "near antiresonance classified as warning");
  // Try2 Exact retains the warned candidate and withholds the certificate.
  Data warnData;
  for (double f0 : {1e-2, (1 + 1e-14) / (2 * pi), 1., 10.})
    warnData.push_back({f0, forward(ideal, f0, false).z});
  Config c;
  auto warn = try2(warnData, {{'L', 1, 0}, {'C', 1, 0}}, c);
  require(warn.enumerationComplete, "warn run completes enumeration");
  require(warn.termination == "complete_with_numerical_warnings" &&
              !warn.continuousGlobalCertified,
          "warning withholds exact certificate");
  const Candidate *warned = nullptr;
  for (auto &cand : warn.candidates)
    if (cand.graph.vertices == 2 && cand.graph.edges.size() == 2)
      warned = &cand;
  require(warned && warned->diagnostics.numericalStatus == "WARN" &&
              warned->diagnostics.identifiabilityStatus == "NOT_APPLICABLE",
          "warning-level candidate retained with WARN status");
  // A truly singular point still rejects and counts as a numerical failure.
  Data exactData = warnData;
  exactData[1] = {1. / (2 * pi), {1e12, 0}};
  auto failed = try2(exactData, {{'L', 1, 0}, {'C', 1, 0}}, c);
  require(failed.numericalFailures >= 1 && !failed.continuousGlobalCertified &&
              failed.termination == "complete_with_numerical_failures",
          "singular point still rejects as failure");
}
void knownTopology() {
  // Try3 accepts known topologies with more internal nodes than the
  // diagnostic canonical-labeling helper supports; its topology keys are
  // preserve-label identities, stable under edge row reordering.
  std::vector<int> path{0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 1};
  Graph chain{11};
  for (size_t i = 0; i + 1 < path.size(); ++i)
    chain.edges.push_back({path[i], path[i + 1], {'R', 1000, 0}});
  rejects([&] { canonical(chain, false); });
  auto chainData = sample(chain);
  Config c;
  auto r = try3(chainData, chain, c);
  const auto &best = r.candidates[0];
  require(best.metrics.wrmse < 1e-8,
          "chain: Try3 recovery beyond 8 internal nodes");
  require(best.graph.edges.size() == 1,
          "chain: exact reduction collapses the chain");
  require(best.originalTopologyKey.find("0,2:R") != std::string::npos &&
              best.originalTopologyKey.find("1,10:R") != std::string::npos,
          "chain: original key preserves user labels");
  require(best.effectiveTopologyKey.find("0,1:R") != std::string::npos,
          "chain: effective key nonempty and labeled");
  std::mt19937 shuffler(3);
  for (int trial = 0; trial < 20; ++trial) {
    Graph shuffled = chain;
    std::shuffle(shuffled.edges.begin(), shuffled.edges.end(), shuffler);
    auto rs = try3(chainData, shuffled, c);
    require(rs.candidates[0].originalTopologyKey ==
                    best.originalTopologyKey &&
                rs.candidates[0].effectiveTopologyKey ==
                    best.effectiveTopologyKey,
            "chain: keys invariant under edge row reordering");
  }
}
void selectorScenarios() {
  Config c;
  c.starts = 12;
  c.iterations = 180;
  Graph rc{2, {{0, 1, {'R', 1200, 0}}, {0, 1, {'C', 2e-7, 0}}}};
  // 1) all-valid comparison set keeps the calibrated AICc ranking.
  c.exactN = 2;
  auto all = try1(sample(rc), c);
  require(all.selectionCriterion == "AICc" && all.selectionQualified,
          "all-valid AICc selection");
  require(!all.candidates.empty() && all.candidates[0].selection.eligible &&
              all.candidates[0].selection.delta == 0.,
          "primary candidate first with zero delta");
  // 3) rank-deficient exact-fit continuum (parallel L on ideal-L data).
  Graph ideal{2, {{0, 1, {'L', 1e-3, 0}}}};
  auto parallel = try1(sample(ideal), c);
  bool rankDeficient = false;
  for (auto &cand : parallel.candidates)
    if (!cand.selection.eligible)
      for (auto &why : cand.selection.reasons)
        rankDeficient = rankDeficient || why == "rank_deficient";
  require(rankDeficient, "rank-deficient candidate recorded as diagnostic");
  // 4) at-bound candidate stays diagnostic-only without demoting others.
  c.exactN.reset();
  c.maxN = 3;
  c.topK = 40;
  auto bound = try1(sample(rc), c);
  require(bound.selectionCriterion == "AICc" && bound.selectionQualified,
          "at-bound candidate does not force RSS fallback");
  bool atBoundDiagnostic = false;
  for (auto &cand : bound.candidates)
    if (std::find(cand.selection.reasons.begin(), cand.selection.reasons.end(),
                  "parameter_at_bound") != cand.selection.reasons.end()) {
      atBoundDiagnostic = true;
      require(!cand.selection.eligible && !cand.selection.delta,
              "at-bound candidate is diagnostic-only without delta");
    }
  require(atBoundDiagnostic, "at-bound candidate present");
  require(bound.candidates[0].selection.eligible, "rank-1 is primary");
  c.maxN = 4;
  c.topK = 8;
  // 2) mixed valid/invalid AICc set keeps qualified AICc ranking: a single
  //    over-parameterized null-AICc candidate must not demote the whole set.
  auto mix = try1(sample(rc, 3), c);
  require(mix.selectionCriterion == "AICc" && mix.selectionQualified &&
              mix.candidates[0].selection.eligible,
          "null-AICc candidate does not demote the whole set");
  bool nullSeen = false;
  for (auto &cand : mix.candidates)
    if (!cand.metrics.aicc) {
      nullSeen = true;
      require(!cand.selection.eligible &&
                  std::find(cand.selection.reasons.begin(),
                            cand.selection.reasons.end(),
                            "aicc_unavailable") !=
                      cand.selection.reasons.end(),
              "null-AICc candidate is diagnostic with reason");
    }
  require(nullSeen, "mixed set contains null-AICc candidates");
  // v4.1.2: the AICc-scored set (qualified union provisional) precedes the
  // diagnostic-only tail; a provisional may legitimately outrank a qualified
  // candidate by current AICc, so eligibility alone is no longer the tier
  // boundary.
  for (size_t i = 1; i < mix.candidates.size(); ++i)
    if (mix.candidates[i].selection.criterion != "NONE")
      require(mix.candidates[i - 1].selection.criterion != "NONE",
              "scored candidates precede diagnostic-only");
  // 5) robust run: uncalibrated diagnostic fallback, never fake deltas.
  Config robust = c;
  robust.robust = true;
  auto bad = sample(rc);
  bad[8].z *= 100;
  auto rb = try1(bad, robust);
  require(rb.selectionCriterion == "RSS_DIAGNOSTIC_FALLBACK" &&
              !rb.selectionQualified,
          "robust run uses diagnostic fallback");
  for (auto &cand : rb.candidates)
    require(!cand.selection.delta, "no calibrated delta in fallback");
  // 6) no eligible candidate at all: exploratory fallback.
  auto none = try25(sample(rc, 2), {'R', 'L'}, c);
  require(none.selectionCriterion == "RSS_DIAGNOSTIC_FALLBACK" &&
              !none.selectionQualified,
          "no-primary fallback");
  for (auto &cand : none.candidates)
    require(!cand.selection.delta && !cand.selection.eligible,
            "fallback candidates are exploratory");
}
} // namespace
int main() {
  try {
    io();
    physics();
    reductionProperties();
    enumeration();
    fitting();
    forwardPolicy();
    knownTopology();
    selectorScenarios();
    selectionTiers();
    std::cout << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
    return 1;
  }
}
