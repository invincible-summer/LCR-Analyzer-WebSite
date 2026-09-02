"""Candidate ranking and equivalence clustering (DESIGN.md section 8).

Mirrors the Try1 selector philosophy: rank by RSS (AICc adds a constant
here since every candidate has the same component set), then merge
electrically equivalent candidates -- distinct wirings can share the same
Z(s) exactly (graph symmetries, Whitney 2-isomorphism, value-coincidence
Y-delta transformations), and the correct output of an identification run
is an ordered list of equivalence classes, not a unique "winner".

Equivalence test (Try1 selector): maximum relative difference of Z on a
validation grid (band expanded 10x at each end, 200 log-spaced points)
below max(tol, 3 * sigma_rel_hat).

Secondary order inside a class (Try1 uses fewest elements + plausibility):
fewer internal junctions first, then series-parallel wirings before
bridged ones (Valdes-Tarjan-Puech test), then canonical serialization.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from .components import ComponentSet
from .graph import Network, is_series_parallel
from .metric import aicc, fit_metrics, rss_of, residual_vector
from .nodal import StructureStamps, network_z


@dataclass
class Candidate:
    """One evaluated candidate network."""

    network: Network
    z_fit: np.ndarray                    # model Z at the measured freqs
    rss: float
    aicc_val: float
    wrmse: float
    max_rel_err: float
    sp: bool = True                      # series-parallel wiring?
    note: str = ""

    @property
    def n_internal(self) -> int:
        return self.network.structure.n_internal


@dataclass
class EquivalenceClass:
    representative: Candidate
    members: list[Candidate] = field(default_factory=list)

    @property
    def rss(self) -> float:
        return self.representative.rss

    @property
    def wrmse(self) -> float:
        return self.representative.wrmse

    @property
    def max_rel_err(self) -> float:
        return self.representative.max_rel_err

    @property
    def aicc(self) -> float:
        return self.representative.aicc_val

    @property
    def n_members(self) -> int:
        return 1 + len(self.members)


def evaluate_candidates(nets: list[Network], compset: ComponentSet,
                        s: np.ndarray, z: np.ndarray, w: np.ndarray,
                        batch_size: int = 4096) -> list[Candidate]:
    """Full-band evaluation of funnel survivors into Candidates."""
    n_obs = 2 * len(z)
    p = compset.n_params
    out: list[Candidate] = []
    by_structure: dict = {}
    for net in nets:
        by_structure.setdefault(net.structure.key, (net.structure, []))[1].append(net.assign)
    for structure, assigns in by_structure.values():
        stamps = StructureStamps.build(structure, compset)
        sp_flag = is_series_parallel(structure.V, structure.mult)
        for i0 in range(0, len(assigns), batch_size):
            chunk = np.array(assigns[i0:i0 + batch_size], dtype=np.intp)
            z_model = stamps.z_full(chunk, s)
            for row, assign in zip(range(chunk.shape[0]), chunk):
                zf = z_model[row]
                if not np.all(np.isfinite(zf)):
                    continue
                res = residual_vector(z, zf, w)
                rss = rss_of(res)
                wrmse, mre = fit_metrics(z, zf)
                out.append(Candidate(
                    network=Network(structure=structure, assign=tuple(int(x) for x in assign)),
                    z_fit=zf, rss=rss, aicc_val=aicc(rss, n_obs, p),
                    wrmse=wrmse, max_rel_err=mre, sp=sp_flag))
    out.sort(key=lambda c: c.rss)
    return out


def make_validation_grid(f: np.ndarray, n: int = 200,
                         expand: float = 10.0) -> np.ndarray:
    """Band expanded `expand`x at each end, `n` log-spaced points (Try1)."""
    fmin, fmax = float(np.min(f)), float(np.max(f))
    return np.logspace(np.log10(fmin / expand), np.log10(fmax * expand), n)


def are_equivalent(net_a: Network, net_b: Network, compset: ComponentSet,
                   grid: np.ndarray, tol: float) -> bool:
    """Try1 numerical equivalence: max_k |Z1 - Z2| / |Z1| < tol on the grid."""
    za = network_z(net_a, compset, grid)
    zb = network_z(net_b, compset, grid)
    return _rel_diff_below(za, zb, tol)


def _rel_diff_below(za: np.ndarray, zb: np.ndarray, tol: float) -> bool:
    denom = np.abs(za)
    tiny = denom < 1e-300
    if np.any(tiny):
        denom = np.where(tiny, np.abs(zb), denom)
    rel = np.abs(za - zb) / denom
    rel = rel[np.isfinite(rel)]
    if len(rel) == 0:
        return False
    return float(np.max(rel)) < tol


def rank_and_cluster(candidates: list[Candidate], compset: ComponentSet,
                     f: np.ndarray, *, cluster_top: int = 50,
                     equiv_tol: float = 1e-3) -> list[EquivalenceClass]:
    """Sort by RSS and cluster the top `cluster_top` candidates."""
    if not candidates:
        return []
    sigma_hat = min(c.wrmse for c in candidates)
    tol = max(equiv_tol, 3.0 * sigma_hat)
    grid = make_validation_grid(f)
    top = candidates[:cluster_top]
    z_grids = [network_z(c.network, compset, grid) for c in top]
    classes: list[tuple[EquivalenceClass, np.ndarray]] = []
    for cand, zg in zip(top, z_grids):
        for cl, z_rep in classes:
            if _rel_diff_below(z_rep, zg, tol):
                cl.members.append(cand)
                break
        else:
            classes.append((EquivalenceClass(representative=cand), zg))
    keys = [comp.key() for comp in compset.components]
    out: list[EquivalenceClass] = []
    for cl, _ in classes:
        everyone = [cl.representative] + cl.members
        everyone.sort(key=lambda c: _secondary_key(c, keys))
        cl.representative, cl.members = everyone[0], everyone[1:]
        out.append(cl)
    out.sort(key=lambda cl: cl.rss)
    return out


def _secondary_key(c: Candidate, comp_keys: list[tuple]) -> tuple:
    return (c.n_internal, 0 if c.sp else 1,
            str(c.network.serialize(comp_keys)))
