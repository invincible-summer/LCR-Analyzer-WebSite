#pragma once
// Single definition point for internal numerical policy thresholds shared by
// the solver and fit layers. Tests locate and stress these values here; they
// are deliberately not part of the public Config.
#include "lcr/lcr.hpp"
namespace lcr::numerics {
struct Policy {
  double luRankThreshold = 1e-15; // FullPivLU invertibility cut
  double rcondWarn = 1e-12;       // ILL_CONDITIONED / numerical warning
  double rcondReject = 1e-15;     // evaluation rejected below this rcond
  double backwardReject = 1e-10;  // evaluation rejected above this residual
  double identConditionWarn = 1e4; // condition number warning threshold
  double weakElasticity = 0.1;    // weak-parameter elasticity cut
};
inline constexpr Policy policy{};

// Sole forward-solve disposition shared by the common fit evaluator and the
// Try2 Exact engine: OK accepts; ILL_CONDITIONED that still satisfies the
// reject thresholds warns; every other state (and any threshold violation)
// rejects. One policy, no per-engine reimplementation.
enum class ForwardDisposition { Accept, Warn, Reject };
inline ForwardDisposition classify(const Forward &f) {
  if (f.status == SolveStatus::OK)
    return ForwardDisposition::Accept;
  if (f.status == SolveStatus::ILL_CONDITIONED &&
      f.backwardError <= policy.backwardReject &&
      f.rcond >= policy.rcondReject)
    return ForwardDisposition::Warn;
  return ForwardDisposition::Reject;
}
} // namespace lcr::numerics
