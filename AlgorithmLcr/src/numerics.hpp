#pragma once
// Single definition point for internal numerical policy thresholds shared by
// the solver and fit layers. Tests locate and stress these values here; they
// are deliberately not part of the public Config.
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
} // namespace lcr::numerics
