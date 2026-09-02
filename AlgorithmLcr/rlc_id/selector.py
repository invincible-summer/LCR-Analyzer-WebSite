"""Selector: candidate merge, equivalence clustering and ranking (DESIGN.md §5.5, §7).

Equivalence rule (decision D6):
  Two candidates (from Engine A or B) are judged equivalent if on an expanded
  validation grid (band expanded by 10x at each end, 200 log-spaced points)
      max_k |Z1(jw_k) - Z2(jw_k)| / |Z1(jw_k)| < 1e-3.

Clustering and secondary criteria:
  Equivalent candidates are merged into an equivalence class represented by
  the lowest-AICc member. Within an equivalence class, candidates are ordered
  by secondary criteria:
    1. fewer elements first,
    2. physical plausibility (elements closer to typical nominal center).
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from .circuits import evaluate_f, leaf_kinds
from .fit_engine_a import Candidate

EQUIV_BAND_EXPAND = 10.0
EQUIV_N_POINTS = 200
EQUIV_MAX_REL_TOL = 1e-3


@dataclass
class EquivalenceClass:
    """A cluster of electrically equivalent candidate circuits."""

    representative: Candidate
    members: list[Candidate] = field(default_factory=list)

    @property
    def aicc(self) -> float:
        return self.representative.aicc_val

    @property
    def wrmse(self) -> float:
        return self.representative.wrmse

    @property
    def max_rel_err(self) -> float:
        return self.representative.max_rel_err

    @property
    def n_params(self) -> int:
        return self.representative.n_params


def make_validation_grid(f: np.ndarray,
                         expand: float = EQUIV_BAND_EXPAND,
                         n_points: int = EQUIV_N_POINTS) -> np.ndarray:
    """Log-spaced validation grid expanded by `expand` beyond the measured band."""
    f_min = float(np.min(f)) / expand
    f_max = float(np.max(f)) * expand
    return np.geomspace(f_min, f_max, n_points)


def are_equivalent(c1: Candidate, c2: Candidate, f_grid: np.ndarray,
                   tol: float = EQUIV_MAX_REL_TOL) -> bool:
    """Check if c1 and c2 yield indistinguishable responses on f_grid."""
    if c1.skipped or c2.skipped:
        return False
    z1 = evaluate_f(c1.tree, c1.theta, f_grid)
    z2 = evaluate_f(c2.tree, c2.theta, f_grid)
    denom = np.maximum(np.abs(z1), 1e-300)
    max_rel = float(np.max(np.abs(z1 - z2) / denom))
    return max_rel < tol


def secondary_sort_key(cand: Candidate) -> tuple[int, float]:
    """Secondary criterion: fewer elements first, then parameter plausibility."""
    # center deviation penalty: sum of (log10(v) - mid)^2
    # Typical nominal centers: R=1k (3.0), L=1mH (-3.0), C=10nF (-8.0)
    center_map = {"R": 3.0, "L": -3.0, "C": -8.0}
    penalty = sum((cand.theta[i] - center_map.get(k, 0.0)) ** 2
                  for i, k in enumerate(leaf_kinds(cand.tree)))
    return (cand.n_params, penalty)


def rank_and_cluster_equivalent(candidates: list[Candidate],
                                f: np.ndarray,
                                equiv_tol: float = EQUIV_MAX_REL_TOL,
                                n_obs: int | None = None,
                                ) -> list[EquivalenceClass]:
    """Merge equivalent candidates into equivalence classes, ranked by AICc.

    Parsimony pass (discrepancy principle): models whose per-dof RSS is
    statistically indistinguishable from the best achievable (within chi^2
    fluctuation) are all "noise-consistent"; among them the fewest-parameter
    model is promoted.  This prevents higher-order models from winning AICc
    merely by fitting noise when the true lower-order model is already at the
    noise floor.
    """
    valid = [c for c in candidates if not c.skipped and np.isfinite(c.aicc_val)]
    if not valid:
        return []

    if n_obs is None:
        n_obs = 2 * 60  # fallback; callers should pass the real 2M

    # noise-floor estimate from the best achievable per-dof RSS
    rss_per_dof = [(c.rss / max(n_obs - c.n_params, 1), c) for c in valid]
    sigma2_hat = min(r for r, _ in rss_per_dof)
    # chi^2 3-sigma upper fluctuation of the RSS/dof ratio
    margin = 3.0 * np.sqrt(2.0 / max(n_obs, 1))
    consistent = [c for r, c in rss_per_dof
                  if r <= sigma2_hat * (1.0 + margin)]
    min_p = min(c.n_params for c in consistent)
    champions = [c for c in consistent if c.n_params == min_p]
    champion = min(champions, key=lambda c: c.aicc_val)

    # Sort candidates: champion first, then remaining by AICc
    rest = [c for c in valid if c is not champion]
    rest.sort(key=lambda c: c.aicc_val)
    ordered = [champion] + rest

    # Noise-aware equivalence tolerance: with default relative weights the
    # per-point residual sigma equals sigma_rel_hat = sqrt(RSS/n_obs), and
    # two noise-floor fits of the same physical circuit can differ by a few
    # sigma at the worst grid point.  T2 equivalence (same rational function)
    # must not be split merely because of independent noise realizations.
    sigma_rel_hat = float(np.sqrt(sigma2_hat))
    eff_equiv_tol = max(equiv_tol, 3.0 * sigma_rel_hat)

    f_grid = make_validation_grid(f)

    classes: list[EquivalenceClass] = []
    for cand in ordered:
        matched = False
        for eq in classes:
            if are_equivalent(cand, eq.representative, f_grid, tol=eff_equiv_tol):
                eq.members.append(cand)
                matched = True
                break
        if not matched:
            classes.append(EquivalenceClass(representative=cand, members=[cand]))

    # Within each class, sort members by secondary criteria
    for eq in classes:
        eq.members.sort(key=secondary_sort_key)

    return classes
