"""Vector Fitting (rational approximation) of Z(f) in the s-domain.

Model (pole-residue form):

    Z(s) ~= sum_k c_k / (s - a_k) + d + s*e        (s = j*2*pi*f)

The classical vector-fitting iteration (Gustavsen & Semlyen, 1999) is used:

1. pick starting poles (weakly damped conjugate pairs, log-spaced over the
   measured band);
2. solve the augmented linear LS problem

       sum c_k/(s-a_k) + d + s*e  ~=  (1 + sum c~_k/(s-a_k)) * Z(s)

   for unknowns (c, c~, d, e);
3. the *new* poles are the eigenvalues of  diag(a) - 1*c~^T  (the zeros of the
   weighting sigma(s) = 1 + sum c~_k/(s-a_k)), flipped into the left half-plane;
4. repeat a few times, then solve a final plain LS for the residues (c, d, e).

Order selection: the fit is run for every order N = 1..n_max conjugate pairs
and the winner is picked by AICc on the sigma-weighted residual, so the
circuit complexity is decided by the data instead of a hand-chosen topology.

Residual weighting: if per-point noise sigma(f) is available (from the
time-domain sine fits) it is used; otherwise a relative weighting
sigma_i = max(|Z_i|, floor) is applied. The reported chi2_red is with respect
to the same weighting, so it is comparable across models and orders.
"""
from __future__ import annotations
from dataclasses import dataclass
import numpy as np

#: order-selection threshold: a fit whose weighted chi2_red <= CHI2_ACCEPT is
#: "at the noise level"; the smallest such order is chosen (parsimony).
CHI2_ACCEPT = 4.0


@dataclass
class RationalFit:
    order: int                      # number of conjugate pole pairs (real poles count as pairs of 0.. see below)
    poles: list[complex]            # stable poles (Re < 0), conjugate-symmetric, plus optional real poles
    residues: list[complex]         # c_k matching poles; conjugate-symmetric / real for real poles
    d: float                        # constant term (ohm)
    e: float                        # s-coefficient (henry)
    n_poles: int                    # len(poles)
    n_real: int                     # number of negative-real poles
    chi2_red: float                 # reduced chi-square of weighted residual
    aicc: float                     # Akaike information criterion (small-sample corrected)
    rss: float                      # weighted residual sum of squares
    converged: bool
    z_fit: np.ndarray               # model Z at the measured frequencies
    freqs: np.ndarray               # measured frequencies (Hz)

    def evaluate(self, freqs) -> np.ndarray:
        """Evaluate the fitted rational Z at arbitrary frequencies (Hz)."""
        s = 1j * 2.0 * np.pi * np.asarray(freqs, dtype=float)
        Z = np.full_like(s, self.d, dtype=complex) + self.e * s
        for a, c in zip(self.poles, self.residues):
            Z = Z + c / (s - a)
        return Z

    def zeros(self) -> list[complex]:
        """Zeros of Z(s): roots of the numerator polynomial."""
        a = np.asarray(self.poles, dtype=complex)
        c = np.asarray(self.residues, dtype=complex)
        # numerator = sum_k c_k * prod_{j!=k}(s-a_j) + (d + s*e) * prod(s-a_j)
        # build via full polynomial coefficients: N(s) = (d + s e) * D(s) + sum_k c_k * D_k(s)
        n = a.size
        # D(s) = prod (s - a_j): coeffs via convolution
        D = np.array([1.0 + 0j])
        for aj in a:
            D = np.convolve(D, [1.0, -aj])
        N = np.convolve([self.e, self.d], D)  # (d + s e) * D  -> coeffs [e, d]
        for k in range(n):
            Dk = np.array([1.0 + 0j])
            for j in range(n):
                if j != k:
                    Dk = np.convolve(Dk, [1.0, -a[j]])
            # D_k has degree n-1 (n coeffs) while (es+d)*D has degree n+1
            # (n+2 coeffs): pad high-order zeros so the low-order terms align
            N = N + c[k] * np.concatenate([np.zeros(N.size - Dk.size), Dk])
        # trim leading near-zero coefficients
        while N.size > 1 and abs(N[0]) < 1e-300:
            N = N[1:]
        if N.size <= 1:
            return []
        return sorted(np.roots(N), key=lambda z: (round(z.imag, 12), z.real))

    def theory_grid(self, f_lo: float, f_hi: float, n: int = 200) -> dict:
        f = np.logspace(np.log10(max(f_lo, 1e-3)), np.log10(max(f_hi, f_lo * 10)), n)
        Zt = self.evaluate(f)
        return {
            "frequency": f.tolist(),
            "z_mag": np.abs(Zt).tolist(),
            "z_phase_deg": np.degrees(np.angle(Zt)).tolist(),
            "z_real": Zt.real.tolist(),
            "z_imag": Zt.imag.tolist(),
        }


