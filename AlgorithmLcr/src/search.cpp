#include "lcr/lcr.hpp"
#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <stdexcept>
#include <sstream>
namespace lcr {
namespace {
using Clock = std::chrono::steady_clock;
struct Tree {
  char type;
  std::vector<std::shared_ptr<Tree>> children;
  std::string key;
  int n = 1, depth = 0;
};
using T = std::shared_ptr<Tree>;
Graph expand(const T &t) {
  Graph g;
  std::function<void(T, int, int)> emit = [&](T x, int u, int v) {
    if (x->children.empty()) {
      g.edges.push_back({u, v, {x->type, 1, 0}});
      return;
    }
    if (x->type == 'P')
      for (auto c : x->children)
        emit(c, u, v);
    else {
      int a = u;
      std::vector<int> mid;
      for (size_t k = 1; k < x->children.size(); ++k)
        mid.push_back(g.vertices++);
      mid.push_back(v);
      for (size_t k = 0; k < x->children.size(); ++k) {
        emit(x->children[k], a, mid[k]);
        a = mid[k];
      }
    }
  };
  emit(t, 0, 1);
  return g;
}
std::string labeledSignature(const Graph &g) {
  std::string key;
  for (auto b : g.edges)
    key += std::to_string(b.u) + "," + std::to_string(b.v) + ":" +
           b.element.type + ";";
  return key;
}
// Preserve-label diagnostic identity for Try3 known topologies: stable for a
// given edge multiset, invariant under edge row reordering, and free of the
// factorial canonical-labeling internal-node limit. Never a search key.
std::string labeledTopologyKey(const Graph &g, bool values) {
  validate(g);
  std::vector<std::string> keys;
  for (auto b : g.edges) {
    int u = std::min(b.u, b.v), v = std::max(b.u, b.v);
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
  return key;
}
struct Run {
  SearchResult r;
  const Config &c;
  Clock::time_point start = Clock::now();
  Run(int which, const Config &cfg, std::string family) : c(cfg) {
    r.which = which;
    r.family = std::move(family);
    r.mode = c.mode == Config::Strict ? "STRICT" : "FAST";
  }
  bool stop() {
    size_t budget = c.candidateBudget ? c.candidateBudget
                                      : (c.mode == Config::Fast ? 1000 : 0);
    if ((c.cancelled && c.cancelled()) || (budget && r.evaluated >= budget) ||
        (c.seconds > 0 && seconds() >= c.seconds)) {
      r.enumerationComplete = false;
      r.termination = "budget_exhausted";
      return true;
    }
    return false;
  }
  double seconds() const {
    return std::chrono::duration<double>(Clock::now() - start).count();
  }
  Config fitConfig() {
    Config local = c;
    local.cancelled = [this] { return stop(); };
    return local;
  }
  void add(Candidate x) {
    if (x.diagnostics.optimizer == "budget_exhausted") {
      r.enumerationComplete = false;
      r.termination = "budget_exhausted";
    }
    ++r.evaluated;
    if (!std::isfinite(x.metrics.rss)) {
      ++r.numericalFailures;
      return;
    }
    r.candidates.push_back(std::move(x));
  }
};
bool equivalent(const Candidate &a, const Candidate &b, const Data &d,
                double tol) {
  if (a.predicted.size() != d.size() || b.predicted.size() != d.size())
    return false;
  for (size_t i = 0; i < d.size(); ++i)
    if (std::abs(a.predicted[i] - b.predicted[i]) >
        tol * std::max(
                  {std::abs(a.predicted[i]), std::abs(b.predicted[i]), 1e-12}))
      return false;
  return true;
}
// Primary-AICc eligibility: only regular candidates participate in the
// calibrated Delta-AICc comparison; everything else is diagnostic-only.
SelectionInfo eligibility(const Candidate &a) {
  SelectionInfo s;
  s.criterion = "NONE";
  if (!std::isfinite(a.metrics.rss) || !std::isfinite(a.metrics.wrmse) ||
      !std::isfinite(a.metrics.maxRel)) {
    s.reasons.push_back("nonfinite_metrics");
    return s;
  }
  if (!a.metrics.aicc) {
    s.reasons.push_back("aicc_unavailable");
    return s;
  }
  if (a.diagnostics.optimizer.rfind("converged", 0) != 0) {
    s.reasons.push_back("optimizer_not_converged");
    return s;
  }
  if (a.diagnostics.robustUsed) {
    s.reasons.push_back("robust_run");
    return s;
  }
  if (a.diagnostics.rank != a.nParams) {
    s.reasons.push_back("rank_deficient");
    return s;
  }
  if (!a.diagnostics.atBound.empty()) {
    s.reasons.push_back("parameter_at_bound");
    return s;
  }
  s.eligible = true;
  s.criterion = "AICc";
  s.score = *a.metrics.aicc;
  return s;
}
void finish(Run &run, const Data &d, bool selection) {
  auto &v = run.r.candidates;
  auto byRss = [](auto &a, auto &b) {
    if (a.metrics.rss != b.metrics.rss)
      return a.metrics.rss < b.metrics.rss;
    if (a.nParams != b.nParams)
      return a.nParams < b.nParams;
    return a.topology < b.topology;
  };
  if (selection) {
    for (auto &a : v)
      a.selection = eligibility(a);
    bool primary =
        std::any_of(v.begin(), v.end(),
                    [](auto &a) { return a.selection.eligible; });
    // Robust runs never expose calibrated AICc deltas: no comparable robust
    // likelihood exists, so they stay on the diagnostic fallback.
    if (primary && !run.c.robust) {
      double best = inf;
      for (auto &a : v)
        if (a.selection.eligible)
          best = std::min(best, *a.metrics.aicc);
      for (auto &a : v)
        if (a.selection.eligible)
          a.selection.delta = *a.metrics.aicc - best;
      // Primary candidates first by AICc, diagnostic-only candidates after
      // them by raw RSS; a single AICc-invalid over-parameter model can no
      // longer demote the whole comparison set.
      std::stable_sort(v.begin(), v.end(), [&](auto &a, auto &b) {
        if (a.selection.eligible != b.selection.eligible)
          return a.selection.eligible;
        if (a.selection.eligible)
          return std::tie(*a.metrics.aicc, a.nParams, a.topology) <
                 std::tie(*b.metrics.aicc, b.nParams, b.topology);
        return byRss(a, b);
      });
      run.r.selectionCriterion = "AICc";
      run.r.selectionQualified = true;
    } else {
      std::stable_sort(v.begin(), v.end(), byRss);
      run.r.selectionCriterion = "RSS_DIAGNOSTIC_FALLBACK";
      run.r.selectionQualified = false;
    }
  } else {
    std::stable_sort(v.begin(), v.end(), byRss);
    // Try2 ranks by its own common objective; Try3 is a single prepared fit.
    run.r.selectionCriterion =
        run.r.which == 2
            ? (run.r.family == "ACTIVE_MULTIGRAPH_EXACT" ? "RSS_EXACT"
                                                         : "RSS_COMMON")
            : "NONE";
    run.r.selectionQualified = run.r.which == 2;
  }
  std::vector<Candidate> classes;
  for (auto &cand : v) {
    auto it = std::find_if(classes.begin(), classes.end(), [&](auto &rep) {
      return equivalent(cand, rep, d, run.c.equivalenceTolerance);
    });
    if (it != classes.end()) {
      ++it->members;
      continue;
    }
    classes.push_back(std::move(cand));
  }
  if (classes.size() > size_t(run.c.topK))
    classes.resize(run.c.topK);
  run.r.candidates = std::move(classes);
  run.r.elapsed = run.seconds();
  if (run.r.numericalFailures) {
    run.r.continuousGlobalCertified = false;
    if (run.r.termination == "complete")
      run.r.termination = "complete_with_numerical_failures";
  }
}
} // namespace
bool enumerate(const std::vector<Edge> &components, const GraphVisitor &visit,
               const std::function<bool()> &stop, size_t *structureCount) {
  if (components.empty() || components.size() > 8)
    throw std::invalid_argument("Try2 supports 1..8 components");
  Graph check;
  for (auto e : components)
    check.edges.push_back({0, 1, e});
  validate(check);
  int E = int(components.size());
  std::set<std::string> structures;
  std::vector<Edge> sorted = components;
  std::sort(sorted.begin(), sorted.end(), [](auto a, auto b) {
    return std::tie(a.type, a.parameter, a.parameterOfCapacitanceDCResistance) <
           std::tie(b.type, b.parameter, b.parameterOfCapacitanceDCResistance);
  });
  for (int V = 2; V <= E + 1; ++V) {
    std::vector<std::pair<int, int>> slots;
    for (int u = 0; u < V; ++u)
      for (int v = u + 1; v < V; ++v)
        slots.push_back({u, v});
    Graph g;
    g.vertices = V;
    bool complete = true;
    std::function<void(int, int)> dfs = [&](int left, int slot) {
      if (!complete)
        return;
      if (stop && stop()) {
        complete = false;
        return;
      }
      if (!left) {
        std::vector<int> degree(V);
        for (auto b : g.edges) {
          ++degree[b.u];
          ++degree[b.v];
        }
        if (std::find(degree.begin(), degree.end(), 0) != degree.end())
          return;
        try {
          if (liveEdges(g).size() != size_t(E))
            return;
        } catch (const std::runtime_error &) {
          return;
        }
        auto sig = canonical(g, false);
        if (!structures.insert(sig).second)
          return;
        if (structureCount)
          ++*structureCount;
        std::set<std::string> colored;
        std::vector<int> used(E);
        Graph coloredGraph = g;
        std::function<void(int)> assignments = [&](int k) {
          if (!complete)
            return;
          if (stop && stop()) {
            complete = false;
            return;
          }
          if (k == E) {
            auto key = canonical(coloredGraph, true);
            if (colored.insert(key).second && !visit(coloredGraph))
              complete = false;
            return;
          }
          for (int j = 0; j < E; ++j)
            if (!used[j]) {
              if (j > 0 && !used[j - 1] &&
                  sorted[j].type == sorted[j - 1].type &&
                  sorted[j].parameter == sorted[j - 1].parameter &&
                  sorted[j].parameterOfCapacitanceDCResistance ==
                      sorted[j - 1].parameterOfCapacitanceDCResistance)
                continue;
              used[j] = 1;
              coloredGraph.edges[k].element = sorted[j];
              assignments(k + 1);
              used[j] = 0;
            }
        };
        assignments(0);
        return;
      }
      for (int s = slot; s < int(slots.size()) && complete; ++s) {
        g.edges.push_back({slots[s].first, slots[s].second, {'R', 1, 0}});
        dfs(left - 1, s);
        g.edges.pop_back();
      }
    };
    dfs(E, 0);
    if (!complete)
      return false;
  }
  return true;
}
std::vector<Graph> spLibrary(int devices, int depth,
                             const std::function<bool()> &stop) {
  if (devices < 1 || devices > 12 || depth < 1)
    throw std::invalid_argument("invalid SP bounds");
  std::vector<std::vector<T>> layers(devices + 1);
  for (char k : std::string("CLR")) {
    auto t = std::make_shared<Tree>();
    t->type = k;
    t->key = std::string(1, k);
    layers[1].push_back(t);
  }
  for (int n = 2; n <= devices; ++n)
    for (char op : std::string("PS")) {
      if (stop && stop())
        return {};
      std::vector<T> pool;
      for (int j = 1; j < n; ++j)
        for (auto t : layers[j])
          if (t->type != op && t->depth < depth)
            pool.push_back(t);
      std::sort(pool.begin(), pool.end(),
                [](auto a, auto b) { return a->key < b->key; });
      std::vector<T> children;
      std::function<void(int, size_t)> dfs = [&](int remaining, size_t begin) {
        if (stop && stop())
          return;
        if (!remaining) {
          if (children.size() < 2)
            return;
          auto t = std::make_shared<Tree>();
          t->type = op;
          t->n = n;
          t->children = children;
          t->key = std::string(1, op) + "(";
          for (auto c : children) {
            t->depth = std::max(t->depth, c->depth + 1);
            t->key += c->key + ",";
          }
          t->key.back() = ')';
          layers[n].push_back(t);
          return;
        }
        for (size_t i = begin; i < pool.size(); ++i) {
          if (stop && stop())
            return;
          auto x = pool[i];
          if (x->n > remaining)
            continue;
          bool allowed = true;
          if (x->children.empty())
            for (auto c : children)
              if (c->children.empty()) {
                if (c->type == x->type && (op == 'S' || x->type != 'L'))
                  allowed = false;
                if (op == 'S' && ((x->type == 'R' && c->type == 'L') ||
                                  (x->type == 'L' && c->type == 'R')))
                  allowed = false;
              }
          if (!allowed)
            continue;
          children.push_back(x);
          dfs(remaining - x->n, i);
          children.pop_back();
        }
      };
      dfs(n, 0);
    }
  std::vector<Graph> out;
  for (auto t : layers[devices])
    out.push_back(expand(t));
  return out;
}
SearchResult try2(const Data &d, const std::vector<Edge> &components,
                  const Config &c) {
  validate(d, c);
  if (c.robust && c.tolerance == 0)
    throw std::invalid_argument(
        "robust refitting requires Try2 Tolerance, not Exact");
  Run run(2, c,
          c.tolerance > 0 ? "ACTIVE_MULTIGRAPH_TOLERANCE"
                          : "ACTIVE_MULTIGRAPH_EXACT");
  bool done = enumerate(
      components,
      [&](const Graph &g) {
        ++run.r.generated;
        if (run.stop())
          return false;
        Candidate a;
        if (c.tolerance > 0) {
          // Physical BOM identity: every known component stays its own
          // parameter with its nominal tolerance box (plan 5.4).
          auto p = prepareForFit(g, c, ReductionPolicy::None);
          a = fit(p, d, run.fitConfig(), {g});
          a.refined = true;
        } else {
          a.graph = g;
          a.diagnostics.optimizer = "exact_evaluation";
          a.diagnostics.verdict = "AMBIGUOUS_EQUIVALENCE_CLASS";
          bool ok = true;
          for (auto p : d) {
            auto f = forward(g, p.f, false);
            a.diagnostics.worstBackwardError =
                std::max(a.diagnostics.worstBackwardError, f.backwardError);
            a.diagnostics.worstRcond =
                std::min(a.diagnostics.worstRcond, f.rcond);
            if (f.status != SolveStatus::OK) {
              ok = false;
              break;
            }
            a.predicted.push_back(f.z);
          }
          if (ok)
            a.metrics = metrics(d, a.predicted, 0, c);
        }
        a.topology = canonical(g, true);
        run.add(std::move(a));
        return true;
      },
      [&]() { return run.stop(); }, &run.r.structures);
  run.r.enumerationComplete = done && run.r.enumerationComplete;
  run.r.continuousGlobalCertified = run.r.enumerationComplete &&
                                    c.tolerance == 0 &&
                                    run.r.numericalFailures == 0;
  finish(run, d, false);
  return run.r;
}
SearchResult try3(const Data &d, const Graph &g, const Config &c) {
  validate(d, c);
  if (c.tolerance > 0)
    throw std::invalid_argument("Try3 has no nominal tolerance input");
  Run run(3, c, "KNOWN_REDUCED_GRAPH");
  // R0 + exact electrical reduction, then the shared local fit over the
  // propagated aggregate domains (plan 5.1).
  auto p = prepareForFit(g, c, ReductionPolicy::ExactElectrical);
  auto a = fit(p, d, run.fitConfig());
  a.topology = "known_topology";
  a.originalTopologyKey = labeledTopologyKey(g, false);
  a.effectiveTopologyKey = labeledTopologyKey(a.graph, false);
  a.effectiveDevices = int(a.graph.edges.size());
  run.r.generated = run.r.structures = 1;
  run.add(std::move(a));
  finish(run, d, false);
  return run.r;
}
SearchResult try25(const Data &d, const std::vector<char> &kinds,
                   const Config &c) {
  validate(d, c);
  if (c.tolerance > 0)
    throw std::invalid_argument("Try2.5 uses wide bounds");
  std::vector<Edge> es;
  for (char t : kinds)
    es.push_back({t, 1, 0});
  Run run(25, c, "ACTIVE_TYPED_MULTIGRAPH_LOCAL_FIT");
  run.r.enumerationComplete = enumerate(
      es,
      [&](const Graph &g) {
        ++run.r.generated;
        if (run.stop())
          return false;
        // Same Try3 prepared inner fit: exact reduction collapses
        // non-identifiable aggregates before the wide-box local fit.
        auto p = prepareForFit(g, c, ReductionPolicy::ExactElectrical);
        auto a = fit(p, d, run.fitConfig());
        a.topology = canonical(g, false);
        a.originalTopologyKey = a.topology;
        a.effectiveTopologyKey = canonical(a.graph, false);
        a.effectiveDevices = int(a.graph.edges.size());
        run.add(std::move(a));
        return true;
      },
      [&]() { return run.stop(); }, &run.r.structures);
  finish(run, d, true);
  return run.r;
}
SearchResult try1(const Data &d, const Config &c) {
  validate(d, c);
  if (c.tolerance > 0)
    throw std::invalid_argument("Try1 has no nominal values");
  Run run(1, c, "NORMALIZED_SP_PLUS_FOSTER");
  int max = c.exactN ? *c.exactN : c.maxN;
  auto aux = rationalStarts(d, max);
  // Auxiliary realizations are normalized before count filtering and refitting.
  for (auto &g : aux) {
    if (run.stop())
      break;
    auto p = prepareForFit(g, c, ReductionPolicy::ExactElectrical);
    if (p.effective.edges.size() > size_t(max) ||
        (c.exactN && p.effective.edges.size() != size_t(*c.exactN)))
      continue;
    auto a = fit(p, d, run.fitConfig(), {p.effective});
    a.engine = "B";
    a.topology = "foster:" + labeledSignature(p.effective);
    a.originalTopologyKey = labeledSignature(g);
    a.effectiveTopologyKey = labeledSignature(p.effective);
    a.effectiveDevices = int(p.effective.edges.size());
    ++run.r.generated;
    run.add(std::move(a));
  }
  for (int n = c.exactN ? *c.exactN : 1; n <= max; ++n) {
    if (run.stop())
      break;
    auto graphs = spLibrary(n, c.maxDepth, [&] { return run.stop(); });
    run.r.structures += graphs.size();
    for (auto &g : graphs) {
      if (run.stop())
        break;
      ++run.r.generated;
      auto p = prepareForFit(g, c, ReductionPolicy::None);
      auto a = fit(p, d, run.fitConfig());
      a.topology = labeledSignature(g);
      a.originalTopologyKey = a.topology;
      a.effectiveTopologyKey = a.topology;
      a.effectiveDevices = int(g.edges.size());
      run.add(std::move(a));
    }
  }
  finish(run, d, true);
  return run.r;
}
} // namespace lcr
