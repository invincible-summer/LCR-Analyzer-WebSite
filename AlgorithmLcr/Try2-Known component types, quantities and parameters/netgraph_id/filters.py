"""Pruning funnel (DESIGN.md section 7).

With known component values every candidate is *exactly* evaluable at any
frequency, so the cheapest complete filter is a small set of probe
frequencies (band edges + middle): a wrong wiring of exact-valued
components is wrong by O(1) relative error somewhere in the band, while
the true wiring sits at the noise floor (~sigma_rel).  The funnel keeps
every candidate whose probe RSS is within `funnel_ratio` of the running
best (generous by construction: it can only drop candidates that are wrong
by more than ~sqrt(funnel_ratio) relative, far beyond any noise level),
then fully evaluates the survivors.  This subsumes the classical
DC/HF-asymptote checks (Z(0), Z(inf)) which are additionally reported for
transparency.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from .components import ComponentSet
from .enumerate import enumerate_structures, iter_assignments
from .graph import Network, Structure
from .nodal import StructureStamps
from .metric import weighted_rss


def coarse_indices(M: int, n_points: int = 3) -> list[int]:
    """Probe frequencies: band edges + interior, spread over the band."""
    if M <= n_points:
        return list(range(M))
    return sorted(set([0, M - 1] + [round(i * (M - 1) / (n_points - 1)) for i in range(n_points)]))


@dataclass
class FunnelState:
    """Streaming funnel accumulator across structures."""

    compset: ComponentSet
    s: np.ndarray                    # full angular frequencies (M,)
    z: np.ndarray                    # measurements (M,)
    w: np.ndarray                    # weights (M,)
    probe_idx: list[int]
    funnel_ratio: float
    min_keep: int
    batch_size: int = 4096

    best_probe: float = np.inf
    kept: list[tuple] = field(default_factory=list)   # (probe_rss, Network)
    n_total: int = 0
    n_probe_evaluated: int = 0

    def _update(self, nets: list[Network], probe_rss: np.ndarray) -> None:
        for net, prss in zip(nets, probe_rss):
            self.n_total += 1
            if prss < self.best_probe:
                self.best_probe = float(prss)
        self.n_probe_evaluated += len(nets)
        thr = self.best_probe * self.funnel_ratio
        for net, prss in zip(nets, probe_rss):
            if prss <= thr:
                self.kept.append((float(prss), net))

    def final_keep(self) -> list[Network]:
        """Survivors after the global best is known; always keep some."""
        thr = self.best_probe * self.funnel_ratio
        kept = [net for prss, net in self.kept if prss <= thr]
        if len(kept) < self.min_keep:
            self.kept.sort(key=lambda t: t[0])
            kept = [net for _, net in self.kept[:self.min_keep]]
        return kept


def run_funnel(compset: ComponentSet, s: np.ndarray, z: np.ndarray,
               w: np.ndarray, config) -> FunnelState:
    """Enumerate all structures, stream assignments, probe-filter them."""
    E = compset.n
    structures = enumerate_structures(E, allow_dead=config.allow_dead)
    probe_idx = coarse_indices(len(s), config.coarse_points)
    state = FunnelState(compset=compset, s=s, z=z, w=w, probe_idx=probe_idx,
                        funnel_ratio=config.funnel_ratio,
                        min_keep=config.funnel_min_keep,
                        batch_size=config.batch_size)
    s_probe = s[probe_idx]
    z_probe = z[probe_idx]
    w_probe = w[probe_idx]

    for structure in structures:
        stamps = StructureStamps.build(structure, compset)
        batch: list[tuple[int, ...]] = []
        for assign in iter_assignments(structure, compset):
            batch.append(assign)
            if len(batch) >= state.batch_size:
                _probe_batch(state, stamps, batch, s_probe, z_probe, w_probe)
                batch = []
        if batch:
            _probe_batch(state, stamps, batch, s_probe, z_probe, w_probe)
    return state


def _probe_batch(state: FunnelState, stamps: StructureStamps,
                 batch: list[tuple[int, ...]], s_probe, z_probe,
                 w_probe) -> None:
    assigns = np.array(batch, dtype=np.intp)
    z_model = stamps.z_full(assigns, s_probe)         # (N, P)
    probe_rss = weighted_rss(z_probe, z_model, w_probe)
    nets = [Network(structure=stamps.structure, assign=tuple(a)) for a in batch]
    state._update(nets, probe_rss)
