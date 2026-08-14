"""Automatic model discovery: vector fitting + topology library, ranked.

The candidate set is:

* the vector-fitted rational model (pole-residue form, order chosen by the
  data) with its Foster-synthesised RLC netlist, and
* every fixed topology from ``topology_fit.MODELS``.

All candidates share the same sigma-weighted residual convention, so their
AICc values are directly comparable. The result carries the winner plus the
full ranking table for the frontend.

The winner is picked by AICc *with a parsimony tie-break*: candidates whose
AICc is within 2 of the best are statistically indistinguishable
(Burnham & Anderson's rule of thumb); among those we prefer, in order:
a named topology over the (more opaque) rational model, then fewer elements.
"""
from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from .rational_fit import vector_fit, RationalFit, CHI2_ACCEPT
from .synthesis import synthesise, SynthesisResult
from .topology_fit import MODELS, fit_topology, TopologyFitResult


@dataclass
class Candidate:
    kind: str                    # "vf" | "topology"
    name: str                    # "auto_vf" | topology key
    label: str                   # human-readable
    n_params: int
    chi2_red: float
    aicc: float
    rmse: float
    # exactly one of the following is set
    vf: RationalFit | None = None
    topo: TopologyFitResult | None = None
    synthesis: SynthesisResult | None = None


@dataclass
class AutoFitResult:
    best: Candidate
    ranking: list[Candidate]
    theory: dict                                    # winner's dense curve
    freqs: np.ndarray
    z_meas: np.ndarray

    def to_summary(self) -> list[dict]:
        """JSON-friendly ranking rows for the API/frontend."""
        rows = []
        for rank, c in enumerate(self.ranking, start=1):
            rows.append({
                "rank": rank,
                "kind": c.kind,
                "model": c.name,
                "label": c.label,
                "n_params": c.n_params,
                "chi2_red": c.chi2_red,
                "aicc": c.aicc,
                "rmse": c.rmse,
                "delta_aicc": c.aicc - self.ranking[0].aicc,
                "selected": c is self.best,
            })
        return rows


def _count_elements(syn: SynthesisResult) -> int:
    n = 0
    stack = [syn.netlist]
    while stack:
        el = stack.pop()
        if el["type"] in ("R", "L", "C"):
            n += 1
        else:
            stack.extend(el.get("children", []))
    return n


def fit_auto(freqs, Z, sigma=None, n_max: int = 6) -> AutoFitResult:
    """Run the full candidate set and rank by AICc."""
    freqs = np.asarray(freqs, dtype=float)
    Z = np.asarray(Z, dtype=complex)

    candidates: list[Candidate] = []

    # --- vector fitting + Foster synthesis -------------------------------
    try:
        rfit = vector_fit(freqs, Z, sigma=sigma, n_max=n_max)
        syn = synthesise(rfit, float(freqs.min()), float(freqs.max()))
        rss = rfit.rss
        n = 2 * Z.size
        rmse = float(np.sqrt(np.mean(np.abs(Z - rfit.z_fit) ** 2)))
        candidates.append(Candidate(
            kind="vf", name="auto_vf",
            label=f"矢量拟合 · {rfit.order} 阶（Foster 综合，"
                  f"{_count_elements(syn)} 元件）",
            n_params=4 * rfit.order + 3,
            chi2_red=rfit.chi2_red, aicc=rfit.aicc, rmse=rmse,
            vf=rfit, synthesis=syn,
        ))
    except (RuntimeError, ValueError):
        pass

    # --- fixed topology library -------------------------------------------
    for name in MODELS:
        try:
            res = fit_topology(name, freqs, Z, sigma=sigma, n_starts=16)
        except (ValueError, np.linalg.LinAlgError):
            continue
        candidates.append(Candidate(
            kind="topology", name=name, label=MODELS[name].label,
            n_params=len(MODELS[name].params),
            chi2_red=res.chi2_red, aicc=res.aicc, rmse=res.rmse,
            topo=res,
        ))

    if not candidates:
        raise RuntimeError("all fitting candidates failed")

    candidates.sort(key=lambda c: c.aicc)
    best_aicc = candidates[0].aicc

    # "statistically indistinguishable" from the winner: within dAICc <= 2
    # (Burnham & Anderson) *or* already at the noise level (chi2_red <= 4 --
    # for clean data the log-RSS differences between two perfect fits are
    # numerical artifacts, not evidence). Prefer a named, interpretable
    # topology over the more opaque rational model.
    tied = [c for c in candidates
            if c.aicc - best_aicc <= 2.0 or c.chi2_red <= CHI2_ACCEPT]
    best = next((c for c in tied if c.kind == "topology"), tied[0])

    if best.kind == "vf":
        theory = best.vf.theory_grid(float(freqs.min()), float(freqs.max()))
    else:
        theory = best.topo.theory

    return AutoFitResult(best=best, ranking=candidates, theory=theory,
                         freqs=freqs, z_meas=Z)
