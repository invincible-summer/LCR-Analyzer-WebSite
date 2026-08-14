"""Foster-style RLC network synthesis from a fitted rational Z(s).

Given the pole-residue model

    Z(s) = sum_k c_k/(s - a_k) + d + s*e

we synthesise a realisable passive one-port:

* constant ``d``          -> series resistor R = d
* ``s*e``                 -> series inductor  L = e
* real pole  a = -p       -> term c/(s+p) is exactly a parallel RC:
                             C = 1/c, R = c/p                     (c > 0)
* conjugate pair a = -alpha + j*beta, residue c = u + j*v:
  the pair sums to

      Z_pair(s) = (A s + B) / (s^2 + 2 alpha s + Om^2),
      A = 2u,  B = 2(u*alpha - v*beta),  Om^2 = alpha^2 + beta^2.

  Its admittance divides as

      Y = 1/A * [ s + (2*alpha - gamma) + ((alpha-gamma)^2+beta^2)/(s+gamma) ],
      gamma = B/A,

  i.e. a parallel combination of
      C_p  = 1/A
      R_p  = A/(2*alpha - gamma)                       (needs 0 < gamma < 2*alpha)
      series RL: L_s = A/((alpha-gamma)^2+beta^2),
                 R_s = gamma*A/((alpha-gamma)^2+beta^2) (needs gamma > 0)

The result is a JSON-friendly netlist *tree* that the frontend renders as a
schematic directly:

    {"type": "series", "children": [
        {"type": "R", "R": 12.3},
        {"type": "L", "L": 1.2e-6},
        {"type": "parallel", "children": [
            {"type": "C", "C": ...}, {"type": "R", "R": ...},
            {"type": "series", "children": [{"type":"R",...},{"type":"L",...}]}
        ]},
    ]}

A dense-grid Re(Z) >= 0 check reports passivity violations.
"""
from __future__ import annotations
from dataclasses import dataclass, field
import numpy as np

from .rational_fit import RationalFit


@dataclass
class SynthesisResult:
    netlist: dict                     # tree, root is {"type": "series", ...}
    warnings: list[str] = field(default_factory=list)
    passive: bool = True

    def spice(self, name: str = "DUT") -> str:
        """Render the netlist as a SPICE ``.subckt`` (linear R/L/C elements)."""
        lines = [f".subckt {name} 1 0"]
        counter = {"n": 1, "e": 0}

        def node():
            counter["n"] += 1
            return str(counter["n"])

        def emit(el, a, b):
            t = el["type"]
            if t in ("R", "L", "C"):
                counter["e"] += 1
                lines.append(f"{t}{counter['e']} {a} {b} {el[t]:.8g}")
            elif t == "series":
                children = el["children"]
                if not children:
                    counter["e"] += 1
                    lines.append(f"R{counter['e']} {a} {b} 1e-9")
                    return
                prev = a
                for j, child in enumerate(children):
                    nxt = b if j == len(children) - 1 else node()
                    emit(child, prev, nxt)
                    prev = nxt
            elif t == "parallel":
                for child in el["children"]:
                    emit(child, a, b)
            else:
                raise ValueError(f"unknown element type {t!r}")

        emit(self.netlist, "1", "0")
        lines.append(".ends")
        return "\n".join(lines) + "\n"


