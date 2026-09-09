#pragma once
// Three-tier selection semantics for non-robust selector runs (Try1/Try2.5),
// split out of finish() so the tier logic is directly unit-testable. This
// header is internal: it is not installed and never part of the public API.
#include "lcr/lcr.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace lcr::selection {

enum class Tier { Qualified, Provisional, Diagnostic };

// Diagnostic disqualifiers always win over provisional promotion. A merely
// unconverged regular candidate keeps its current AICc as a score: for a
// fixed topology (fixed k) AICc is strictly increasing in RSS, and further
// optimization can only lower RSS, so the current value is a conservative
// upper bound on that topology's reachable AICc — never a calibrated
// qualification.
inline Tier tierOf(const Candidate &a) {
  if (!std::isfinite(a.metrics.rss) || !std::isfinite(a.metrics.wrmse) ||
      !std::isfinite(a.metrics.maxRel))
    return Tier::Diagnostic;
  if (!a.metrics.aicc)
    return Tier::Diagnostic;
  if (a.diagnostics.robustUsed)
    return Tier::Diagnostic;
  if (a.diagnostics.rank != a.nParams)
    return Tier::Diagnostic;
  if (!a.diagnostics.atBound.empty())
    return Tier::Diagnostic;
  return a.diagnostics.optimizer.rfind("converged", 0) == 0 ? Tier::Qualified
                                                            : Tier::Provisional;
}

inline SelectionInfo infoOf(const Candidate &a, Tier t) {
  SelectionInfo s;
  s.criterion = "NONE";
  switch (t) {
  case Tier::Qualified:
    s.eligible = true;
    s.criterion = "AICc";
    s.score = *a.metrics.aicc;
    break;
  case Tier::Provisional:
    // Scored ordering only: never a calibrated Delta-AICc qualification.
    s.criterion = "AICc_PROVISIONAL";
    s.score = *a.metrics.aicc;
    s.reasons.push_back("optimizer_not_converged");
    break;
  case Tier::Diagnostic:
    if (!std::isfinite(a.metrics.rss) || !std::isfinite(a.metrics.wrmse) ||
        !std::isfinite(a.metrics.maxRel))
      s.reasons.push_back("nonfinite_metrics");
    else if (!a.metrics.aicc)
      s.reasons.push_back("aicc_unavailable");
    else if (a.diagnostics.robustUsed)
      s.reasons.push_back("robust_run");
    else if (a.diagnostics.rank != a.nParams)
      s.reasons.push_back("rank_deficient");
    else if (!a.diagnostics.atBound.empty())
      s.reasons.push_back("parameter_at_bound");
    else
      s.reasons.push_back("optimizer_not_converged");
    break;
  }
  return s;
}

// Strict order inside the AICc-scored set (Qualified union Provisional).
inline bool scoredAhead(const Candidate &a, const Candidate &b) {
  if (*a.metrics.aicc != *b.metrics.aicc)
    return *a.metrics.aicc < *b.metrics.aicc;
  if (a.nParams != b.nParams)
    return a.nParams < b.nParams;
  return a.topology < b.topology;
}

inline bool diagnosticAhead(const Candidate &a, const Candidate &b) {
  if (a.metrics.rss != b.metrics.rss)
    return a.metrics.rss < b.metrics.rss;
  if (a.nParams != b.nParams)
    return a.nParams < b.nParams;
  return a.topology < b.topology;
}

inline bool scored(const Candidate &a) { return tierOf(a) != Tier::Diagnostic; }

// Full selection order: the scored set by current AICc first, the
// diagnostic-only candidates after them by raw RSS. Robust runs are
// diagnostic throughout (the run-level flag, not per-candidate robustUsed —
// a robust-run candidate that never triggered reweighting has robustUsed ==
// false and must not sneak into the scored set).
inline bool aheadOf(const Candidate &a, const Candidate &b, bool robust) {
  bool sa = !robust && scored(a), sb = !robust && scored(b);
  if (sa != sb)
    return sa;
  return sa ? scoredAhead(a, b) : diagnosticAhead(a, b);
}

inline void annotate(std::vector<Candidate> &v, bool robust) {
  for (auto &a : v)
    a.selection = infoOf(a, robust ? Tier::Diagnostic : tierOf(a));
}

// Deltas are calibrated only within the qualified set.
inline double qualifiedFloor(const std::vector<Candidate> &v) {
  double best = inf;
  for (auto &a : v)
    if (tierOf(a) == Tier::Qualified)
      best = std::min(best, *a.metrics.aicc);
  return best;
}

inline void assignDeltas(std::vector<Candidate> &v, double qualifiedFloor) {
  for (auto &a : v)
    if (a.selection.eligible)
      a.selection.delta = *a.metrics.aicc - qualifiedFloor;
}

// Representative preference inside an observed-grid equivalence class: keep
// the most reliable diagnostic state on display. Robust runs compare by raw
// RSS only.
inline bool betterRepresentative(const Candidate &a, const Candidate &b,
                                 bool robust) {
  if (robust)
    return a.metrics.rss < b.metrics.rss;
  Tier ta = tierOf(a), tb = tierOf(b);
  auto rank = [](Tier t) {
    return t == Tier::Qualified ? 0 : t == Tier::Provisional ? 1 : 2;
  };
  if (ta != tb)
    return rank(ta) < rank(tb);
  return ta == Tier::Diagnostic ? a.metrics.rss < b.metrics.rss
                                : *a.metrics.aicc < *b.metrics.aicc;
}

struct Outcome {
  std::string criterion;
  bool qualified;
};

// Run-level semantics read off the final rank-1 candidate. Robust runs are
// always the diagnostic fallback: candidate-specific Huber IRLS costs do not
// form a comparable robust likelihood (a run-level robust candidate that
// never triggered reweighting has robustUsed == false).
inline Outcome outcomeOf(const std::vector<Candidate> &v, bool robust) {
  if (robust || v.empty() || tierOf(v[0]) == Tier::Diagnostic)
    return {"RSS_DIAGNOSTIC_FALLBACK", false};
  if (tierOf(v[0]) == Tier::Qualified)
    return {"AICc", true};
  return {"AICc_PROVISIONAL_ORDER", false};
}

} // namespace lcr::selection