# ---------------------------------------------------------------- helpers

def _starting_poles(n_pairs: int, w_lo: float, w_hi: float,
                    damping: float = 100.0, ext: float = 1.0) -> np.ndarray:
    """Log-spaced weakly damped conjugate pairs covering the band.

    ``damping`` sets the pole real part as -w/damping; ``ext`` widens the
    band by this factor on both sides (poles may sit slightly out of band).
    """
    poles = []
    imag = np.geomspace(w_lo / ext, w_hi / ext, n_pairs)
    for wm in imag:
        poles.append(-wm / damping + 1j * wm)
        poles.append(-wm / damping - 1j * wm)
    return np.array(poles, dtype=complex)


def _solve_ls(s: np.ndarray, Z: np.ndarray, poles: np.ndarray,
              sigma_w: np.ndarray):
    """One augmented VF linear solve.

    Unknowns x = [c (per pole), d, e, c~ (per pole)].
    Row:  sum c_k phi_k + d + s e - Z * sum c~_k phi_k = Z, weighted by sigma_w.
    Returns (c, d, e, c_tilde).
    """
    n_p = poles.size
    Phi = 1.0 / (s[:, None] - poles[None, :])        # (M, n_p)
    ones = np.ones((s.size, 1), dtype=complex)
    smat = s[:, None]
    A = np.hstack([Phi, ones, smat, -Z[:, None] * Phi])
    b = Z
    # row weighting by noise sigma, then column scaling for conditioning
    A = A / sigma_w[:, None]
    b = b / sigma_w
    scale = np.linalg.norm(A, axis=0)
    scale[scale == 0] = 1.0
    x, *_ = np.linalg.lstsq(A / scale, b, rcond=None)
    x = x / scale
    c = x[:n_p]
    d = x[n_p]
    e = x[n_p + 1]
    c_t = x[n_p + 2:]
    return c, complex(d), complex(e), c_t


def _relocate_poles(poles: np.ndarray, c_t: np.ndarray) -> np.ndarray:
    """New poles = eigenvalues of diag(a) - 1 * c~^T, stabilised to Re <= 0.

    No conjugate averaging here: relocation frequently pushes a conjugate pair
    onto two *distinct* real poles (RC-type behaviour), and averaging would
    collapse them onto their mean and corrupt the iteration. Exact conjugate
    symmetry is enforced once, on the final result, in :func:`_symmetrise`.
    """
    if np.all(np.abs(c_t) < 1e-12):
        return poles
    A_mat = np.diag(poles) - np.outer(np.ones(poles.size), c_t)
    new = np.linalg.eigvals(A_mat)
    # stabilise: flip right-half-plane poles (keeps |Im|, negates Re)
    new = np.where(new.real > 0, -new.real + 1j * new.imag, new)
    # deterministic order for reproducible iterations
    return np.array(sorted(new, key=lambda z: (z.imag, z.real)))


