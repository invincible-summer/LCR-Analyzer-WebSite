"""Engine A: complex-domain weighted least squares over a topology library.

DESIGN.md section 5:
  * residuals stack real/imaginary parts of w_k * (z_hat_k - Z(jw_k; theta))
    with w_k = 1/sigma_k or, by default, w_k = 1/|z_hat_k| (section 5.1, D4);
  * Jacobians by the complex-step method with h = 1e-20 on the log10
    parameters (section 5.2) -- Z is analytic in theta, so the derivative is
    exact to machine precision with no subtractive cancellation;
  * scipy least_squares TRF with per-kind bounds (section 4.3);
  * multi-start: Latin-hypercube starts plus asymptotic heuristic starts
    (section 5.4), organized as a two-stage coarse -> refine funnel (F4);
  * goodness of fit: AICc with n_obs = 2M and K = p + 1 (section 5.5).
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np
from scipy.optimize import least_squares
from scipy.stats import qmc

from .circuits import (KIND_BOUNDS, DCR_BOUNDS, Tree, bounds, canonical,
                       evaluate, evaluate_jac, param_kinds, n_params)

COMPLEX_STEP_H = 1e-20


# ---------------------------------------------------------------------------
# residuals / jacobian / metrics
# ---------------------------------------------------------------------------

def default_weights(z: np.ndarray) -> np.ndarray:
    """Default relative-error weights w_k = 1/|z_hat_k| (model A3)."""
    return 1.0 / np.abs(z)


def residual_vector(tree: Tree, theta, s: np.ndarray, z: np.ndarray,
                    w: np.ndarray) -> np.ndarray:
    """Interleaved [Re r_1, Im r_1, Re r_2, ...] weighted residual."""
    r = w * (z - evaluate(tree, theta, s))
    out = np.empty(2 * len(z))
    out[0::2] = r.real
    out[1::2] = r.imag
    return out


def jacobian_cs(tree: Tree, theta, s: np.ndarray, w: np.ndarray) -> np.ndarray:
    """Exact Jacobian of the weighted residual wrt log10 parameters.

    Uses forward-mode AD (circuits.evaluate_jac).  NOTE: DESIGN.md section 5.2
    specified a complex-step derivative, but that method is invalid here --
    Z is complex-valued at real theta, so Im[Z(theta+i h)]/h is dominated by
    Im[Z(theta)]/h rather than the derivative.  Forward AD is exact to
    machine precision at the cost of a single tree evaluation."""
    _, J = evaluate_jac(tree, theta, s)          # J[i, k] = dZ_k/dtheta_i
    p = J.shape[0]
    out = np.empty((2 * len(s), p))
    dr = -w[None, :] * J                          # d(weighted residual)/dtheta
    out[0::2, :] = dr.real.T
    out[1::2, :] = dr.imag.T
    return out


def rss_of(residual: np.ndarray) -> float:
    return float(residual @ residual)


def aicc(rss: float, n_obs: int, p: int) -> float:
    """Corrected AIC (section 5.5); n_obs = 2M real equations, K = p + 1."""
    k = p + 1
    rss = max(rss, 1e-300)
    denom = max(n_obs - k - 1, 1)
    return n_obs * np.log(rss / n_obs) + 2 * k + 2 * k * (k + 1) / denom


def fit_metrics(z: np.ndarray, zfit: np.ndarray) -> tuple[float, float]:
    """(weighted) relative RMSE and max relative error (section 8.1)."""
    rel = np.abs((z - zfit) / z)
    return float(np.sqrt(np.mean(rel**2))), float(np.max(rel))


# ---------------------------------------------------------------------------
# candidate container
# ---------------------------------------------------------------------------

@dataclass
class Candidate:
    """One fitted topology: structure + log10 parameters + quality metrics."""

    tree: Tree
    theta: np.ndarray
    rss: float
    aicc_val: float
    wrmse: float
    max_rel_err: float
    engine: str = "A"
    note: str = ""
    skipped: bool = False

    @property
    def n_params(self) -> int:
        return len(self.theta)

    @property
    def canonical(self) -> str:
        return canonical(self.tree)

    @property
    def values(self) -> np.ndarray:
        return np.power(10.0, self.theta)


# ---------------------------------------------------------------------------
# heuristic starting points (section 5.4)
# ---------------------------------------------------------------------------

@dataclass
class StartHints:
    """Data-driven magnitudes used to seed the optimizer (from pruning.py)."""

    r_level: float
    l_est: float
    c_est: float
    w_res: float | None = None  # angular frequency of a |Z| extremum, if any
    r_peak: float | None = None  # max|Z|: parallel-resonance R level


def _clip_kind(kind: str, value: float) -> float:
    lo, hi = KIND_BOUNDS[kind]
    return float(np.clip(value, 10.0**lo, 10.0**hi))


def heuristic_starts(tree: Tree, hints: StartHints | None) -> list[np.ndarray]:
    """Asymptotic heuristic starts for this topology (section 5.4).

    start 0: plain asymptotic magnitudes (R from the flat level, L from the
             high-frequency slope, C from the low-frequency slope; the DCR
             parameter of an L device starts at the geometric middle of its
             search box -- no data-driven estimate is robust enough);
    start 1: resonance-informed (only when the data show an interior |Z|
             extremum and the topology contains both L and C): L*C = 1/w0^2.
    """
    pks = param_kinds(tree)
    lb, ub = bounds(tree)
    mid = 0.5 * (lb + ub)
    rd_mid = 0.5 * (DCR_BOUNDS[0] + DCR_BOUNDS[1])

    def base_estimate(pk: str, i: int) -> float:
        if hints is None:
            return rd_mid if pk == "Rd" else mid[i]
        if pk == "R":
            return np.log10(_clip_kind("R", hints.r_level))
        if pk == "L":
            return np.log10(_clip_kind("L", hints.l_est))
        if pk == "Rd":
            return rd_mid
        return np.log10(_clip_kind("C", hints.c_est))

    starts = [np.array([base_estimate(pk, i) for i, pk in enumerate(pks)])]

    if hints is not None and hints.w_res is not None and "L" in pks and "C" in pks:
        th = starts[0].copy()
        l_first = np.log10(_clip_kind("L", hints.l_est))
        for i, pk in enumerate(pks):
            if pk == "L":
                th[i] = l_first
        for i, pk in enumerate(pks):
            if pk == "C":
                lc = 1.0 / hints.w_res**2
                th[i] = np.log10(_clip_kind("C", lc / 10.0**l_first))
        # parallel resonance: R is at the peak of |Z|, not the band median
        if hints.r_peak is not None:
            for i, pk in enumerate(pks):
                if pk == "R":
                    th[i] = np.log10(_clip_kind("R", hints.r_peak))
        starts.append(np.clip(th, lb, ub))
    return [np.clip(st, lb, ub) for st in starts]


def lhs_starts(n: int, lb: np.ndarray, ub: np.ndarray,
               rng: np.random.Generator) -> list[np.ndarray]:
    """Latin-hypercube starts over the log10 bound box."""
    if n <= 0:
        return []
    d = len(lb)
    unit = qmc.LatinHypercube(d=d, seed=int(rng.integers(0, 2**31 - 1))).random(n)
    return [lb + u * (ub - lb) for u in unit]


# ---------------------------------------------------------------------------
# single-topology fit
# ---------------------------------------------------------------------------

def fit_topology(tree: Tree, s: np.ndarray, z: np.ndarray, w: np.ndarray,
                 starts: list[np.ndarray], max_nfev: int | None = None,
                 tol: float = 1e-11) -> Candidate | None:
    """Fit one topology from several starts; return the best candidate."""
    lb, ub = bounds(tree)
    p = len(lb)
    best: Candidate | None = None
    for x0 in starts:
        x0 = np.clip(np.asarray(x0, dtype=float), lb + 1e-12, ub - 1e-12)
        try:
            res = least_squares(
                lambda th: residual_vector(tree, th, s, z, w),
                x0,
                jac=lambda th: jacobian_cs(tree, th, s, w),
                bounds=(lb, ub),
                method="trf",
                loss="linear",
                max_nfev=max_nfev,
                xtol=tol,
                ftol=tol,
                gtol=tol,
            )
        except Exception:
            continue
        rss = rss_of(res.fun)
        if best is None or rss < best.rss:
            zfit = evaluate(tree, res.x, s)
            wrmse, emax = fit_metrics(z, zfit)
            best = Candidate(tree=tree, theta=res.x, rss=rss,
                             aicc_val=aicc(rss, 2 * len(z), p),
                             wrmse=wrmse, max_rel_err=emax)
    return best


# ---------------------------------------------------------------------------
# library fit with two-stage funnel (F4)
# ---------------------------------------------------------------------------

@dataclass
class EngineAConfig:
    n_starts_coarse: int = 3
    n_starts_refine: int = 10
    refine_fraction: float = 0.2
    max_nfev_coarse: int = 40
    max_nfev_refine: int | None = None
    tol_coarse: float = 1e-6
    tol_refine: float = 1e-11
    seed: int = 0


def fit_library(trees: list[Tree], s: np.ndarray, z: np.ndarray,
                w: np.ndarray, config: EngineAConfig | None = None,
                hints: StartHints | None = None,
                extra_starts: dict[str, list[np.ndarray]] | None = None,
                ) -> list[Candidate]:
    """Fit every topology in ``trees`` (two-stage coarse -> refine, F4).

    ``extra_starts`` maps a canonical topology string to additional starting
    vectors (used to inject engine-B Foster solutions as starts, which also
    guarantees such topologies survive the coarse stage).
    """
    cfg = config or EngineAConfig()
    rng = np.random.default_rng(cfg.seed)
    extra_starts = extra_starts or {}

    # ---- stage 1: coarse pass over all topologies with few starts ----------
    coarse: list[Candidate] = []
    for tree in trees:
        lb, ub = bounds(tree)
        starts = heuristic_starts(tree, hints)
        starts += list(extra_starts.get(canonical(tree), []))
        n_lhs = max(cfg.n_starts_coarse - len(starts), 1)
        starts += lhs_starts(n_lhs, lb, ub, rng)
        cand = fit_topology(tree, s, z, w, starts,
                            max_nfev=cfg.max_nfev_coarse, tol=cfg.tol_coarse)
        if cand is not None:
            coarse.append(cand)
    if not coarse:
        return []

    # ---- stage 2: refine the best fraction with the full start set ---------
    coarse.sort(key=lambda c: c.rss)
    n_ref = max(1, int(np.ceil(cfg.refine_fraction * len(coarse))))
    # topologies carrying engine-B starts always advance
    front = {c.canonical for c in coarse[:n_ref]} | set(extra_starts)
    refined: list[Candidate] = []
    for cand in coarse:
        if cand.canonical not in front:
            refined.append(cand)
            continue
        tree = cand.tree
        lb, ub = bounds(tree)
        starts = [cand.theta]
        starts += heuristic_starts(tree, hints)
        starts += list(extra_starts.get(canonical(tree), []))
        n_lhs = max(cfg.n_starts_refine - len(starts), 2)
        starts += lhs_starts(n_lhs, lb, ub, rng)
        best = fit_topology(tree, s, z, w, starts,
                            max_nfev=cfg.max_nfev_refine, tol=cfg.tol_refine)
        refined.append(best if best is not None else cand)
    refined.sort(key=lambda c: c.aicc_val)
    return refined
