"""rlc_id: unknown RLC one-port network identification.

Public entry point: ``identify(f, z, weights=None, config=None)`` following
the dual-engine flow of DESIGN.md appendix B.2:

  0. extract asymptotic features (F2 pre-judgement, §5.4);
  1. engine B: SK rational fit + Foster I/II closed-form candidates (§6),
     whose pole structure feeds the F3 pruning bounds;
  2. prune the canonical topology library (F2 + F3, §7);
  3. engine A: multi-start complex-domain weighted least squares over the
     pruned library (§5), seeded with the Foster solutions;
  4. selector: AICc ranking + equivalence clustering (§5.5, D6).
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from . import fit_engine_a, fit_engine_b, library, pruning, selector, synthetic
from .circuits import KIND_BOUNDS, Leaf, Node, Tree, canonical, evaluate, to_string
from .fit_engine_a import Candidate, EngineAConfig, default_weights
from .fit_engine_b import RationalModel
from .pruning import AsymptoticFeatures
from .selector import EquivalenceClass

__all__ = [
    "identify", "Config", "IdentifyResult", "Candidate", "EquivalenceClass",
    "RationalModel", "AsymptoticFeatures", "circuits", "library", "synthetic",
    "pruning", "selector", "fit_engine_a", "fit_engine_b", "report",
    "to_string", "canonical",
]

from . import circuits, report  # noqa: E402  (re-exported submodules)


@dataclass
class Config:
    """Identification configuration.

    max_n:      engine-A library size limit (DESIGN.md A1 default is 4;
                DUT8-class problems need 5).
    max_order:  engine-B rational order scan limit (pole pairs need 2 each).
    """

    max_n: int = 4
    max_order: int = 4
    sk_iters: int = 15
    enable_f2: bool = True
    enable_f3: bool = True
    n_starts_coarse: int = 3
    n_starts_refine: int = 10
    refine_fraction: float = 0.2
    max_idepth: int = library.DEFAULT_MAX_IDEPTH
    seed: int = 0

    def engine_a_config(self) -> EngineAConfig:
        return EngineAConfig(n_starts_coarse=self.n_starts_coarse,
                             n_starts_refine=self.n_starts_refine,
                             refine_fraction=self.refine_fraction,
                             seed=self.seed)


@dataclass
class IdentifyResult:
    """Full result of an identification run."""

    classes: list[EquivalenceClass]
    features: AsymptoticFeatures
    z_model: RationalModel
    y_model: RationalModel
    foster: list[Candidate]          # all Foster candidates incl. skipped (D8)
    n_library: int
    n_pruned_kept: int

    @property
    def best(self) -> EquivalenceClass | None:
        return self.classes[0] if self.classes else None

    def __iter__(self):
        return iter(self.classes)

    def __len__(self):
        return len(self.classes)


def identify(f, z, weights=None, config: Config | None = None) -> IdentifyResult:
    """Identify an unknown RLC one-port from impedance samples.

    f:       frequencies [Hz]; z: complex impedance samples (same length);
    weights: residual weights w_k (if known-noise, pass 1/sigma_k); when None
             the default relative model w_k = 1/|z_k| is used (A3);
    config:  see Config.
    """
    cfg = config or Config()
    f = np.asarray(f, dtype=float)
    z = np.asarray(z, dtype=complex)
    w = 2.0 * np.pi * f
    s = 1j * w
    wts = np.asarray(weights, dtype=float) if weights is not None \
        else default_weights(z)

    # 0. asymptotic features (F2 pre-judgement, heuristic starts)
    features = pruning.extract_asymptotics(w, z)
    hints = pruning.hints_from_features(features)

    # 1. engine B: rational fit + Foster synthesis (closed-form candidates)
    foster, z_model, y_model = fit_engine_b.foster_candidates(
        w, z, wts, max_order=cfg.max_order, n_iters=cfg.sk_iters)

    # 2. prune library (F2 asymptotics + F3 conservative energy bound)
    lib = list(library.get_library(cfg.max_n, cfg.max_idepth))
    min_energy = fit_engine_b.conservative_energy_bound(z_model, s)
    kept = pruning.prune(lib, features, min_energy,
                         enable_f2=cfg.enable_f2, enable_f3=cfg.enable_f3)

    # 3. engine A: two-stage multi-start fit; Foster solutions as starts
    extra_starts: dict[str, list[np.ndarray]] = {}
    for cand in foster:
        if not cand.skipped:
            extra_starts.setdefault(cand.canonical, []).append(cand.theta)
    fits_a = fit_engine_a.fit_library(kept, s, z, wts,
                                      config=cfg.engine_a_config(),
                                      hints=hints, extra_starts=extra_starts)

    # 4. selector: merge engines, cluster equivalents, noise-consistent
    #    parsimony ranking (D6 + discrepancy principle)
    pool = fits_a + [c for c in foster if not c.skipped]
    classes = selector.rank_and_cluster_equivalent(pool, f, n_obs=2 * len(z))

    return IdentifyResult(classes=classes, features=features,
                          z_model=z_model, y_model=y_model, foster=foster,
                          n_library=len(lib), n_pruned_kept=len(kept))