def _classify_poles(poles: np.ndarray):
    """Split relocation output into (real poles, conjugate-pair poles Im>0).

    A pole is treated as *real* when its imaginary part is negligible in both
    absolute terms and relative to its distance from the origin: VF frequently
    converges a conjugate pair onto two near-real poles (RC-type behaviour),
    and those must synthesise as parallel-RC branches, not as a resonant pair
    with beta ~ 0 (which yields absurd element values). Remaining complex
    poles are paired with their conjugate partner; a loner is force-paired
    with its own conjugate (residues are re-solved afterwards anyway).
    """
    scale = max(float(np.max(np.abs(poles))) if poles.size else 1.0, 1.0)
    real_poles, cplx = [], []
    for p in poles:
        if abs(p.imag) <= max(1e-9 * scale, 1e-3 * abs(p)):
            real_poles.append(p.real)
        else:
            cplx.append(p)
    # merge duplicate real poles (same corner frequency)
    merged: list[float] = []
    for p in sorted(real_poles):
        if merged and abs(p - merged[-1]) < 1e-9 * max(abs(p), 1.0):
            continue
        merged.append(p)

    pairs: list[complex] = []
    used = np.zeros(len(cplx), dtype=bool)
    for i, p in enumerate(cplx):
        if used[i]:
            continue
        used[i] = True
        best_j, best_d = None, np.inf
        for j in range(len(cplx)):
            if used[j]:
                continue
            dist = abs(np.conj(cplx[j]) - p)
            if dist < best_d:
                best_d, best_j = dist, j
        if best_j is not None and best_d < 1e-6 * max(abs(p), 1.0):
            used[best_j] = True
            pairs.append(0.5 * (p + np.conj(cplx[best_j])))
        else:
            pairs.append(p)      # force-pair with own conjugate
    # keep only Im>0 members; the conjugate is implied
    return merged, [p if p.imag > 0 else np.conj(p) for p in pairs]


def _final_solve_real(s: np.ndarray, Z: np.ndarray, sigma_w: np.ndarray,
                      real_poles: list[float], pairs: list[complex]):
    """Final residue solve with an exactly-real parameterisation.

    Unknowns (all real): per conjugate pair (Re c, Im c), per real pole c,
    plus d and e. This guarantees the reported model is a real-coefficient
    rational function -- d and e can never absorb spurious imaginary parts
    that break the delicate cancellation between near-real pole dipoles.
    """
    cols: list[np.ndarray] = []
    for a in pairs:
        phi = 1.0 / (s - a)
        phi_bar = 1.0 / (s - np.conj(a))
        cols.append(phi + phi_bar)          # d/dc.real
        cols.append(1j * (phi - phi_bar))   # d/dc.imag
    for p in real_poles:
        cols.append(1.0 / (s - p))
    cols.append(np.ones_like(s))            # d
    cols.append(s)                          # e

    A = np.column_stack(cols)
    A = np.vstack([A.real, A.imag]) / np.concatenate([sigma_w, sigma_w])[:, None]
    b = np.concatenate([(Z / sigma_w).real, (Z / sigma_w).imag])
    scale = np.linalg.norm(A, axis=0)
    scale[scale == 0] = 1.0
    x, *_ = np.linalg.lstsq(A / scale, b, rcond=None)
    x = x / scale

    pair_res, real_res = [], []
    idx = 0
    for _ in pairs:
        ur, ui = x[idx], x[idx + 1]
        pair_res.append(complex(ur, ui))
        idx += 2
    for _ in real_poles:
        real_res.append(float(x[idx]))
        idx += 1
    d = float(x[idx])
    e = float(x[idx + 1])

    # expand to full pole/residue lists (conjugates made explicit)
    poles_out: list[complex] = []
    resid_out: list[complex] = []
    for a, c in zip(pairs, pair_res):
        poles_out += [complex(a), complex(np.conj(a))]
        resid_out += [complex(c), complex(np.conj(c))]
    for p, c in zip(real_poles, real_res):
        poles_out.append(complex(p))
        resid_out.append(complex(c))

    Zm = d + e * s
    for a, ck in zip(poles_out, resid_out):
        Zm = Zm + ck / (s - a)
    return poles_out, resid_out, d, e, Zm


