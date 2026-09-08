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
Reduction reduce(const Graph &g) {
  auto live = liveEdges(g);
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
      r.groups.push_back({{int(i)}, "single"});
    }
  }
  auto merge = [&](size_t i, size_t j, Branch b, const std::string &mode) {
    r.graph.edges[i] = b;
    auto &a = r.groups[i];
    a.members.insert(a.members.end(), r.groups[j].members.begin(),
                     r.groups[j].members.end());
    std::sort(a.members.begin(), a.members.end());
    a.mode = mode;
    r.graph.edges.erase(r.graph.edges.begin() + j);
    r.groups.erase(r.groups.begin() + j);
  };
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < r.graph.edges.size() && !changed; ++i)
      for (size_t j = i + 1; j < r.graph.edges.size(); ++j) {
        auto a = r.graph.edges[i], b = r.graph.edges[j];
        char t = a.element.type;
        if (a.u == b.u && a.v == b.v && t == b.element.type && t != 'L') {
          a.element.parameter =
              t == 'R' ? 1 / (1 / a.element.parameter + 1 / b.element.parameter)
                       : a.element.parameter + b.element.parameter;
          merge(i, j, a, "par");
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
      Edge e = a.element;
      if (ta == tb) {
        e.parameter =
            ta == 'C' ? 1 / (1 / a.element.parameter + 1 / b.element.parameter)
                      : a.element.parameter + b.element.parameter;
        if (ta == 'L')
          e.parameterOfCapacitanceDCResistance +=
              b.element.parameterOfCapacitanceDCResistance;
      } else {
        e = ta == 'L' ? a.element : b.element;
        e.parameterOfCapacitanceDCResistance +=
            (ta == 'R' ? a.element.parameter : b.element.parameter);
      }
      merge(i, j, {std::min(x, y), std::max(x, y), e}, "ser");
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
