"""Engine B: SK-iteration rational fit + Foster I/II synthesis (DESIGN.md §6).

Pipeline (§6.1): Sanathanan–Koerner iteration on
    Z(s) ≈ N(s)/D(s),  N = Σ_{m=0}^{n+1} a_m φ_m,  D = Σ_{m=0}^{n} b_m φ_m,
    b_n = 1,  φ_m = (s/ω0)^m,  ω0 = geometric mean of the band (D7a),
with real coefficients (Re/Im stacking), unstable-pole flipping
(Re p → −Re p, the numerical projection of the PR constraint) and a fixed
number of iterations (default 15, inside the documented 10–20 range).
Model order is scanned 0..max_order and selected by AICc; poles with
negligible residues (near pole-zero cancellation) are dropped.

Note (deviation): the numerator degree is n+1 (not n as in the §6.1 sketch),
giving the 2n+2 continuous degrees of freedom of an order-n impedance (§2.1)
and letting the residue basis' e·s term (§6.1) actually be represented.

With poles fixed, residues are re-estimated by one linear weighted LS over
the basis {s, 1, 1/s, 1/(s-p_i)} (§6.1).  Foster I then maps the partial
fraction of Z to series sections and Foster II repeats the whole procedure
on Y = 1/Z for parallel branches (§6.2 mapping tables).  Non-positive
element values or complex pole pairs with c ≠ 0 are explicitly marked as
skipped (decision D8) — no negative-element pseudo-realizations are emitted.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from .circuits import (SER, PAR, DCR_MIN, Leaf, Tree, assemble, evaluate)
from .fit_engine_a import Candidate, aicc, fit_metrics, rss_of, residual_vector

# significance threshold for dropping numerically irrelevant model terms
TERM_DROP_REL = 1e-9
# a term is kept when its contribution exceeds the estimated noise floor by
# this factor somewhere in the band
TERM_SNR = 3.0
# significance threshold for pole-structure pruning info (F3)
POLE_SIG_REL = 1e-2
# pole snapping: |p| below this fraction of the band edge is a pole at 0
POLE_ZERO_REL = 0.05
# pole snapping: |p| beyond this multiple of the band edge is a pole at inf
POLE_INF_REL = 1e4
# D8 tolerance on the pair constant term: |c| <= C_PAIR_TOL * rho_r * omega_n
C_PAIR_TOL = 1e-6
# tank with parallel R above this multiple of max|Z| is treated as lossless
TANK_R_OPEN_REL = 1e2
# series-branch R below this fraction of min|Z| is treated as a short
BRANCH_R_SHORT_REL = 1e-2


@dataclass
class RationalModel:
    """Pole-residue model  Z(s) = e·s + d + k0/s + Σ ρ_i/(s - p_i)."""

    order: int
    omega0: float
    e: float
    d: float
    k0: float
    poles: np.ndarray      # finite, nonzero, significant poles
    residues: np.ndarray   # complex residues aligned with `poles`
    rss: float
    aicc: float
    n_unknowns: int
    # selection AICc (with the machine-precision RSS floor applied) and the
    # full per-order scan, filled in by sk_rational_fit (decision D10)
    sel_aicc: float = np.inf
    alternatives: list = field(default_factory=list)

    def z_fit(self, s: np.ndarray) -> np.ndarray:
        out = self.e * s + self.d + self.k0 / s
        for p, r in zip(self.poles, self.residues):
            out = out + r / (s - p)
        return out

    # -- pole-structure summary used by pruning (F3) ------------------------
    def pole_structure(self, s: np.ndarray) -> tuple[int, int, int]:
        """(degree, n_complex_pairs, n_real_poles) counting only poles whose
        in-band contribution exceeds POLE_SIG_REL of max|Z| (noise-robust)."""
        zmax = float(np.max(np.abs(self.z_fit(s))))
        n_real = 0
        n_pair = 0
        seen: set[int] = set()
        for i, p in enumerate(self.poles):
            if i in seen:
                continue
            contrib = float(np.max(np.abs(self.residues[i] / (s - p))))
            if abs(p.imag) > 1e-9 * max(1.0, abs(p)):
                # find the conjugate partner to count the pair once
                for j in range(i + 1, len(self.poles)):
                    if abs(self.poles[j] - p.conjugate()) < 1e-6 * abs(p):
                        seen.add(j)
                        contrib = max(contrib, float(
                            np.max(np.abs(self.residues[j] / (s - self.poles[j])))))
                        break
                if contrib > POLE_SIG_REL * zmax:
                    n_pair += 1
            elif contrib > POLE_SIG_REL * zmax:
                n_real += 1
        degree = n_real + 2 * n_pair
        degree += int(abs(self.k0) > 0) + int(abs(self.e) > 0)
        return degree, n_pair, n_real


# ---------------------------------------------------------------------------
# SK iteration
# ---------------------------------------------------------------------------

def _lstsq_scaled(A: np.ndarray, y: np.ndarray) -> np.ndarray:
    """Least squares with column scaling (conditioning, D7a complement)."""
    scale = np.linalg.norm(A, axis=0)
    scale[scale == 0] = 1.0
    sol = np.linalg.lstsq(A / scale, y, rcond=None)[0]
    return sol / scale


def _feature_centers_x(x: np.ndarray, z: np.ndarray,
                       max_centers: int = 4) -> list[float]:
    """Data-driven resonance locations (in x units): interior |Z| extrema
    with prominence -- these are where the poles must live."""
    mag = np.abs(z)
    ax = np.abs(x)
    centers: list[float] = []
    for i in range(1, len(mag) - 1):
        lo = min(mag[i - 1], mag[i + 1])
        hi = max(mag[i - 1], mag[i + 1])
        if mag[i] > 1.25 * hi or mag[i] < 0.8 * lo:
            centers.append(float(ax[i]))
    # strongest extrema first, deduplicated in log space
    centers.sort(key=lambda c: -abs(np.log10(mag[np.argmin(abs(ax - c))])))
    out: list[float] = []
    for c in centers:
        if all(abs(np.log10(c) - np.log10(o)) > 0.15 for o in out):
            out.append(c)
    return out[:max_centers]


def _initial_poles_x(n: int, xlo: float, xhi: float,
                     hints: list[float] | None = None) -> list[np.ndarray]:
    """Initial pole sets (normalized x = s/omega0 units) for the SK/VF
    iteration: vector-fitting style log-spaced, lightly damped pole sets
    covering the band, seeded at data-driven resonance locations."""
    xi = 0.1  # light relative damping of the starting poles
    n_pairs = n // 2
    mid = float(np.sqrt(xlo * xhi))

    # pool of candidate pair centers: resonance hints + coarse log grid
    pool = list(hints or [])
    for c in np.geomspace(xlo, xhi, 5)[1:-1]:
        pool.append(float(c))
    pool.append(mid)
    # dedupe in log space
    dedup: list[float] = []
    for c in sorted(pool):
        if not dedup or abs(np.log10(c) - np.log10(dedup[-1])) > 0.1:
            dedup.append(c)
    pool = dedup[:6]

    def pair_set(centers, extra_real):
        poles: list[complex] = []
        for c in centers:
            poles += [complex(-xi * c, c), complex(-xi * c, -c)]
        if extra_real is not None:
            poles.append(complex(-extra_real))
        return np.array(poles[:n], dtype=complex)

    inits: list[np.ndarray] = []
    extra = mid if n % 2 else None
    if n_pairs >= 1:
        from itertools import combinations

        for combo in combinations(pool, min(n_pairs, len(pool))):
            if len(combo) < n_pairs:
                combo = combo + (mid,) * (n_pairs - len(combo))
            inits.append(pair_set(combo, extra))
        if len(pool) < n_pairs:  # degenerate tiny pool
            inits.append(pair_set([mid] * n_pairs, extra))
    if n % 2 == 1 or not inits:
        # purely real log-spaced poles (covers odd orders / RC-RL cases)
        inits.append((-np.geomspace(xlo, xhi, n)).astype(complex))
    return inits


def _pole_basis(x: np.ndarray, poles: np.ndarray) -> tuple[list[np.ndarray], list, list]:
    """Real-coefficient partial-fraction basis for the given poles (x-domain).

    Returns (columns, reals, pairs): one column 1/(x-p) per real pole and two
    columns 2(x+alpha)/D2, -2 beta/D2 per conjugate pair, so real coefficients
    of these columns reproduce a conjugate-symmetric pole-residue expansion.
    """
    reals, pairs = _split_poles(poles)
    cols: list[np.ndarray] = []
    for p in reals:
        cols.append(1.0 / (x - p))
    for p in pairs:
        alpha, beta = -p.real, abs(p.imag)
        d2 = (x + alpha) ** 2 + beta**2
        cols.append(2.0 * (x + alpha) / d2)
        cols.append(-2.0 * beta / d2)
    return cols, reals, pairs


def _sigma_zeros(poles: np.ndarray, reals, pairs,
                 ctilde: np.ndarray) -> np.ndarray:
    """Zeros of sigma(x) = 1 + sum_j c~_j/(x - p_j), computed as the roots of
    prod(x-p) + sum_j c~_j prod_{i!=j}(x-p_i) (real coefficients by conjugate
    symmetry)."""
    poles_all: list[complex] = []
    c_all: list[complex] = []
    k = 0
    for p in reals:
        poles_all.append(complex(p))
        c_all.append(complex(ctilde[k]))
        k += 1
    for p in pairs:
        cr, ci = float(ctilde[k]), float(ctilde[k + 1])
        k += 2
        poles_all += [p, p.conjugate()]
        c_all += [complex(cr, ci), complex(cr, -ci)]
    num = np.poly(np.array(poles_all)).astype(complex)
    for j, cj in enumerate(c_all):
        lower = np.atleast_1d(np.poly(np.delete(np.array(poles_all), j)))
        num += cj * np.pad(lower, (len(num) - len(lower), 0))
    return np.roots(num.real)


def _vf_step(x: np.ndarray, z: np.ndarray, wgt: np.ndarray,
             poles: np.ndarray) -> np.ndarray:
    """One SK iteration in pole basis (= vector-fitting pole relocation):
    solve min |sigma*z - (e x + d + sum r B)|^2 with fixed weights,
    sigma = 1 + sum c~ B, then relocate poles to the zeros of sigma.
    (DESIGN.md section 6.1 notes vector fitting is the pole-relocation
    variant of the SK iteration.)"""
    cols_p, reals, pairs = _pole_basis(x, poles)
    cols = [x, np.ones_like(x)] + cols_p + [-z * b for b in cols_p]
    G = np.column_stack(cols)
    A = np.vstack([(wgt[:, None] * G).real, (wgt[:, None] * G).imag])
    yv = np.concatenate([(wgt * z).real, (wgt * z).imag])
    sol = _lstsq_scaled(A, yv)
    n_b = len(cols_p)
    ctilde = sol[2 + n_b:2 + 2 * n_b]
    new_poles = _sigma_zeros(poles, reals, pairs, ctilde)
    flipped = np.array([complex(-p.real, p.imag) if p.real > 0 else p
                        for p in new_poles])
    return flipped


def _quick_score(x: np.ndarray, z: np.ndarray, w: np.ndarray,
                 poles: np.ndarray) -> float:
    """True weighted RSS of the best e*x + d + pole-residue model at the
    given poles (honest convergence metric for the relocation loop)."""
    cols_p, _, _ = _pole_basis(x, poles)
    cols = [x, np.ones_like(x)] + cols_p
    G = np.column_stack(cols)
    A = np.vstack([(w[:, None] * G).real, (w[:, None] * G).imag])
    yv = np.concatenate([(w * z).real, (w * z).imag])
    sol = _lstsq_scaled(A, yv)
    r = yv - A @ sol
    return float(r @ r)


def _sk_fit_order(s: np.ndarray, z: np.ndarray, w: np.ndarray, n: int,
                  omega0: float, n_iters: int) -> np.ndarray:
    """SK iteration at fixed order n; returns the fitted poles (s-domain).

    Pole-basis (vector-fitting) relocation variant of the Sanathanan-Koerner
    iteration -- same linearized least-squares family as the plain polynomial
    SK step of DESIGN.md section 6.1, but numerically robust over the wide
    (6-decade) band where the monomial basis is Vandermonde-ill-conditioned.
    Multiple pole initializations are tried; every iteration is scored by the
    true weighted residual and the best pole set is kept.
    """
    if n == 0:
        return np.empty(0, dtype=complex)
    x = s / omega0
    xlo = float(np.min(np.abs(x)))
    xhi = float(np.max(np.abs(x)))
    hints = _feature_centers_x(x, z)
    best: np.ndarray | None = None
    best_rss = np.inf
    for poles in _initial_poles_x(n, xlo, xhi, hints):
        for _ in range(max(n_iters, 1)):
            poles = _vf_step(x, z, w, poles)
            score = _quick_score(x, z, w, poles)
            if score < best_rss:
                best_rss, best = score, poles.copy()
    assert best is not None
    return omega0 * best


def _split_poles(poles: np.ndarray,
                 residues: np.ndarray | None = None):
    """Split into real poles and conjugate pairs (representative Im > 0).

    When ``residues`` is given, each entry carries its residue: reals are
    ``(p, rho)`` with both real, pairs are ``(p, rho)`` at the representative
    pole with Im p > 0.
    """
    order = sorted(range(len(poles)),
                   key=lambda i: (poles[i].real, abs(poles[i].imag)))
    used = [False] * len(poles)
    reals: list = []
    pairs: list = []

    def pack(p, r):
        return (p, r) if residues is not None else p

    for i in order:
        if used[i]:
            continue
        p = poles[i]
        ri = residues[i] if residues is not None else None
        if abs(p.imag) <= 1e-9 * max(1.0, abs(p)):
            reals.append(pack(float(p.real),
                              None if ri is None else float(ri.real)))
            used[i] = True
            continue
        best_j, best_d = None, np.inf
        for j in range(len(poles)):
            if j == i or used[j]:
                continue
            dd = abs(poles[j] - p.conjugate())
            if dd < best_d:
                best_j, best_d = j, dd
        if best_j is None or best_d > 1e-6 * abs(p):
            reals.append(pack(float(p.real),  # unpaired: keep real part
                              None if ri is None else float(ri.real)))
            used[i] = True
            continue
        used[i] = used[best_j] = True
        if p.imag < 0:  # representative has Im > 0
            p, ri = p.conjugate(), (ri.conjugate() if ri is not None else None)
        pairs.append(pack(complex(p.real, abs(p.imag)), ri))
    return reals, pairs


def _refit_residues(s: np.ndarray, z: np.ndarray, w: np.ndarray,
                    poles: np.ndarray, order: int, omega0: float,
                    w_band: tuple[float, float]) -> RationalModel:
    """Linear weighted re-estimation of e, d, k0 and residues (poles fixed)."""
    wmin, wmax = w_band
    # snap band-exterior poles: near-zero -> absorbed by the 1/s basis,
    # far-out -> absorbed by the constant basis (their in-band effect is
    # indistinguishable, §2.2b); keeps the basis well conditioned.
    kept = [p for p in poles
            if abs(p) >= POLE_ZERO_REL * wmin and abs(p) <= POLE_INF_REL * wmax]
    reals, pairs = _split_poles(np.array(kept, dtype=complex))

    cols: list[np.ndarray] = [s, np.ones_like(s), 1.0 / s]
    for p in reals:
        cols.append(1.0 / (s - p))
    for p in pairs:
        # pair rho/(s-p) + conj(rho)/(s-conj(p)) with rho = rho_r + j*rho_i
        # = rho_r * 2*(s+alpha)/D2 + rho_i * (-2*beta)/D2, D2 = (s+alpha)^2+beta^2
        # (real-coefficient rational basis functions, complex-valued on jw)
        alpha, beta = -p.real, abs(p.imag)
        d2 = (s + alpha) ** 2 + beta**2
        cols.append(2.0 * (s + alpha) / d2)   # coefficient rho_r
        cols.append(-2.0 * beta / d2)         # coefficient rho_i
    G = np.column_stack(cols)
    A = np.vstack([(w[:, None] * G).real, (w[:, None] * G).imag])
    yv = np.concatenate([(w * z).real, (w * z).imag])
    sol = _lstsq_scaled(A, yv)

    e, d, k0 = float(sol[0]), float(sol[1]), float(sol[2])
    idx = 3
    pole_list: list[complex] = []
    res_list: list[complex] = []
    for p in reals:
        pole_list.append(complex(p))
        res_list.append(complex(float(sol[idx])))
        idx += 1
    for p in pairs:
        rr, ri = float(sol[idx]), float(sol[idx + 1])
        idx += 2
        pole_list += [p, p.conjugate()]
        res_list += [complex(rr, ri), complex(rr, -ri)]

    # drop insignificant terms (near pole-zero cancellation, §6.1).
    # Threshold = max(absolute numerical floor, noise floor): the noise floor
    # is estimated from the fit's own weighted residuals, so noise-level
    # spurious terms (e.g. e ~ -1e-10 from 0.5% noise) never reach Foster.
    zfit0 = (e * s + d + k0 / s
             + sum(r / (s - p) for p, r in zip(pole_list, res_list)))
    zmax = float(np.max(np.abs(zfit0)))
    rss_full = float(np.sum(np.abs(w * (z - zfit0)) ** 2))
    sigma_hat = float(np.sqrt(rss_full / max(2 * len(z) - len(sol), 1)))
    noise_floor = TERM_SNR * sigma_hat / np.maximum(w, 1e-300)  # per-point |Z|
    zfit0_abs = np.abs(zfit0)
    term_arrays = [np.abs(e * s), np.full_like(zfit0_abs, abs(d)),
                   np.abs(k0 / s)]
    for p, r in zip(pole_list, res_list):
        term_arrays.append(np.abs(r / (s - p)))

    def significant(i: int) -> bool:
        t = term_arrays[i]
        if zmax > 0 and float(np.max(t)) < TERM_DROP_REL * zmax:
            return False
        # significant anywhere above the (estimated) noise floor
        return bool(np.any(t > noise_floor))

    if not significant(0):
        e = 0.0
    if not significant(1):
        d = 0.0
    if not significant(2):
        k0 = 0.0
    keep_poles, keep_res = [], []
    for k, (p, r) in enumerate(zip(pole_list, res_list)):
        if not significant(3 + k):
            continue
        keep_poles.append(p)
        keep_res.append(r)

    n_kept = (int(e != 0.0) + int(d != 0.0) + int(k0 != 0.0)
              + len(keep_poles))
    model = RationalModel(order=order, omega0=omega0, e=e, d=d, k0=k0,
                          poles=np.array(keep_poles, dtype=complex),
                          residues=np.array(keep_res, dtype=complex),
                          rss=0.0, aicc=0.0, n_unknowns=n_kept)
    r = w * (z - model.z_fit(s))
    rss = float(r.real @ r.real + r.imag @ r.imag)
    model.rss = rss
    model.aicc = aicc(rss, 2 * len(z), model.n_unknowns)
    return model


def sk_rational_fit(w: np.ndarray, z: np.ndarray, wts: np.ndarray,
                    max_order: int = 4, n_iters: int = 15) -> RationalModel:
    """Scan orders 0..max_order, SK-iterate each, select by AICc (§6.1).

    Parsimony tie-breaker (decision D10): when order n+1 drops the RSS by
    less than 10% compared to order n, the lower-order model is preferred
    (an overfitting penalty against noise-floor chase).  All order models are
    attached to ``best.alternatives`` for downstream Foster exploration.
    """
    s = 1j * np.asarray(w, dtype=float)
    omega0 = float(np.exp(np.mean(np.log(s.imag))))
    band = (float(s.imag.min()), float(s.imag.max()))
    # RSS floor at relative machine precision: below it, AICc differences are
    # numerical noise and the parsimony penalty must dominate.
    rss_floor = (1e-13) ** 2 * float(np.sum(np.abs(wts * z) ** 2))
    models: list[RationalModel] = []
    for n in range(0, max_order + 1):
        poles = _sk_fit_order(s, z, wts, n, omega0, n_iters)
        model = _refit_residues(s, z, wts, poles, n, omega0, band)
        model.rss = max(model.rss, rss_floor)
        model.sel_aicc = aicc(model.rss, 2 * len(z), model.n_unknowns)
        models.append(model)

    # Order selection by the discrepancy principle (decision D10): the
    # per-dof RSS of an adequate model equals the noise floor; estimate the
    # floor from the best achievable fit and select the LOWEST order whose
    # per-dof RSS is within the chi^2 3-sigma fluctuation of it.  Pure AICc
    # minimization over-fits at low noise: with 2M=60 points the relative
    # RSS fluctuation is ~19% per sigma, larger than the AICc penalty of a
    # few extra parameters, so AICc alone occasionally chases noise.
    n_obs = 2 * len(z)
    per_dof = [m.rss / max(n_obs - m.n_unknowns, 1) for m in models]
    sigma2_hat = min(per_dof)
    margin = 3.0 * np.sqrt(2.0 / n_obs)
    threshold = sigma2_hat * (1.0 + margin)
    best = models[0]
    for m in models:  # ascending order: first adequate = lowest order
        if m.rss / max(n_obs - m.n_unknowns, 1) <= threshold:
            best = m
            break
    else:
        best = min(models, key=lambda m: m.sel_aicc)
    best.alternatives = models
    return best


def conservative_energy_bound(model: RationalModel, s: np.ndarray,
                              delta_aicc: float = 10.0) -> int:
    """Conservative lower bound on reactive element count (F3 pruning).

    Takes the *minimum* pole count among all rational models within
    ``delta_aicc`` of the best, so that a spurious extra pole (fitting
    noise) can never prune away the true low-order topology.  This is the
    key robustness property: F3 may only under-prune, never over-prune.
    """
    pool = model.alternatives if model.alternatives else [model]
    best_sel = min(m.sel_aicc for m in pool)
    bounds: list[int] = []
    for m in pool:
        if m.sel_aicc <= best_sel + delta_aicc:
            _, n_pair, n_real = m.pole_structure(s)
            bounds.append(n_real + 2 * n_pair)
    return min(bounds) if bounds else 0


# ---------------------------------------------------------------------------
# Foster synthesis (§6.2 mapping tables, decision D8)
# ---------------------------------------------------------------------------

def _foster_sections(model: RationalModel, s_data: np.ndarray,
                     z_data: np.ndarray, admittance: bool,
                     sigma_hat: float,
                     ) -> tuple[list[tuple[Tree, list[float]]] | None, list[str]]:
    """Map a pole-residue model to Foster sections (Z: series sections,
    Y: parallel branches).  Returns (sections, notes); sections is None when
    a term is not realizable (D8) and the whole candidate must be skipped.

    ``sigma_hat`` is the model's own relative noise-floor estimate; the D8
    c-test treats c as zero when it is within noise of zero (a genuinely
    large c — the Bott-Duffin case — still triggers the skip).
    """
    zmax = float(np.max(np.abs(z_data)))
    zmin = float(np.min(np.abs(z_data)))
    notes: list[str] = []
    sections: list[tuple[Tree, list[float]]] = []

    e, d = model.e, model.d
    if not admittance and e != 0.0 and d != 0.0 and e > 0.0 and d > 0.0:
        # Z = d + e*s is ONE real inductor device: the constant term is the
        # winding DC resistance (v2 R4 series absorption), not a separate
        # series resistor
        sections.append((Leaf("L"), [e, max(d, DCR_MIN)]))
        e = 0.0
        d = 0.0

    if e != 0.0:
        if e > 0:
            if admittance:  # e'*s in Y -> parallel C
                sections.append((Leaf("C"), [e]))
            else:           # e*s in Z -> series L (ideal: DCR at floor)
                sections.append((Leaf("L"), [e, DCR_MIN]))
        else:
            return None, [f"e={'admittance' if admittance else 'impedance'} "
                          f"term negative ({e:.3g}), skipped (D8)"]
    if model.k0 != 0.0:
        if model.k0 > 0:
            if admittance:  # k0'/s in Y -> parallel L (ideal: DCR at floor)
                sections.append((Leaf("L"), [1.0 / model.k0, DCR_MIN]))
            else:           # k0/s in Z -> series C
                sections.append((Leaf("C"), [1.0 / model.k0]))
        else:
            return None, [f"k0 term negative ({model.k0:.3g}), skipped (D8)"]
    if d != 0.0:
        if d > 0:
            if admittance:  # d' in Y -> parallel R = 1/d'
                sections.append((Leaf("R"), [1.0 / d]))
            else:
                sections.append((Leaf("R"), [d]))
        else:
            return None, [f"d term negative ({d:.3g}), skipped (D8)"]

    reals_with_r, pairs_with_r = _split_poles(model.poles, model.residues)

    for p, rho_val in reals_with_r:  # real pole p = -a
        a = -float(p)
        rho = float(rho_val)
        if a <= 0:
            return None, [f"real pole at +|a| (a={a:.3g}), skipped (D8)"]
        if admittance:
            # rho'/(s+a') in Y -> series (L + Rd) branch: L = 1/rho',
            # Rd = a'/rho'  (v2: ONE device, two parameters)
            if rho <= 0:
                return None, [f"Y real-pole residue {rho:.3g} <= 0, skipped (D8)"]
            lv, rv = 1.0 / rho, a / rho
            rv = max(rv, DCR_MIN)
            if rv < BRANCH_R_SHORT_REL * zmin:
                notes.append("branch DCR below band floor (negligible loss)")
            sections.append((Leaf("L"), [lv, rv]))
        else:
            # rho/(s+a) in Z -> series R||C section: C = 1/rho, R = rho/a
            if rho <= 0:
                return None, [f"Z real-pole residue {rho:.3g} <= 0, skipped (D8)"]
            cv, rv = 1.0 / rho, rho / a
            sections.append(assemble(PAR, [(Leaf("R"), [rv]),
                                           (Leaf("C"), [cv])]))

    for p, rho in pairs_with_r:  # conjugate pair -alpha +/- j beta
        alpha, beta = p.real, abs(p.imag)
        if alpha > 0:
            return None, [f"unstable pole pair (alpha={alpha:.3g}), skipped (D8)"]
        alpha = -alpha  # damping >= 0 after flipping
        rho_r, rho_i = float(rho.real), float(rho.imag)
        om = float(np.hypot(alpha, beta))
        if rho_r <= 0:
            return None, [f"pair residue rho_r={rho_r:.3g} <= 0, skipped (D8)"]
        # D8: two realizable section families exist for a conjugate pair:
        #   (i)  lossless-tank family, c == 0:
        #          Z side: R || L || C          (parallel R carries damping)
        #          Y side: series (Rd + sL) + C (branch Rd carries damping)
        #   (ii) lossy-tank family, c == 2*alpha*rho_r (Z side only):
        #          (Rd + sL) || C -- the v2 signature of a REAL inductor in
        #          the tank; two devices, closed form C = 1/(2 rho_r),
        #          L = 2 rho_r/om^2, Rd = 4*alpha*rho_r/om^2.
        # Anything else needs Bott-Duffin / bridge synthesis (skip).  Each
        # family matches when the mismatch is within the noise-aware c_tol.
        c_const = rho_r * alpha - rho_i * beta
        c_lossy = 2.0 * alpha * rho_r
        c_tol = max(C_PAIR_TOL, 3.0 * sigma_hat) * abs(rho_r) * om
        if admittance:
            if abs(c_const) > c_tol:
                return None, [f"Y complex pair c={c_const:.3g} != 0 "
                              "(needs Bott-Duffin/bridge), skipped (D8)"]
            # series (L + Rd) + C branch: L = 1/(2 rho_r'), C = 2 rho_r'/(a^2+b^2),
            # Rd = alpha'/rho_r'  (v2: R folds into the L device's DCR)
            lv = 1.0 / (2.0 * rho_r)
            cv = 2.0 * rho_r / om**2
            rv = max(alpha / rho_r, DCR_MIN)
            if rv < BRANCH_R_SHORT_REL * zmin:
                notes.append("branch DCR below band floor (negligible loss)")
            sections.append(assemble(SER, [(Leaf("L"), [lv, rv]),
                                           (Leaf("C"), [cv])]))
            continue
        # impedance side: pick whichever family c is closer to
        cv = 1.0 / (2.0 * rho_r)
        lv = 2.0 * rho_r / om**2
        if abs(c_const - c_lossy) < abs(c_const) and \
                abs(c_const - c_lossy) <= c_tol:
            # family (ii): lossy parallel tank (Rd + sL) || C
            rd = max(4.0 * alpha * rho_r / om**2, DCR_MIN)
            sections.append(assemble(PAR, [(Leaf("L"), [lv, rd]),
                                           (Leaf("C"), [cv])]))
            continue
        if abs(c_const) > c_tol:
            return None, [f"Z complex pair c={c_const:.3g} not realizable "
                          "(needs Bott-Duffin/bridge), skipped (D8)"]
        # family (i): parallel R||L||C tank, C = 1/(2 rho_r), L = 2 rho_r/(a^2+b^2),
        # R = rho_r/alpha  (L branch is ideal: DCR at floor)
        rv = np.inf if alpha == 0 else rho_r / alpha
        if rv > TANK_R_OPEN_REL * zmax:
            notes.append("tank parallel R above band ceiling, dropped "
                         "(lossless section)")
            sections.append(assemble(PAR, [(Leaf("L"), [lv, DCR_MIN]),
                                           (Leaf("C"), [cv])]))
        else:
            sections.append(assemble(PAR, [(Leaf("R"), [rv]),
                                           (Leaf("L"), [lv, DCR_MIN]),
                                           (Leaf("C"), [cv])]))
    return sections, notes


def _sections_to_candidate(sections: list[tuple[Tree, list[float]]],
                           root_kind: str, s: np.ndarray, z: np.ndarray,
                           w: np.ndarray, note: str) -> Candidate | None:
    if not sections:
        return None
    if len(sections) == 1:
        tree, values = sections[0]
    else:
        tree, values = assemble(root_kind, sections)
    theta = np.log10(np.asarray(values, dtype=float))
    rss = rss_of(residual_vector(tree, theta, s, z, w))
    zfit = evaluate(tree, theta, s)
    wrmse, emax = fit_metrics(z, zfit)
    return Candidate(tree=tree, theta=theta, rss=rss,
                     aicc_val=aicc(rss, 2 * len(z), len(theta)),
                     wrmse=wrmse, max_rel_err=emax, engine="B", note=note)


def foster_candidates(w: np.ndarray, z: np.ndarray, wts: np.ndarray,
                      max_order: int = 4, n_iters: int = 15,
                      ) -> tuple[list[Candidate], RationalModel, RationalModel]:
    """Foster I (on Z) and Foster II (on Y = 1/Z) closed-form candidates.

    Returns (candidates, z_model, y_model); skipped candidates are included
    with ``skipped=True`` and an explanatory note (decision D8).
    """
    s = 1j * np.asarray(w, dtype=float)
    z_model = sk_rational_fit(w, z, wts, max_order, n_iters)
    y_model = sk_rational_fit(w, 1.0 / z, wts * np.abs(z) ** 2,
                              max_order, n_iters)

    out: list[Candidate] = []
    for model, adm, root, name in (
            (z_model, False, SER, "Foster-I"),
            (y_model, True, PAR, "Foster-II")):
        sigma_hat = float(np.sqrt(model.rss / max(2 * len(z)
                                                  - model.n_unknowns, 1)))
        sections, notes = _foster_sections(model, s, z, admittance=adm,
                                           sigma_hat=sigma_hat)
        if sections is None:
            out.append(Candidate(tree=Leaf("R"), theta=np.array([0.0]),
                                 rss=np.inf, aicc_val=np.inf, wrmse=np.inf,
                                 max_rel_err=np.inf, engine="B",
                                 note=f"{name}: " + "; ".join(notes),
                                 skipped=True))
            continue
        cand = _sections_to_candidate(sections, root, s, z, wts, name)
        if cand is None:
            continue
        # D8 validation: if the synthesized circuit has poor fit to the data
        # (e.g. from dropping terms or from a complex pair with c != 0), mark
        # it as skipped rather than emitting an invalid realization.
        if cand.wrmse > 0.05:
            out.append(Candidate(tree=cand.tree, theta=cand.theta,
                                 rss=np.inf, aicc_val=np.inf, wrmse=cand.wrmse,
                                 max_rel_err=cand.max_rel_err, engine="B",
                                 note=f"{name}: synthesis mismatch (wRMSE={cand.wrmse:.2g}), skipped (D8)",
                                 skipped=True))
            continue
        if notes:
            cand.note = f"{name}: " + "; ".join(notes)
        out.append(cand)
    return out, z_model, y_model