def _weighted_residual(Z: np.ndarray, Zm: np.ndarray, sigma: np.ndarray):
    diff = Zm - Z
    return np.concatenate([(diff.real / sigma), (diff.imag / sigma)])


def _pair_realizable(p: complex, c: complex) -> bool:
    """Foster-branch passivity, with a noise-aware tolerance.

    Strictly the branch needs u = Re(c) > 0 and 0 <= gamma < 2*alpha. But a
    *lossless* section (true gamma = 0, e.g. a parallel RLC) gets its gamma
    perturbed to either sign by fit noise, so instead of pruning on the sign
    of gamma we prune on the synthesised elements: a small negative series
    loss  |R_s| <= 5% of R_p  is within noise (R_s = gamma*A/denom,
    R_p = A/(2*alpha-gamma); A cancels in the comparison)."""
    alpha = -p.real
    beta = abs(p.imag)
    u, v = c.real, c.imag
    if u <= 0:
        return False
    gamma = alpha - (v / u) * beta
    if gamma < 0.0:
        # -R_s <= 0.05 * R_p   <=>   -gamma*(2*alpha - gamma) <= 0.05*denom
        denom = (alpha - gamma) ** 2 + beta * beta
        if -gamma * (2.0 * alpha - gamma) > 0.05 * denom:
            return False
    if gamma >= 2.0 * alpha:
        return False
    return True


def _polish(s: np.ndarray, Z: np.ndarray, sigma_w: np.ndarray,
            real_poles: list[float], pairs: list[complex]):
    """Jointly refine pole positions and residues with a small nonlinear LS.

    VF relocation converges to poles that can still be ~1% off; a fixed-pole
    residue solve cannot make up for that. Here every pair contributes 4 real
    unknowns (Re/Im of pole and residue), every non-origin real pole 2, plus
    d and e; the origin pole stays pinned at s = 0 (it is structural).

    Returns the same tuple as :func:`_final_solve_real`.
    """
    from scipy.optimize import least_squares
    _po, _ro, _d, _e, _Zm = _final_solve_real(s, Z, sigma_w, real_poles, pairs)
    # pull initial residues back out of the expanded representation
    pair_res = [_ro[i] for i, p in enumerate(_po) if p.imag > 0]
    real_res = [_ro[i] for i, p in enumerate(_po) if p.imag == 0]

    w_hi = float(np.max(np.abs(s)))
    tiny = 1e-12 * w_hi
    big = 1e9 * w_hi
    n_free_real = sum(1 for p in real_poles if p != 0.0)

    x0: list[float] = []
    lo: list[float] = []
    hi: list[float] = []
    for a, c in zip(pairs, pair_res):
        x0 += [float(a.real), float(a.imag), float(c.real), float(c.imag)]
        lo += [-big, tiny, -np.inf, -np.inf]
        hi += [-tiny, big, np.inf, np.inf]
    for p, c in zip(real_poles, real_res):
        if p == 0.0:
            x0.append(float(c.real))
            lo.append(-np.inf)
            hi.append(np.inf)
        else:
            x0 += [float(p), float(c.real)]
            lo += [-big, -np.inf]
            hi += [0.0, np.inf]
    x0 += [float(_d), float(_e)]
    lo += [-np.inf, -np.inf]
    hi += [np.inf, np.inf]
    lo_a = np.array(lo)
    hi_a = np.array(hi)
    x0 = np.clip(np.array(x0), lo_a, hi_a)
    i_d = len(x0) - 2
    i_e = len(x0) - 1
    n_pair = len(pairs)

    def model(x):
        Zm = x[i_d] + x[i_e] * s
        for k in range(n_pair):
            ar, ai, cr, ci = x[4 * k: 4 * k + 4]
            a = ar + 1j * ai
            c = cr + 1j * ci
            Zm = Zm + c / (s - a) + np.conj(c) / (s - np.conj(a))
        j = 4 * n_pair
        for p in real_poles:
            if p == 0.0:
                Zm = Zm + x[j] / s
                j += 1
            else:
                Zm = Zm + x[j + 1] / (s - x[j])
                j += 2
        return Zm

    def resid(x):
        dZ = model(x) - Z
        return np.concatenate([(dZ.real / sigma_w), (dZ.imag / sigma_w)])

    sol = least_squares(resid, x0, bounds=(np.array(lo), np.array(hi)),
                        x_scale="jac", max_nfev=100)
    Zm = model(sol.x)
    if not np.all(np.isfinite(Zm.view(float))):
        return _final_solve_real(s, Z, sigma_w, real_poles, pairs)

    poles_out: list[complex] = []
    resid_out: list[complex] = []
    x = sol.x
    for k in range(n_pair):
        a = x[4 * k] + 1j * x[4 * k + 1]
        c = x[4 * k + 2] + 1j * x[4 * k + 3]
        poles_out += [a, np.conj(a)]
        resid_out += [c, np.conj(c)]
    j = 4 * n_pair
    for p in real_poles:
        if p == 0.0:
            poles_out.append(0.0)
            resid_out.append(complex(x[j]))
            j += 1
        else:
            poles_out.append(complex(x[j]))
            resid_out.append(complex(x[j + 1]))
            j += 2
    return poles_out, resid_out, float(x[i_d]), float(x[i_e]), Zm


