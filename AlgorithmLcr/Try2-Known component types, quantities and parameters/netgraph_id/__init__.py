"""netgraph_id -- identification of a 2-terminal RLC network whose
component multiset is fully known (Try2).

Public entry point: identify(components, f, z).

Problem (DESIGN.md section 1): the exact set of components is known
(resistors and capacitors ideal; each inductor = ideal L in series with a
known DC resistance) together with noisy impedance measurements z(f); the
wiring topology is the only unknown.  The algorithm enumerates every
admissible 2-terminal multigraph structure up to isomorphism (parallel
edges included), assigns the known components to the edges, evaluates each
candidate exactly by nodal analysis and ranks by the Try1 error metrics.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field

import numpy as np

from .components import Component, ComponentSet
from .enumerate import enumerate_structures, iter_assignments
from .filters import run_funnel
from .graph import Network, Structure
from .nodal import network_z
from .selector import Candidate, EquivalenceClass, evaluate_candidates, rank_and_cluster
from . import components, enumerate, filters, graph, metric, nodal, selector, synthetic, report

__all__ = [
    "identify", "Config", "IdentifyResult", "Candidate", "EquivalenceClass",
    "ComponentSet", "Component", "Network", "Structure",
    "enumerate_structures", "iter_assignments", "network_z",
    "synthetic", "report",
]


@dataclass
class Config:
    """Search parameters (DESIGN.md section 7)."""

    coarse_points: int = 3          # probe frequencies in the funnel
    funnel_ratio: float = 1e6       # keep probe-RSS <= best * ratio
    funnel_min_keep: int = 200      # lower bound on funnel survivors
    batch_size: int = 4096          # candidates per batched nodal solve
    cluster_top: int = 50           # how many best candidates to cluster
    equiv_tol: float = 1e-3         # equivalence tolerance floor
    allow_dead: bool = False        # include electrically dead structures
    top_k: int = 8                  # reported classes


@dataclass
class IdentifyResult:
    classes: list[EquivalenceClass]
    compset: ComponentSet
    n_candidates: int
    n_funnel_kept: int
    n_structures: int
    elapsed: float
    timings: dict = field(default_factory=dict)

    @property
    def best(self) -> EquivalenceClass | None:
        return self.classes[0] if self.classes else None

    def __iter__(self):
        return iter(self.classes)

    def __len__(self) -> int:
        return len(self.classes)


def identify(components: ComponentSet, f: np.ndarray, z: np.ndarray,
             weights: np.ndarray | None = None,
             config: Config | None = None) -> IdentifyResult:
    """Exhaustive topology identification with known component values.

    components: ComponentSet -- the exact multiset of DUT components;
    f (Hz), z (complex): measurements; weights: residual weights
    (default 1/|z|, Try1 A3).
    """
    config = config or Config()
    f = np.asarray(f, dtype=float)
    z = np.asarray(z, dtype=complex)
    if len(f) != len(z):
        raise ValueError("f and z must have the same length")
    w = np.asarray(weights) if weights is not None else 1.0 / np.abs(z)
    s = 2j * np.pi * f

    t0 = time.perf_counter()
    state = run_funnel(components, s, z, w, config)
    t_funnel = time.perf_counter() - t0

    t1 = time.perf_counter()
    survivors = state.final_keep()
    candidates = evaluate_candidates(survivors, components, s, z, w,
                                     batch_size=config.batch_size)
    t_eval = time.perf_counter() - t1

    t2 = time.perf_counter()
    classes = rank_and_cluster(candidates, components, f,
                               cluster_top=config.cluster_top,
                               equiv_tol=config.equiv_tol)
    t_cluster = time.perf_counter() - t2

    n_struct = len(enumerate_structures(components.n,
                                        allow_dead=config.allow_dead))
    return IdentifyResult(
        classes=classes, compset=components,
        n_candidates=state.n_total, n_funnel_kept=len(survivors),
        n_structures=n_struct, elapsed=time.perf_counter() - t0,
        timings={"funnel": t_funnel, "eval": t_eval, "cluster": t_cluster})