def synthesise(rfit: RationalFit, f_lo: float, f_hi: float,
               n_grid: int = 2001) -> SynthesisResult:
    """Build the RLC netlist tree from a :class:`RationalFit`."""
    warnings: list[str] = []
    children: list[dict] = []

    zref = float(np.median(np.abs(rfit.z_fit))) if rfit.z_fit.size else 1.0

    if abs(rfit.e) > 1e-3 * max(zref / max(f_lo, 1e-3), 1e-30):
        if rfit.e < 0:
            warnings.append(f"s-coefficient e = {rfit.e:.3g} < 0: negative series inductance")
        children.append({"type": "L", "L": float(rfit.e)})

    # separate conjugate pairs and real poles
    pairs, reals = [], []
    seen: set[int] = set()
    plist = rfit.poles
    for i, p in enumerate(plist):
        if i in seen:
            continue
        if p.imag == 0:
            reals.append((p.real, rfit.residues[i].real))
            seen.add(i)
        else:
            # locate conjugate partner
            j = None
            for jj in range(i + 1, len(plist)):
                if jj in seen:
                    continue
                if abs(np.conj(plist[jj]) - p) < 1e-9 * max(abs(p), 1.0):
                    j = jj
                    break
            if j is None:
                reals.append((p.real, rfit.residues[i].real))
                seen.add(i)
            else:
                seen.add(j)
                pairs.append((p, rfit.residues[i]))

    for p_real, c_real in reals:
        a = float(p_real)             # negative real pole, or 0 (origin)
        c = float(c_real)
        if a == 0.0:
            # origin pole: c0/s is a series capacitor
            if c <= 0:
                warnings.append(f"origin pole residue {c:.3g} <= 0: series capacitor branch skipped")
                continue
            children.append({"type": "C", "C": 1.0 / c})
            continue
        if a >= 0:
            warnings.append(f"unstable real pole {a:.4g} ignored")
            continue
        if c <= 0:
            warnings.append(f"real pole at {a:.4g} has negative residue ({c:.3g}): branch not synthesised")
            continue
        C = 1.0 / c
        R = c / (-a)
        children.append({"type": "parallel", "children": [
            {"type": "R", "R": R}, {"type": "C", "C": C},
        ]})

    for p, c in pairs:
        alpha = -p.real
        beta = abs(p.imag)
        u, v = c.real, c.imag
        A = 2.0 * u
        if A <= 0:
            warnings.append(
                f"pole pair at {p:.4g}: residue real part u = {u:.3g} <= 0, "
                "branch not synthesisable as a passive RLC network")
            continue
        B = 2.0 * (u * alpha - v * beta)
        gamma = B / A
        Om2 = alpha * alpha + beta * beta
        Cp = 1.0 / A
        denom = (alpha - gamma) ** 2 + beta * beta
        Ls = A / denom
        branch: list[dict] = [{"type": "C", "C": Cp}]
        g_eps = 1e-6 * (alpha + beta)
        if gamma > g_eps:
            branch.append({"type": "series", "children": [
                {"type": "R", "R": gamma * A / denom}, {"type": "L", "L": Ls},
            ]})
        else:
            # gamma ~= 0 (or numerically negative): pure inductor. A
            # *parallel RLC* section lands exactly here (B = 0, R_s = 0).
            # Matches the pruning rule in rational_fit._pair_realizable:
            # |R_s| <= 5% of R_p is fit noise, only warn beyond that.
            if gamma < 0.0 and -gamma * (2.0 * alpha - gamma) > 0.05 * denom:
                warnings.append(
                    f"pole pair at {p:.4g}: gamma = {gamma:.3g} < 0, series loss "
                    "resistor dropped (branch is not strictly passive)")
            branch.append({"type": "L", "L": Ls})
        if 2.0 * alpha - gamma > 1e-15:
            Rp = A / (2.0 * alpha - gamma)
            branch.append({"type": "R", "R": Rp})
        else:
            warnings.append(f"pole pair at {p:.4g}: 2*alpha - gamma <= 0, parallel resistor omitted (active branch)")
        children.append({"type": "parallel", "children": branch})

    if abs(rfit.d) > 1e-3 * zref:
        if rfit.d < 0:
            warnings.append(f"constant term d = {rfit.d:.3g} < 0: negative series resistance")
        children.append({"type": "R", "R": float(rfit.d)})

    netlist = {"type": "series", "children": children}

    # passivity check on a dense grid
    f = np.logspace(np.log10(max(f_lo, 1e-3)), np.log10(max(f_hi, f_lo * 10)), n_grid)
    Zg = rfit.evaluate(f)
    min_re = float(np.min(Zg.real)) if Zg.size else 0.0
    if min_re < -1e-9 * max(float(np.max(np.abs(Zg))) if Zg.size else 1.0, 1.0):
        passive = False
        warnings.append(f"fitted model is non-passive (min Re(Z) = {min_re:.4g} Ω); treat results with caution")
    else:
        passive = True

    return SynthesisResult(netlist=netlist, warnings=warnings, passive=passive)