def _fit_one_order(freqs, Z, sigma, poles0, n_iter=5):
    """Full VF pipeline for one candidate order. Returns
    (poles, residues, d, e, Zm) or None on numerical failure.

    Pipeline: relocation iterations -> classify poles (real / conjugate
    pairs, plus a structural origin pole for series capacitance) -> nonlinear
    polish of pole positions + residues -> prune poles that cannot belong to
    a *passive* one-port (real poles with residue <= 0, conjugate pairs whose
    Foster branch would need negative elements) and repeat.
    """
    s = 1j * 2.0 * np.pi * freqs
    poles = poles0.copy()
    noise_rel = float(np.median(sigma / np.maximum(np.abs(Z), 1e-300)))
    accept_rel = max(3.0 * noise_rel, 1e-10)
    try:
        for _ in range(n_iter):
            _c, _d, _e, c_t = _solve_ls(s, Z, poles, sigma)
            poles = _relocate_poles(poles, c_t)
        real_poles, pairs = _classify_poles(poles)
        # always offer an origin pole (c0/s = series capacitor): out-of-band
        # resonance sections look like pure capacitance inside the band, and
        # without this term VF fakes them with non-passive out-of-band pairs
        real_poles.append(0.0)
        result = None
        for _cycle in range(min(len(pairs) + len(real_poles) + 1, 6)):
            # fast path: if the fixed-pole solve is already at the noise
            # level and passively realisable, skip the (costly) polish
            quick = _final_solve_real(s, Z, sigma, real_poles, pairs)
            q_po, q_ro, _qd, _qe, q_Zm = quick
            rel_med = float(np.median(np.abs(q_Zm - Z) / np.maximum(np.abs(Z), 1e-300)))
            ok_real = all(c.real > 0 for p, c in zip(q_po, q_ro) if p.imag == 0)
            ok_pair = all(_pair_realizable(p, c)
                          for p, c in zip(q_po, q_ro) if p.imag > 0)
            if rel_med < accept_rel and ok_real and ok_pair:
                return quick
            result = _polish(s, Z, sigma, real_poles, pairs)
            _po, _ro, _d, _e, _Zm = result
            bad_real = [i for i, c in enumerate(_ro)
                        if _po[i].imag == 0 and c.real <= 0]
            # conjugate members share realisability via the Im>0 member
            bad_pair = [i for i, c in enumerate(_ro)
                        if _po[i].imag > 0 and not _pair_realizable(_po[i], c)]
            if not bad_real and not bad_pair:
                return result
            # drop the worst passivity offender first
            worst = min(bad_real + bad_pair, key=lambda i: _ro[i].real)
            if _po[worst].imag == 0:
                real_pos = sum(1 for p in _po[:worst] if p.imag == 0)
                del real_poles[real_pos]
            else:
                pair_pos = sum(1 for p in _po[:worst] if p.imag != 0) // 2
                del pairs[pair_pos]
                if not pairs and not real_poles:
                    return result
        return result
    except np.linalg.LinAlgError:
        return None


