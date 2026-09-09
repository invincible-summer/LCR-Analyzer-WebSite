#include "lcr/lcr.hpp"
#include <iomanip>
#include <ostream>
namespace lcr {
namespace {
void number(std::ostream &o, double x) {
  if (std::isfinite(x))
    o << std::setprecision(17) << x;
  else
    o << "null";
}
void quote(std::ostream &o, const std::string &s) {
  o << '"';
  for (unsigned char c : s) {
    if (c == '"' || c == '\\')
      o << '\\' << c;
    else if (c == '\n')
      o << "\\n";
    else if (c < 32)
      o << '?';
    else
      o << c;
  }
  o << '"';
}
template <class T> void array(std::ostream &o, const std::vector<T> &v) {
  o << '[';
  for (size_t i = 0; i < v.size(); ++i) {
    if (i)
      o << ',';
    number(o, double(v[i]));
  }
  o << ']';
}
void expr(std::ostream &o, const ReductionExpr &e) {
  switch (e.op) {
  case ExprOp::PrimitiveValue:
    o << "{\"op\":\"value\",\"edge\":" << e.sourceEdge << '}';
    break;
  case ExprOp::PrimitiveDcr:
    o << "{\"op\":\"dcr\",\"edge\":" << e.sourceEdge << '}';
    break;
  default:
    o << "{\"op\":\"" << (e.op == ExprOp::Sum ? "sum" : "hsum")
      << "\",\"children\":[";
    for (size_t k = 0; k < e.children.size(); ++k) {
      if (k)
        o << ',';
      expr(o, e.children[k]);
    }
    o << "]}";
  }
}
} // namespace
void report(std::ostream &o, const SearchResult &r, const Data &d,
            const Config &c, bool json) {
  if (!json) {
    o << "LCR v4 Try" << r.which << " mode=" << r.mode << " family=" << r.family
      << " termination=" << r.termination << "\n";
    o << "enumeration_complete=" << r.enumerationComplete
      << " continuous_global_certified=" << r.continuousGlobalCertified
      << " evaluated=" << r.evaluated
      << " numerical_failures=" << r.numericalFailures << " seed=" << c.seed
      << " elapsed=" << r.elapsed << "s\n";
    o << "selection_criterion=" << r.selectionCriterion
      << " selection_qualified=" << r.selectionQualified << "\n";
    for (size_t i = 0; i < r.candidates.size(); ++i) {
      auto &a = r.candidates[i];
      o << "rank=" << i + 1 << " devices=" << a.graph.edges.size()
        << " wRMSE=" << a.metrics.wrmse << " maxRel=" << a.metrics.maxRel
        << " AICc=";
      if (a.metrics.aicc)
        o << *a.metrics.aicc;
      else
        o << "unavailable";
      o << " verdict=" << a.diagnostics.verdict
        << " optimizer=" << a.diagnostics.optimizer
        << " numerical=" << a.diagnostics.numericalStatus
        << " identifiability=" << a.diagnostics.identifiabilityStatus
        << " selection=" << (a.selection.eligible ? "primary" : "diagnostic")
        << "\n";
      printAdjacency(o, a.graph, int(i + 1));
      for (size_t j = 0; j < a.reduction.groups.size(); ++j) {
        auto g = a.reduction.groups[j];
        if (g.members.size() > 1) {
          o << "  # merged group " << j << " (" << g.mode << ") edges";
          for (int k : g.members)
            o << ' ' << k;
          o << '\n';
        }
      }
      for (int j : a.reduction.dropped)
        o << "  # dropped edge " << j << " (port-invisible)\n";
    }
    return;
  }
  o << "{\"schema\":\"lcr.native.v4\",\"schema_revision\":2,"
       "\"engine_version\":\"4.1.2\",\"try\":" << r.which << ",\"mode\":";
  quote(o, r.mode);
  o << ",\"hypothesis_family\":";
  quote(o, r.family);
  o << ",\"termination\":";
  quote(o, r.termination);
  o << ",\"enumeration_complete\":"
    << (r.enumerationComplete ? "true" : "false")
    << ",\"continuous_global_certified\":"
    << (r.continuousGlobalCertified ? "true" : "false") << ",\"elapsed\":";
  number(o, r.elapsed);
  o << ",\"seed\":" << c.seed << ",\"max_n\":" << c.maxN
    << ",\"max_depth\":" << c.maxDepth << ",\"exact_n\":";
  if (c.exactN)
    o << *c.exactN;
  else
    o << "null";
  o << ",\"noise_model\":";
  quote(o, c.covariance.empty() ? "relative_unknown_scale"
                                : "supplied_covariance");
  o << ",\"equivalence\":\"observed_grid\",\"equivalence_metric\":"
    << "\"relative_curve\",\"equivalence_threshold\":";
  number(o, c.equivalenceTolerance);
  o << ",\"selection\":{\"criterion\":";
  quote(o, r.selectionCriterion);
  o << ",\"qualified\":" << (r.selectionQualified ? "true" : "false")
    << "},\"stats\":{\"generated\":"
    << r.generated << ",\"structures\":" << r.structures
    << ",\"evaluated\":" << r.evaluated
    << ",\"numerical_failures\":" << r.numericalFailures
    << "},\"candidates\":[";
  for (size_t i = 0; i < r.candidates.size(); ++i) {
    if (i)
      o << ',';
    auto &a = r.candidates[i];
    auto &dg = a.diagnostics;
    o << "{\"rank\":" << i + 1 << ",\"devices\":" << a.graph.edges.size()
      << ",\"n_params\":" << a.nParams << ",\"n_members\":" << a.members
      << ",\"refined\":" << (a.refined ? "true" : "false") << ",\"engine\":";
    quote(o, a.engine);
    o << ",\"topology\":";
    quote(o, a.topology);
    o << ",\"original_topology_key\":";
    quote(o, a.originalTopologyKey);
    o << ",\"effective_topology_key\":";
    quote(o, a.effectiveTopologyKey);
    o << ",\"effective_devices\":" << a.effectiveDevices;
    o << ",\"wrmse\":";
    number(o, a.metrics.wrmse);
    o << ",\"max_rel\":";
    number(o, a.metrics.maxRel);
    o << ",\"rss\":";
    number(o, a.metrics.rss);
    o << ",\"aicc\":";
    if (a.metrics.aicc)
      number(o, *a.metrics.aicc);
    else
      o << "null";
    o << ",\"selection\":{\"eligible\":"
      << (a.selection.eligible ? "true" : "false") << ",\"criterion\":";
    quote(o, a.selection.criterion);
    o << ",\"score\":";
    if (a.selection.score)
      number(o, *a.selection.score);
    else
      o << "null";
    o << ",\"delta\":";
    if (a.selection.delta)
      number(o, *a.selection.delta);
    else
      o << "null";
    o << ",\"reasons\":[";
    for (size_t k = 0; k < a.selection.reasons.size(); ++k) {
      if (k)
        o << ',';
      quote(o, a.selection.reasons[k]);
    }
    o << "]}";
    o << ",\"adjacency\":{\"v\":" << a.graph.vertices << ",\"slots\":[";
    auto adj = adjacency(a.graph);
    bool first = true;
    for (int u = 0; u < a.graph.vertices; ++u)
      for (int v = u + 1; v < a.graph.vertices; ++v) {
        auto es = adj[u][v - u - 1];
        if (es.empty())
          continue;
        if (!first)
          o << ',';
        first = false;
        o << "{\"u\":" << u << ",\"j\":" << v << ",\"edges\":[";
        for (size_t j = 0; j < es.size(); ++j) {
          if (j)
            o << ',';
          o << "{\"t\":\"" << es[j].type << "\",\"p\":";
          number(o, es[j].parameter);
          o << ",\"d\":";
          number(o, es[j].parameterOfCapacitanceDCResistance);
          o << '}';
        }
        o << "]}";
      }
    o << "]},\"theory\":{\"f\":[";
    for (size_t j = 0; j < d.size(); ++j) {
      if (j)
        o << ',';
      number(o, d[j].f);
    }
    o << "],\"re\":[";
    for (size_t j = 0; j < a.predicted.size(); ++j) {
      if (j)
        o << ',';
      number(o, a.predicted[j].real());
    }
    o << "],\"im\":[";
    for (size_t j = 0; j < a.predicted.size(); ++j) {
      if (j)
        o << ',';
      number(o, a.predicted[j].imag());
    }
    o << "]},\"diagnostics\":{\"verdict\":";
    quote(o, dg.verdict);
    o << ",\"optimizer\":";
    quote(o, dg.optimizer);
    o << ",\"numerical_status\":";
    quote(o, dg.numericalStatus);
    o << ",\"identifiability_status\":";
    quote(o, dg.identifiabilityStatus);
    o << ",\"fit_objective\":";
    number(o, dg.fitObjective);
    o << ",\"rank\":" << dg.rank << ",\"condition\":";
    number(o, dg.condition);
    o << ",\"singular_values\":";
    array(o, dg.singularValues);
    o << ",\"standard_errors\":";
    array(o, dg.standardErrors);
    o << ",\"approximate_ci95\":[";
    for (size_t k = 0; k < dg.confidenceIntervals95.size(); ++k) {
      if (k)
        o << ',';
      o << '[';
      number(o, dg.confidenceIntervals95[k][0]);
      o << ',';
      number(o, dg.confidenceIntervals95[k][1]);
      o << ']';
    }
    o << ']';
    o << ",\"weak\":";
    array(o, dg.weak);
    o << ",\"at_bound\":";
    array(o, dg.atBound);
    o << ",\"parameters\":[";
    for (size_t k = 0; k < dg.parameters.size(); ++k) {
      if (k)
        o << ',';
      auto &pd = dg.parameters[k];
      o << "{\"id\":" << pd.id << ",\"edge\":" << pd.edge
        << ",\"quantity\":\""
        << (pd.quantity == ParamQuantity::Dcr ? "dcr" : "value")
        << "\",\"kind\":\"" << pd.kind << "\",\"value\":";
      number(o, pd.value);
      o << ",\"lower\":";
      number(o, pd.lower);
      o << ",\"upper\":";
      number(o, pd.upper);
      o << ",\"free\":" << (pd.free ? "true" : "false")
        << ",\"fixed\":" << (pd.fixed ? "true" : "false")
        << ",\"weak\":" << (pd.weak ? "true" : "false")
        << ",\"at_bound\":" << (pd.atBound ? "true" : "false")
        << ",\"standard_error\":";
      if (pd.standardError)
        number(o, *pd.standardError);
      else
        o << "null";
      o << ",\"ci95\":";
      if (pd.ci95) {
        o << '[';
        number(o, (*pd.ci95)[0]);
        o << ',';
        number(o, (*pd.ci95)[1]);
        o << ']';
      } else
        o << "null";
      o << '}';
    }
    o << ']';
    o << ",\"starts\":" << dg.starts << ",\"best_start\":" << dg.bestStart
      << ",\"converged_starts\":" << dg.convergedStarts
      << ",\"agreeing_starts\":" << dg.agreeingStarts
      << ",\"robust_used\":" << (dg.robustUsed ? "true" : "false")
      << ",\"outlier_count\":" << dg.outliers << ",\"worst_backward_error\":";
    number(o, dg.worstBackwardError);
    o << ",\"worst_rcond\":";
    number(o, dg.worstRcond);
    o << "},\"groups\":[";
    for (size_t j = 0; j < a.reduction.groups.size(); ++j) {
      if (j)
        o << ',';
      auto &edge = a.graph.edges[j];
      auto &grp = a.reduction.groups[j];
      auto &dom = a.reduction.domains.at(j);
      o << "{\"gid\":" << j << ",\"u\":" << edge.u << ",\"v\":" << edge.v
        << ",\"kind\":\"" << edge.element.type << "\",\"value\":";
      number(o, edge.element.parameter);
      o << ",\"dcr\":";
      number(o, edge.element.parameterOfCapacitanceDCResistance);
      o << ",\"members\":";
      array(o, grp.members);
      o << ",\"mode\":";
      quote(o, grp.mode);
      o << ",\"value_bounds\":[";
      number(o, dom.value.lo);
      o << ',';
      number(o, dom.value.hi);
      o << "],\"dcr_bounds\":";
      if (dom.dcr) {
        o << '[';
        number(o, dom.dcr->lo);
        o << ',';
        number(o, dom.dcr->hi);
        o << ']';
      } else
        o << "null";
      o << ",\"parameter_ids\":[";
      bool firstPid = true;
      for (auto &pd : dg.parameters)
        if (pd.edge == int(j)) {
          if (!firstPid)
            o << ',';
          firstPid = false;
          o << pd.id;
        }
      o << "],\"value_expr\":";
      expr(o, grp.valueExpr);
      o << ",\"dcr_expr\":";
      if (grp.dcrExpr)
        expr(o, *grp.dcrExpr);
      else
        o << "null";
      o << '}';
    }
    o << "],\"dropped\":";
    array(o, a.reduction.dropped);
    o << '}';
  }
  o << "]}\n";
}
} // namespace lcr
