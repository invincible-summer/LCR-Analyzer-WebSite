#include "lcr/lcr.hpp"
#include <algorithm>
#include <iomanip>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
namespace lcr {
std::vector<int> liveEdges(const Graph &g) {
  validate(g);
  std::vector<std::vector<int>> adj(g.vertices);
  for (auto b : g.edges) {
    adj[b.u].push_back(b.v);
    adj[b.v].push_back(b.u);
  }
  auto component = [&](int start, int cut) {
    std::vector<bool> seen(g.vertices);
    std::queue<int> q;
    q.push(start);
    seen[start] = true;
    while (!q.empty()) {
      int u = q.front();
      q.pop();
      for (int v : adj[u])
        if (v != cut && !seen[v]) {
          seen[v] = true;
          q.push(v);
        }
    }
    return seen;
  };
  auto connected = component(0, -1);
  if (!connected[1])
    throw std::runtime_error("PORT_OPEN");
  std::vector<bool> dead(g.vertices);
  for (int i = 0; i < g.vertices; ++i)
    dead[i] = !connected[i];
  // A component attached through a single cut vertex cannot carry port current.
  for (int cut = 0; cut < g.vertices; ++cut)
    if (connected[cut]) {
      std::vector<bool> visited(g.vertices);
      visited[cut] = true;
      for (int i = 0; i < g.vertices; ++i)
        if (connected[i] && !visited[i]) {
          auto c = component(i, cut);
          bool port = c[0] || c[1];
          for (int j = 0; j < g.vertices; ++j)
            if (c[j]) {
              visited[j] = true;
              if (!port)
                dead[j] = true;
            }
        }
    }
  std::vector<int> out;
  for (size_t i = 0; i < g.edges.size(); ++i)
    if (!dead[g.edges[i].u] && !dead[g.edges[i].v])
      out.push_back(int(i));
  return out;
}
// Per-device admissible interval: wide physical box, optionally intersected
// with the symmetric nominal tolerance box.
EdgeDomain edgeDomain(const Edge &e, const Config &c) {
  EdgeDomain d;
  d.value.lo = e.type == 'R' ? c.rMin : e.type == 'L' ? c.lMin : c.cMin;
  d.value.hi = e.type == 'R' ? c.rMax : e.type == 'L' ? c.lMax : c.cMax;
  if (c.tolerance > 0) {
    d.value.lo = std::max(d.value.lo, e.parameter * (1 - c.tolerance));
    d.value.hi = std::min(d.value.hi, e.parameter * (1 + c.tolerance));
  }
  if (d.value.hi < d.value.lo)
    throw std::invalid_argument("nominal tolerance outside physical bounds");
  if (e.type == 'L') {
    Interval dcr;
    dcr.lo = 0;
    dcr.hi = c.dcrMax;
    if (c.tolerance > 0) {
      dcr.lo = std::max(
          0., e.parameterOfCapacitanceDCResistance * (1 - c.tolerance) -
                  c.dcrAbsoluteTolerance);
      dcr.hi = std::min(dcr.hi, e.parameterOfCapacitanceDCResistance *
                                        (1 + c.tolerance) +
                                    c.dcrAbsoluteTolerance);
    }
    if (dcr.hi < dcr.lo)
      throw std::invalid_argument("DCR tolerance outside bounds");
    d.dcr = dcr;
  }
  return d;
}
// Monotone interval propagation: Sum adds endpoints, HarmonicSum composes
// reciprocals (strictly positive leaves only). Every legal physical member
// combination therefore maps inside the propagated aggregate interval.
Interval bounds(const ReductionExpr &e,
                const std::vector<EdgeDomain> &source) {
  if (e.op == ExprOp::PrimitiveValue)
    return source.at(e.sourceEdge).value;
  if (e.op == ExprOp::PrimitiveDcr) {
    if (!source.at(e.sourceEdge).dcr)
      throw std::invalid_argument("DCR expression on non-inductor edge");
    return *source.at(e.sourceEdge).dcr;
  }
  if (e.children.size() < 2)
    throw std::invalid_argument("composite expression needs two children");
  Interval out;
  if (e.op == ExprOp::Sum) {
    for (auto &c : e.children) {
      auto b = bounds(c, source);
      out.lo += b.lo;
      out.hi += b.hi;
    }
    return out;
  }
  double invLo = 0, invHi = 0;
  for (auto &c : e.children) {
    auto b = bounds(c, source);
    if (!(b.lo > 0))
      throw std::invalid_argument("harmonic sum requires positive bounds");
    invLo += 1. / b.lo;
    invHi += 1. / b.hi;
  }
  out.lo = 1. / invLo;
  out.hi = 1. / invHi;
  return out;
}
double evaluate(const ReductionExpr &e, const Graph &source) {
  switch (e.op) {
  case ExprOp::PrimitiveValue:
    return source.edges.at(e.sourceEdge).element.parameter;
  case ExprOp::PrimitiveDcr:
    return source.edges.at(e.sourceEdge)
        .element.parameterOfCapacitanceDCResistance;
  case ExprOp::Sum: {
    double s = 0;
    for (auto &c : e.children)
      s += evaluate(c, source);
    return s;
  }
  default: {
    double s = 0;
    for (auto &c : e.children)
      s += 1. / evaluate(c, source);
    return 1. / s;
  }
  }
}
Reduction reduce(const Graph &g, const Config &c) {
  auto live = liveEdges(g);
  std::vector<EdgeDomain> source(g.edges.size());
  for (size_t i = 0; i < g.edges.size(); ++i)
    source[i] = edgeDomain(g.edges[i].element, c);
  Reduction r;
  r.graph.vertices = g.vertices;
  for (size_t i = 0; i < g.edges.size(); ++i) {
    if (std::find(live.begin(), live.end(), int(i)) == live.end())
      r.dropped.push_back(int(i));
    else {
      auto b = g.edges[i];
      if (b.u > b.v)
        std::swap(b.u, b.v);
      r.graph.edges.push_back(b);
      Group group;
      group.members = {int(i)};
      group.valueExpr = {ExprOp::PrimitiveValue, int(i), {}};
      if (b.element.type == 'L')
        group.dcrExpr = ReductionExpr{ExprOp::PrimitiveDcr, int(i), {}};
      group.mode = "single";
      r.groups.push_back(std::move(group));
      r.domains.push_back(source[i]);
    }
  }
  // Combine groups i and j (erasing j). The effective branch value and the
  // admissible domain are always rebuilt from the composed expression
  // evaluated/propagated against the original graph, never from single-device
  // bounds of the merged edge.
  auto merge = [&](size_t i, size_t j, Branch b, ReductionExpr valueExpr,
                   std::optional<ReductionExpr> dcrExpr,
                   const std::string &mode) {
    auto &a = r.groups[i];
    auto &o = r.groups[j];
    a.members.insert(a.members.end(), o.members.begin(), o.members.end());
    std::sort(a.members.begin(), a.members.end());
    a.valueExpr = std::move(valueExpr);
    a.dcrExpr = std::move(dcrExpr);
    a.mode = mode;
    b.element.parameter = evaluate(a.valueExpr, g);
    if (b.element.type == 'L' && a.dcrExpr)
      b.element.parameterOfCapacitanceDCResistance = evaluate(*a.dcrExpr, g);
    else
      b.element.parameterOfCapacitanceDCResistance = 0;
    r.graph.edges[i] = b;
    r.domains[i] = EdgeDomain{
        bounds(a.valueExpr, source),
        a.dcrExpr ? std::optional<Interval>(bounds(*a.dcrExpr, source))
                  : std::nullopt};
    r.graph.edges.erase(r.graph.edges.begin() + j);
    r.groups.erase(r.groups.begin() + j);
    r.domains.erase(r.domains.begin() + j);
  };
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < r.graph.edges.size() && !changed; ++i)
      for (size_t j = i + 1; j < r.graph.edges.size(); ++j) {
        auto a = r.graph.edges[i], b = r.graph.edges[j];
        char t = a.element.type;
        if (a.u == b.u && a.v == b.v && t == b.element.type && t != 'L') {
          ReductionExpr value;
          value.op = t == 'R' ? ExprOp::HarmonicSum : ExprOp::Sum;
          value.children = {r.groups[i].valueExpr, r.groups[j].valueExpr};
          merge(i, j, a, std::move(value), std::nullopt, "par");
          changed = true;
          break;
        }
      }
    if (changed)
      continue;
    for (int u = 2; u < g.vertices && !changed; ++u) {
      std::vector<size_t> incident;
      for (size_t i = 0; i < r.graph.edges.size(); ++i)
        if (r.graph.edges[i].u == u || r.graph.edges[i].v == u)
          incident.push_back(i);
      if (incident.size() != 2)
        continue;
      auto i = incident[0], j = incident[1];
      auto a = r.graph.edges[i], b = r.graph.edges[j];
      int x = a.u == u ? a.v : a.u, y = b.u == u ? b.v : b.u;
      if (x == y)
        continue;
      char ta = a.element.type, tb = b.element.type;
      if (ta != tb && !((ta == 'R' && tb == 'L') || (ta == 'L' && tb == 'R')))
        continue;
      Branch merged{std::min(x, y), std::max(x, y), {}};
      ReductionExpr value;
      std::optional<ReductionExpr> dcrExpr;
      if (ta == tb) {
        merged.element.type = ta;
        value.op = ta == 'C' ? ExprOp::HarmonicSum : ExprOp::Sum;
        value.children = {r.groups[i].valueExpr, r.groups[j].valueExpr};
        if (ta == 'L') {
          ReductionExpr dcr;
          dcr.op = ExprOp::Sum;
          dcr.children = {*r.groups[i].dcrExpr, *r.groups[j].dcrExpr};
          dcrExpr = std::move(dcr);
        }
      } else {
        size_t li = ta == 'L' ? i : j, ri = ta == 'L' ? j : i;
        merged.element.type = 'L';
        value = r.groups[li].valueExpr;
        ReductionExpr dcr;
        dcr.op = ExprOp::Sum;
        dcr.children = {*r.groups[li].dcrExpr, r.groups[ri].valueExpr};
        dcrExpr = std::move(dcr);
      }
      merge(i, j, merged, std::move(value), std::move(dcrExpr), "ser");
      changed = true;
    }
  }
  return r;
}
std::string canonical(const Graph &g, bool values) {
  validate(g);
  std::vector<int> nodes;
  for (auto b : g.edges)
    for (int u : {b.u, b.v})
      if (u >= 2 && std::find(nodes.begin(), nodes.end(), u) == nodes.end())
        nodes.push_back(u);
  std::sort(nodes.begin(), nodes.end());
  if (nodes.size() > 8)
    throw std::invalid_argument(
        "canonical labeling supports at most 8 internal nodes");
  std::vector<int> perm(nodes.size());
  std::iota(perm.begin(), perm.end(), 2);
  std::string best;
  do {
    for (int swap = 0; swap < 2; ++swap) {
      std::vector<int> map(g.vertices, -1);
      map[0] = swap;
      map[1] = 1 - swap;
      for (size_t i = 0; i < nodes.size(); ++i)
        map[nodes[i]] = perm[i];
      std::vector<std::string> keys;
      for (auto b : g.edges) {
        int u = map[b.u], v = map[b.v];
        if (u > v)
          std::swap(u, v);
        std::ostringstream s;
        s << u << ',' << v << ':' << b.element.type;
        if (values)
          s << ':' << std::hexfloat << b.element.parameter << ':'
            << b.element.parameterOfCapacitanceDCResistance;
        keys.push_back(s.str());
      }
      std::sort(keys.begin(), keys.end());
      std::string key;
      for (auto &s : keys)
        key += s + ';';
      if (best.empty() || key < best)
        best = key;
    }
  } while (std::next_permutation(perm.begin(), perm.end()));
  return best;
}
} // namespace lcr