def vector_fit(freqs, Z, sigma=None, n_max: int = 6, n_iter: int = 5):
    """Fit a rational pole-residue model to Z(f), trying orders 1..n_max.

    Order selection: the smallest order whose chi2_red <= CHI2_ACCEPT
    (residuals already at the noise level) wins; fall back to best AICc.
    ``sigma`` is optional per-point 1-sigma noise on Z; if None a relative
    weighting |Z|*1e-3 is used.
    """
    freqs = np.asarray(freqs, dtype=float)
    Z = np.asarray(Z, dtype=complex)
    M = Z.size
    if M < 4:
        raise ValueError("need at least 4 frequency points for a rational fit")
    if sigma is None:
        sigma = np.abs(Z) * 1e-3
    else:
        sigma = np.asarray(sigma, dtype=float)
        # noise floor: below ~100 ppm relative the "noise" is numerical
        # residue, not physics; taking it literally makes chi2 meaningless
        sigma = np.maximum(sigma, np.abs(Z) * 1e-5)
    sigma = np.maximum(sigma, 1e-30)

    w = 2.0 * np.pi * freqs
    w_lo, w_hi = float(w.min()), float(w.max())

    # several starting-pole layouts per order; VF relocation is a fixed-point
    # iteration and mildly sensitive to the start, so keep the best run
    starts = ((100.0, 1.0), (10.0, 1.0))
    fits = []
    for n_pairs in range(1, n_max + 1):
        best_res = None
        best_rss = np.inf
        for damp, ext in starts:
            poles0 = _starting_poles(n_pairs, w_lo, w_hi, damp, ext)
            res = _fit_one_order(freqs, Z, sigma, poles0, n_iter=n_iter)
            if res is None:
                continue
            _p, _c, _d, _e, Zm = res
            if not np.all(np.isfinite(Zm.view(float))):
                continue
            rss = float(np.sum(_weighted_residual(Z, Zm, sigma) ** 2))
            if np.isfinite(rss) and rss < best_rss:
                best_rss, best_res = rss, res
        if best_res is None:
            continue
        poles_s, c_s, d, e, Zm = best_res
        rss = best_rss if best_rss < np.inf else np.finfo(float).tiny
        n_par = 4 * n_pairs + 3                  # complex c, a per pair + c0, d, e
        n = 2 * M
        k = min(n_par, n - 1)
        if rss <= 0:
            rss = np.finfo(float).tiny
        aicc = n * np.log(rss / n) + 2 * k + 2 * k * (k + 1) / max(n - k - 1, 1)
        chi2_red = rss / max(n - k, 1)
        n_real = sum(1 for p in poles_s if p.imag == 0)
        fit = RationalFit(
            order=n_pairs, poles=[complex(p) for p in poles_s],
            residues=[complex(c) for c in c_s], d=d, e=e,
            n_poles=len(poles_s), n_real=n_real,
            chi2_red=float(chi2_red), aicc=float(aicc), rss=rss,
            converged=True, z_fit=Zm, freqs=freqs,
        )
        fits.append(fit)
        # early exit: residuals already at the noise level -> higher orders
        # can only overfit (and cost seconds of polish time); the selection
        # rule below would pick this smallest acceptable order anyway
        if fit.chi2_red <= CHI2_ACCEPT:
            return fit
    if not fits:
        raise RuntimeError("vector fitting failed for all orders")

    # order selection fallback (nothing reached the noise level): best AICc
    return min(fits, key=lambda f: f.aicc)
