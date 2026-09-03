"""Multi-start weighted complex NLS on a known topology (DESIGN.md sec.5).

Pipeline per graph:
  1. exact topology reduction (topofit_id.graph) -- removes structurally
     unidentifiable parameters before any numerics;
  2. dual normalization (Try1 D7a extended): s_tilde = s/w0 with
     w0 = geomean(w), z_tilde = z/z0 with z0 = geomean|z|; element values
     become O(1) numbers (R~=R/z0, L~=w0 L/z0, C~=z0 w0 C, Rd~=Rd/z0);
  3. multi-start TRF (scipy least_squares, Branch-Coleman-Li) on the
     log10 parameters with the *exact* adjoint Jacobian (nodal.z_and_jac),
     unit start + Latin hypercube starts, coarse -> polish funnel;
  4. escalation restarts while wrmse > threshold (local-minimum escape);
  5. diagnostics: elasticity dlnZ/dlnv (weak = band-invisible parameters),
     at-bound flags, merge-group reporting.

Error metrics follow Try1 sec.5.1/8.1 verbatim (topofit_id.metric).
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field

import numpy as np
from scipy.optimize import least_squares
from scipy.stats import qmc

from . import metric
from .graph import PortOpenError, ReductionResult, eval_group, reduce_graph
from .nodal import NodalModel, model_from_reduced

PHYS_BOUNDS = {
    "R": (1e-3, 1e7),     # ohm   (Try1 sec.4.3 search box)
    "C": (1e-13, 1e-3),   # farad
    "L": (1e-10, 1e1),    # henry
    "Rd": (1e-6, 1e7),    # ohm, inductor series DCR
}


@dataclass
class FitConfig:
    n_starts: int = 8             # full-box LHS coarse starts
    n_center: int = 8             # center-focused LHS (+-2 decades, the
                                  # dual normalization puts truth near 0)
    n_perturb: int = 12           # Gaussian log-space restarts around best
    n_polish: int = 3             # tight re-fit of the best starts
    tol_coarse: float = 1e-8
    tol_polish: float = 1e-13
    max_nfev_factor: int = 25     # coarse max_nfev = max(120, factor * p)
    seed: int = 0
    escalation_rounds: int = 3    # extra LHS batches while wrmse > threshold
    escalation_wrmse: float = 0.03
    vis_threshold: float = 0.1    # max|dlnZ/dlnv| below this = weak param


@dataclass
class GroupReport:
    gid: int
    kind: str
    u: int
    v: int
    members: tuple
    mode: str                     # single | par | ser
    value: tuple                  # ("R", r) | ("C", c) | ("L", l, rd)
    weak_params: tuple            # names of weak params, () if none
    at_bound: tuple


@dataclass
class EdgeReport:
    index: int                    # original edge index
    kind: str
    status: str                   # fitted | merged | dropped
    group: int                    # group id or -1
    value: tuple                  # aggregate value if fitted/merged else ()
    note: str = ""


@dataclass
class FitResult:
    edges: list                   # original input [(u,v,kind), ...]
    reduction: ReductionResult
    ok: bool
    rss: float
    wrmse: float
    max_rel: float
    aicc_val: float
    n_params: int
    groups: list                  # [GroupReport]
    edges_out: list               # [EdgeReport]
    theta_norm: np.ndarray        # log10 normalized values, reduced order
    n_starts_used: int
    seconds: float
    jac_sv: tuple = ()            # singular values of weighted Jacobian at solution
    jac_rank: int = -1            # numerical rank (rtol 1e-6)
    jac_cond: float = np.inf      # sv_max/sv_min (inf if rank-deficient)
    _model: NodalModel = field(repr=False, default=None)
    _w0: float = field(repr=False, default=0.0)
    _z0: float = field(repr=False, default=0.0)

    # -- physical group values ------------------------------------------------
    def group_values(self) -> list:
        """Fitted value per reduced group, physical units."""
        return [g.value for g in self.groups]

    def z_model(self, f) -> np.ndarray:
        """Fitted impedance Z(f) at arbitrary frequencies (physical)."""
        f = np.asarray(f, dtype=float)
        s_t = 1j * 2.0 * np.pi * f / self._w0
        Z, _ = self._model.z_and_jac(self.theta_norm, s_t)
        return self._z0 * Z

    def describe(self) -> str:
        lines = []
        for g in self.groups:
            v = g.value
            vs = ("{:.6g}ohm".format(v[1]) if g.kind == "R" else
                  "{:.6g}F".format(v[1]) if g.kind == "C" else
                  "{:.6g}H+{:.6g}ohm".format(v[1], v[2]))
            extra = ""
            if g.weak_params:
                extra += " weak:" + ",".join(g.weak_params)
            if g.at_bound:
                extra += " at_bound:" + ",".join(g.at_bound)
            lines.append("  g{} {}[{}-{}] {} = {}{}".format(
                g.gid, g.kind, g.u, g.v,
                "single" if len(g.members) == 1 else g.mode + "-merged",
                vs, extra))
        for i, why in sorted(self.reduction.dropped.items()):
            lines.append("  e{} {} dropped ({})".format(
                i, self.edges[i][2], why))
        lines.append("  wRMSE={:.4g}  maxRel={:.4g}  AICc={:.2f}  ({:.1f} ms, {} starts)".format(
            self.wrmse, self.max_rel, self.aicc_val, self.seconds * 1e3,
            self.n_starts_used))
        if self.jac_rank >= 0 and self.jac_rank < self.n_params:
            lines.append("  identifiability: rank {}/{} -- parameter vector jointly".format(
                self.jac_rank, self.n_params)
                + " unidentifiable (only rank combinations are determined)")
        elif self.jac_cond > 1e4:
            lines.append("  identifiability: full rank but ill-conditioned (cond {:.1e})".format(
                self.jac_cond))
        return "\n".join(lines)


# ---------------------------------------------------------------------------
# normalization helpers
# ---------------------------------------------------------------------------

def _param_units(model: NodalModel) -> list:
    """Per param: (kind_letter, physical_scale).  phys = norm * scale."""
    units = []
    for e, (u, v, kind) in enumerate(model.edges):
        if kind == "R":
            units.append(("R", 1.0))
        elif kind == "C":
            units.append(("C", 1.0))
        else:
            units.append(("L", 1.0))
            units.append(("Rd", 1.0))
    return units


def _norm_scales(units, w0: float, z0: float) -> np.ndarray:
    """scale_t such that phys_value = norm_value * scale_t."""
    out = np.empty(len(units))
    for t, (letter, _s) in enumerate(units):
        if letter == "R":
            out[t] = z0
        elif letter == "L":
            out[t] = z0 / w0
        elif letter == "C":
            out[t] = 1.0 / (z0 * w0)
        else:                       # Rd
            out[t] = z0
    return out


# ---------------------------------------------------------------------------
# the solver core
# ---------------------------------------------------------------------------

class _Objective:
    """Weighted complex residual + exact adjoint Jacobian, memoized."""

    def __init__(self, model: NodalModel, s_t: np.ndarray, z_t: np.ndarray):
        self.model = model
        self.s_t = s_t
        self.w = 1.0 / np.abs(z_t)
        self.z_t = z_t
        self._cache = None

    def _eval(self, theta):
        if self._cache is not None and np.array_equal(self._cache[0], theta):
            return self._cache[1], self._cache[2]
        Z, J = self.model.z_and_jac(theta, self.s_t)
        self._cache = (theta.copy(), Z, J)
        return Z, J

    def fun(self, theta):
        Z, _ = self._eval(theta)
        return metric.residual_vector(self.z_t, Z, self.w)

    def jac(self, theta):
        _, J = self._eval(theta)
        p, M = J.shape
        out = np.empty((2 * M, p))
        dr = -self.w[None, :] * J
        out[0::2, :] = dr.real.T
        out[1::2, :] = dr.imag.T
        return out


def _unit_start(model: NodalModel, units) -> np.ndarray:
    theta = np.zeros(model.n_params)
    for t, (letter, _s) in enumerate(units):
        theta[t] = -1.0 if letter == "Rd" else 0.0
    return theta


def _lhs(model: NodalModel, lb: np.ndarray, ub: np.ndarray, n: int,
         rng: np.random.Generator) -> list:
    if n <= 0:
        return []
    d = model.n_params
    sampler = qmc.LatinHypercube(d=d, seed=int(rng.integers(0, 2**31 - 1)))
    u = sampler.random(n)
    return [lb + ui * (ub - lb) for ui in u]


def _resonance_omegas(s_t: np.ndarray, z_t: np.ndarray,
                      prominence: float = 4.0, max_res: int = 2) -> list:
    """Interior |z| extrema (peaks or notches) in normalized frequency.

    A high-Q parallel tank shows a sharp |Z| peak, a series resonance a
    notch.  Detection = strict interior extremum with prominence >= the
    given factor against the band median.  Returns normalized omega tilde
    of the up to max_res most prominent extrema (peaks first)."""
    a = np.abs(z_t)
    med = np.median(a)
    cands = []
    for k in range(1, len(a) - 1):
        if a[k] >= a[k - 1] and a[k] > a[k + 1]:
            prom = a[k] / med
            if prom >= prominence:
                cands.append((prom, float(np.abs(s_t[k]))))
        elif a[k] <= a[k - 1] and a[k] < a[k + 1]:
            prom = med / a[k]
            if prom >= prominence:
                cands.append((prom, float(np.abs(s_t[k]))))
    cands.sort(key=lambda t: -t[0])
    return [w for _p, w in cands[:max_res]]


def _resonance_starts(model: NodalModel, units, lb: np.ndarray, ub: np.ndarray,
                      s_t: np.ndarray, z_t: np.ndarray, base: np.ndarray) -> list:
    """Seed starts aligning every (L, C) parameter product with a detected
    resonance: L~ * C~ = 1/w~0^2 in normalized units.

    High-Q resonances make the optimum basin exponentially narrow in log(C)
    (relative width ~ 1/(2Q)); random LHS cannot hit it.  Aligning the
    L*C product to the observed extremum puts TRF inside the basin
    (Try1 sec.5.4 resonance heuristic, generalized to multigraphs)."""
    omegas = _resonance_omegas(s_t, z_t)
    if not omegas:
        return []
    l_idx = [t for t, (letter, _s) in enumerate(units) if letter == "L"]
    c_idx = [t for t, (letter, _s) in enumerate(units) if letter == "C"]
    if not l_idx or not c_idx:
        return []
    out = []
    for w0 in omegas:
        for li in l_idx:
            x = np.array(base, dtype=float)
            c = 10.0 ** x[c_idx[0]]
            x[li] = np.log10(1.0 / (w0 * w0 * c))
            out.append(np.clip(x, lb + 1e-12, ub - 1e-12))
        for ci in c_idx:
            x = np.array(base, dtype=float)
            l = 10.0 ** x[l_idx[0]]
            x[ci] = np.log10(1.0 / (w0 * w0 * l))
            out.append(np.clip(x, lb + 1e-12, ub - 1e-12))
    return out


def _lhs_center(model: NodalModel, lb: np.ndarray, ub: np.ndarray, n: int,
                rng: np.random.Generator, width: float = 2.0) -> list:
    """LHS restricted to +-width decades around the unit point theta=0.

    After dual normalization the data scale is O(1), so plausible element
    values concentrate near log10 = 0; a second, center-focused bank of
    starts sharply raises the probability of hitting narrow basins
    (campaign case seed 799 was the motivator)."""
    if n <= 0:
        return []
    d = model.n_params
    sampler = qmc.LatinHypercube(d=d, seed=int(rng.integers(0, 2**31 - 1)))
    u = sampler.random(n)
    clo = np.maximum(lb, -width)
    chi = np.minimum(ub, width)
    return [clo + ui * (chi - clo) for ui in u]


def _perturb_starts(x_best: np.ndarray, lb: np.ndarray, ub: np.ndarray,
                    n: int, rng: np.random.Generator,
                    sigma: float = 1.0) -> list:
    """Gaussian restarts (log10-units) around the incumbent best point."""
    if n <= 0 or x_best is None:
        return []
    out = []
    for _ in range(n):
        x = x_best + rng.normal(0.0, sigma, size=len(x_best))
        out.append(np.clip(x, lb + 1e-12, ub - 1e-12))
    return out


def _topo_resonance_starts(model: NodalModel, units, lb: np.ndarray,
                           ub: np.ndarray, s_t: np.ndarray, z_t: np.ndarray,
                           base: np.ndarray) -> list:
    """Topology-aware resonance seeds (stage A).

    Special (L, C) placements whose resonance is a direct graph pattern:
      * parallel: an L edge and a C edge on the same node pair (tank);
      * series: an L edge and a C edge meeting at a degree-2 internal node.
    Both resonate at omega^2 = 1/(L C).  For each detected |z| extremum we
    start with each such pair aligned (everything else at the unit base),
    and with two pairs aligned to two extrema simultaneously (multi-tank
    networks, campaign seeds 20261995 / 20261441)."""
    omegas = _resonance_omegas(s_t, z_t)
    if not omegas:
        return []
    l_of, c_of = {}, {}
    for e, (u, v, kind) in enumerate(model.edges):
        if kind == "L":
            l_of[(min(u, v), max(u, v))] = e
        elif kind == "C":
            c_of[(min(u, v), max(u, v))] = e
    deg = {}
    for (u, v, _k) in model.edges:
        deg[u] = deg.get(u, 0) + 1
        deg[v] = deg.get(v, 0) + 1

    def param_of(e, want):
        sl, _cnt = model.edge_param_slices[e]
        return sl

    pairs = []
    seen = set()
    for pr, le in l_of.items():
        if pr in c_of:
            pairs.append((param_of(le, "L"), param_of(c_of[pr], "C")))
    for e1, (u1, v1, k1) in enumerate(model.edges):
        for e2, (u2, v2, k2) in enumerate(model.edges):
            if e2 <= e1 or {k1, k2} != {"L", "C"}:
                continue
            shared = {u1, v1} & {u2, v2}
            for w in shared:
                if w not in (0, 1) and deg.get(w, 0) == 2:
                    key = (min(e1, e2), max(e1, e2))
                    if key not in seen:
                        seen.add(key)
                        pairs.append((param_of(e1, "L") if k1 == "L" else param_of(e2, "L"),
                                      param_of(e1, "C") if k1 == "C" else param_of(e2, "C")))
    if not pairs:
        return []
    pairs = list(dict.fromkeys(pairs))[:6]

    def aligned(pairs_omegas):
        x = np.array(base, dtype=float)
        for (li, ci), w0 in pairs_omegas:
            c_val = 10.0 ** x[ci]
            x[li] = np.log10(1.0 / (w0 * w0 * c_val))
        return np.clip(x, lb + 1e-12, ub - 1e-12)

    out = []
    for w0 in omegas:
        for pr in pairs:
            out.append(aligned([(pr, w0)]))
    if len(omegas) >= 2 and len(pairs) >= 2:
        for i, pa in enumerate(pairs):
            for j, pb in enumerate(pairs):
                if i == j:
                    continue
                out.append(aligned([(pa, omegas[0]), (pb, omegas[1])]))
    return out


def _pair_resonance_restarts(model: NodalModel, units, lb: np.ndarray,
                             ub: np.ndarray, s_t: np.ndarray, z_t: np.ndarray,
                             x_best: np.ndarray) -> list:
    """Restarts aligning every (L, C) parameter product to each detected
    resonance, around the incumbent point.

    Multi-tank networks need SEVERAL L*C products aligned simultaneously;
    single-parameter resonance seeds (stage A) cover one product at a time
    from the unit point.  Restarting the full combo from the incumbent
    lets TRF pull the remaining tanks into place (campaign seeds 20261995
    etc. were the motivator)."""
    omegas = _resonance_omegas(s_t, z_t)
    if not omegas or x_best is None:
        return []
    l_idx = [t for t, (letter, _s) in enumerate(units) if letter == "L"]
    c_idx = [t for t, (letter, _s) in enumerate(units) if letter == "C"]
    out = []
    for w0 in omegas:
        for li in l_idx:
            for ci in c_idx:
                x = np.array(x_best, dtype=float)
                c_val = 10.0 ** x[ci]
                x[li] = np.log10(1.0 / (w0 * w0 * c_val))
                out.append(np.clip(x, lb + 1e-12, ub - 1e-12))
    return out


def _run_starts(obj: _Objective, starts, lb, ub, cfg: FitConfig):
    results = []
    max_nfev = max(120, cfg.max_nfev_factor * obj.model.n_params)
    for x0 in starts:
        x0 = np.clip(x0, lb + 1e-12, ub - 1e-12)
        try:
            res = least_squares(obj.fun, x0, jac=obj.jac, bounds=(lb, ub),
                                method="trf", loss="linear",
                                max_nfev=max_nfev,
                                xtol=cfg.tol_coarse, ftol=cfg.tol_coarse,
                                gtol=cfg.tol_coarse)
        except Exception:
            continue
        results.append((metric.rss_of(res.fun), res.x))
    results.sort(key=lambda t: t[0])
    return results


def _polish(obj: _Objective, starts_x, lb, ub, cfg: FitConfig):
    best_rss, best_x = np.inf, None
    for x0 in starts_x:
        try:
            res = least_squares(obj.fun, x0, jac=obj.jac, bounds=(lb, ub),
                                method="trf", loss="linear",
                                max_nfev=max(400, 60 * obj.model.n_params),
                                xtol=cfg.tol_polish, ftol=cfg.tol_polish,
                                gtol=cfg.tol_polish)
        except Exception:
            continue
        rss = metric.rss_of(res.fun)
        if rss < best_rss:
            best_rss, best_x = rss, res.x
    return best_rss, best_x


def _homotopy_rescue(obj: _Objective, model: NodalModel, units, lb, ub,
                    x0: np.ndarray):
    """Continuation on inductor damping (stage D, fires only when stuck).

    Raising every Rd lower bound forces a lossy, low-Q network whose
    objective landscape is smooth (no razor-thin resonance valleys);
    the damped optimum warm-starts the next, less damped stage
    (Bertsekas continuation / damped-Newton homotopy, standard for
    resonance alignment; campaign seed 20261143 was the motivator)."""
    x = np.clip(x0, lb + 1e-12, ub - 1e-12)
    for floor in (1.0, 0.0, -1.0, -2.0):
        lbk = lb.copy()
        for e, (_u, _v, kind) in enumerate(model.edges):
            if kind == "L":
                sl, _cnt = model.edge_param_slices[e]
                lbk[sl + 1] = max(lb[sl + 1], floor)
        x = np.clip(x, lbk + 1e-12, ub - 1e-12)
        try:
            res = least_squares(obj.fun, x, jac=obj.jac, bounds=(lbk, ub),
                                method="trf", loss="linear", max_nfev=2000,
                                xtol=1e-12, ftol=1e-12, gtol=1e-12)
            x = res.x
        except Exception:
            break
    return metric.rss_of(obj.fun(x)), x


def fit_graph(f, z, edges: list, config: FitConfig | None = None) -> FitResult:
    """Fit element values of a known-topology multigraph to (f, z) data."""
    cfg = config or FitConfig()
    f = np.asarray(f, dtype=float)
    z = np.asarray(z, dtype=complex)
    t_start = time.perf_counter()

    red = reduce_graph(edges)                       # exact, value-free
    model = model_from_reduced(red)

    omega = 2.0 * np.pi * f
    w0 = float(np.exp(np.mean(np.log(omega))))
    z0 = float(np.exp(np.mean(np.log(np.abs(z)))))
    s_t = 1j * omega / w0
    z_t = z / z0

    units = _param_units(model)
    scales = _norm_scales(units, w0, z0)
    lb = np.empty(model.n_params)
    ub = np.empty(model.n_params)
    for t, (letter, _s) in enumerate(units):
        lo, hi = PHYS_BOUNDS[letter]
        lb[t] = np.log10(lo / scales[t])
        ub[t] = np.log10(hi / scales[t])

    obj = _Objective(model, s_t, z_t)
    rng = np.random.default_rng(cfg.seed)
    wrmse_of = lambda rss: np.sqrt(rss / (2 * len(z)))

    # stage A: unit + full-box LHS + center-focused LHS + resonance seeds
    starts = [_unit_start(model, units)]
    starts += _lhs(model, lb, ub, cfg.n_starts, rng)
    starts += _lhs_center(model, lb, ub, cfg.n_center, rng)
    starts += _resonance_starts(model, units, lb, ub, s_t, z_t, starts[0])
    starts += _topo_resonance_starts(model, units, lb, ub, s_t, z_t, starts[0])
    results = _run_starts(obj, starts, lb, ub, cfg)
    n_used = len(starts)

    # stage B: perturbation restarts around the incumbent
    if results:
        pstarts = _perturb_starts(results[0][1], lb, ub, cfg.n_perturb, rng)
        results += _run_starts(obj, pstarts, lb, ub, cfg)
        results.sort(key=lambda t: t[0])
        n_used += len(pstarts)

    # stage B2: resonance-aligned (L, C) pair restarts around the incumbent
    if results:
        rstarts = _pair_resonance_restarts(model, units, lb, ub, s_t, z_t,
                                           results[0][1])
        if rstarts:
            results += _run_starts(obj, rstarts, lb, ub, cfg)
            results.sort(key=lambda t: t[0])
            n_used += len(rstarts)

    # stage C: escalation while still far above any plausible noise floor
    center_next = False
    for _round in range(max(0, cfg.escalation_rounds)):
        if results and wrmse_of(results[0][0]) <= cfg.escalation_wrmse:
            break
        extra = (_lhs_center(model, lb, ub, cfg.n_starts + cfg.n_center, rng)
                 if center_next else
                 _lhs(model, lb, ub, cfg.n_starts + cfg.n_center, rng))
        extra += _pair_resonance_restarts(model, units, lb, ub, s_t, z_t,
                                          results[0][1])
        center_next = not center_next
        results = _run_starts(obj, extra, lb, ub, cfg) + results
        results.sort(key=lambda t: t[0])
        n_used += len(extra)

    if not results:
        raise RuntimeError("all starts failed (numerical)")

    # stage D: damped continuation rescue when still far above the floor
    if wrmse_of(results[0][0]) > cfg.escalation_wrmse:
        for x0 in (results[0][1], _unit_start(model, units)):
            rss_h, x_h = _homotopy_rescue(obj, model, units, lb, ub, x0)
            results.append((rss_h, x_h))
            results.sort(key=lambda t: t[0])
            n_used += 1
            if wrmse_of(results[0][0]) <= cfg.escalation_wrmse:
                break
    rss_n, best_x = _polish(obj, [x for _r, x in results[:cfg.n_polish]],
                            lb, ub, cfg)
    if best_x is None or not np.isfinite(rss_n) or rss_n > results[0][0]:
        rss_n, best_x = results[0]

    Z_fit_t, _ = model.z_and_jac(best_x, s_t)
    z_fit = z0 * Z_fit_t
    wrmse, max_rel = metric.fit_metrics(z, z_fit)
    rss = float(np.sum(np.abs((z - z_fit) / z) ** 2))

    # diagnostics on physical elasticity
    phys_theta = best_x + np.log10(scales)          # log10 physical values
    E = model.elasticity(best_x, s_t)
    max_el = np.max(np.abs(E), axis=1)
    weak = max_el < cfg.vis_threshold
    at_bnd = (np.abs(best_x - lb) < 1e-9) | (np.abs(best_x - ub) < 1e-9)

    Jw = obj.jac(best_x)                       # (2M, p) real
    sv = np.linalg.svd(Jw, compute_uv=False)
    if len(sv):
        tol = sv[0] * max(Jw.shape) * np.finfo(float).eps
        rank = int(np.sum(sv > max(tol, sv[0] * 1e-6)))
        cond = float(sv[0] / sv[-1]) if rank == len(sv) else np.inf
    else:
        rank, cond = 0, np.inf

    groups, edges_out = _build_reports(red, edges, model, phys_theta, units,
                                       weak, at_bnd)
    seconds = time.perf_counter() - t_start
    return FitResult(
        edges=list(edges), reduction=red, ok=True, rss=rss, wrmse=wrmse,
        max_rel=max_rel, aicc_val=metric.aicc(rss, 2 * len(z), model.n_params),
        n_params=model.n_params, groups=groups, edges_out=edges_out,
        theta_norm=best_x, n_starts_used=n_used, seconds=seconds,
        jac_sv=tuple(float(v) for v in sv), jac_rank=rank, jac_cond=cond,
        _model=model, _w0=w0, _z0=z0)


def _build_reports(red, edges, model, phys_theta, units, weak, at_bnd):
    groups = []
    edges_out = []
    par_names = {"R": ("v",), "C": ("v",), "L": ("L", "Rd")}
    t0 = 0
    for gid, gedge in enumerate(red.edges):
        sl, cnt = model.edge_param_slices[gid]
        theta = phys_theta[sl:sl + cnt]
        names = par_names[gedge.kind]
        wps = tuple(nm for j, nm in enumerate(names) if weak[sl + j])
        abp = tuple(nm for j, nm in enumerate(names) if at_bnd[sl + j])
        if gedge.kind == "R":
            val = ("R", float(10.0 ** theta[0]))
        elif gedge.kind == "C":
            val = ("C", float(10.0 ** theta[0]))
        else:
            val = ("L", float(10.0 ** theta[0]), float(10.0 ** theta[1]))
        mode = gedge.expr[0] if gedge.expr[0] != "e" else "single"
        groups.append(GroupReport(gid=gid, kind=gedge.kind, u=gedge.u, v=gedge.v,
                                  members=gedge.members, mode=mode, value=val,
                                  weak_params=wps, at_bound=abp))
        status = "fitted" if len(gedge.members) == 1 else "merged"
        note = "" if status == "fitted" else (
            "{}-merged group g{}: only the aggregate is identifiable".format(mode, gid))
        for m in gedge.members:
            edges_out.append(EdgeReport(index=m, kind=edges[m][2], status=status,
                                        group=gid, value=val, note=note))
    for m, why in sorted(red.dropped.items()):
        edges_out.append(EdgeReport(index=m, kind=edges[m][2], status="dropped",
                                    group=-1, value=(), note=why))
    edges_out.sort(key=lambda e: e.index)
    return groups, edges_out


def identify(f, z, edges: list, config: FitConfig | None = None) -> FitResult:
    """Public entry: fit one known topology."""
    return fit_graph(f, z, edges, config)


def identify_many(f, z, graphs: list, config: FitConfig | None = None) -> list:
    """Fit several candidate topologies and rank them by AICc (ascending).

    Returns the list sorted best-first; each element is a FitResult.
    Port-open graphs raise PortOpenError to the caller (they cannot model
    finite measurements and should not silently compete).
    """
    out = []
    for g in graphs:
        out.append(fit_graph(f, z, g, config))
    out.sort(key=lambda r: r.aicc_val)
    return out
