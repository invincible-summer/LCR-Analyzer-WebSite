"""Known component multiset (Try2 problem input, DESIGN.md section 1).

Each component is a 2-terminal edge:
  * resistor: ideal, one parameter R > 0;
  * capacitor: ideal, one parameter C > 0;
  * inductor: ideal L > 0 in series with an ideal DC resistance R_dc >= 0
    (two parameters; R_dc = 0 gives a purely ideal inductor).

Components with identical kind and identical parameters are
interchangeable (indistinguishable); the enumeration must not count their
permutations as distinct networks.
"""

from __future__ import annotations

from dataclasses import dataclass
from functools import total_ordering

import numpy as np

KINDS = ("R", "C", "L")


@total_ordering
@dataclass(frozen=True)
class Component:
    """One known component.  kind in {"R","C","L"}.

    value is R (ohm) for resistors, C (farad) for capacitors and L (henry)
    for inductors; inductors additionally carry dcr = series DC resistance.
    """

    kind: str
    value: float
    dcr: float = 0.0

    def __post_init__(self) -> None:
        if self.kind not in KINDS:
            raise ValueError(f"kind must be one of {KINDS}, got {self.kind!r}")
        if not (self.value > 0.0):
            raise ValueError("value must be positive")
        if self.dcr < 0.0:
            raise ValueError("dcr must be >= 0")

    @property
    def is_ideal_inductor(self) -> bool:
        """True for an inductor with zero DC resistance (a short at DC)."""
        return self.kind == "L" and self.dcr == 0.0

    def key(self) -> tuple:
        """Interchangeability key: components with equal keys may be swapped
        without changing the network."""
        return (self.kind, self.value, self.dcr)

    def __lt__(self, other: "Component") -> bool:
        return self.key() < other.key()

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Component) and self.key() == other.key()

    def __hash__(self) -> int:
        return hash(self.key())

    def label(self) -> str:
        """Compact human-readable label, e.g. 'R(1k)' or 'L(10mH+d5)'."""
        v = self.value
        if self.kind == "R":
            return f"R({_fmt(v)}ohm)"
        if self.kind == "C":
            return f"C({_fmt(v)}F)"
        if self.dcr == 0.0:
            return f"L({_fmt(v)}H)"
        return f"L({_fmt(v)}H+d{_fmt(self.dcr)})"


def _fmt(v: float) -> str:
    """Engineering formatting with 3 significant digits."""
    if v == 0:
        return "0"
    a = abs(v)
    for exp, prefix in ((9, "G"), (6, "M"), (3, "k"), (0, ""), (-3, "m"), (-6, "u"), (-9, "n"), (-12, "p")):
        if a >= 10.0**exp:
            return f"{v / 10.0**exp:.3g}{prefix}"
    return f"{v:.3g}"


@dataclass(frozen=True)
class ComponentSet:
    """The known multiset of components of the DUT (DESIGN.md section 1.1)."""

    components: tuple[Component, ...]

    def __post_init__(self) -> None:
        if len(self.components) == 0:
            raise ValueError("need at least one component")
        # canonical order: sorted by key (kind R<C<L, then values)
        object.__setattr__(self, "components", tuple(sorted(self.components)))

    # -- basic statistics ---------------------------------------------------
    @property
    def n(self) -> int:
        """Total number of components (edges) E."""
        return len(self.components)

    def count(self, kind: str) -> int:
        return sum(1 for c in self.components if c.kind == kind)

    @property
    def n_params(self) -> int:
        """Total scalar parameters: 1 per R/C, 2 per L.  Identical for every
        candidate topology, so AICc ordering degenerates to RSS ordering
        (DESIGN.md section 5)."""
        return sum(2 if c.kind == "L" else 1 for c in self.components)

    # -- interchangeability -------------------------------------------------
    def multiplicity_groups(self) -> list[tuple[Component, int]]:
        """[(component, count)] over the distinct interchangeable groups."""
        out: dict[Component, int] = {}
        for c in self.components:
            out[c] = out.get(c, 0) + 1
        return sorted(out.items())

    def distinguishable(self) -> bool:
        """True if all components are pairwise distinguishable."""
        return len(set(self.components)) == len(self.components)

    # -- construction helpers ----------------------------------------------
    @staticmethod
    def make(n_R: list[float] | None = None,
             n_C: list[float] | None = None,
             n_L: list[tuple[float, float]] | None = None) -> "ComponentSet":
        """Convenience constructor.

        n_R: list of resistances; n_C: list of capacitances;
        n_L: list of (L, dcr) pairs.
        """
        comps: list[Component] = []
        for r in (n_R or []):
            comps.append(Component("R", float(r)))
        for c in (n_C or []):
            comps.append(Component("C", float(c)))
        for l, d in (n_L or []):
            comps.append(Component("L", float(l), float(d)))
        return ComponentSet(tuple(comps))

    def labels(self) -> list[str]:
        return [c.label() for c in self.components]


def edge_admittance(comp: Component, s: np.ndarray | complex) -> np.ndarray | complex:
    """Admittance Y_e(s) of one component edge.

    R: 1/R    C: sC    L: 1/(dcr + sL)

    For the all-parallel case (V = 2 nodes) summing these over the single
    node pair reproduces exactly the closed-form Z(f) of DESIGN.md eq. (2-1).
    """
    if comp.kind == "R":
        return 1.0 / comp.value
    if comp.kind == "C":
        return s * comp.value
    return 1.0 / (comp.dcr + s * comp.value)
