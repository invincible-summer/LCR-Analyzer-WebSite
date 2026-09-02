"""ASCII report generation (mirrors Try1 report format)."""

from __future__ import annotations

from .components import ComponentSet
from .selector import Candidate, EquivalenceClass


def _sci(x: float) -> str:
    if x == float("inf"):
        return "inf"
    return f"{x:.3e}"


def format_report(title: str, classes: list[EquivalenceClass],
                  compset: ComponentSet, truth_str: str | None = None,
                  top_k: int = 8,
                  extra_lines: list[str] | None = None) -> str:
    from .synthetic import network_str
    lines: list[str] = []
    lines.append(f"== {title} ==")
    lines.append("components: " + ", ".join(compset.labels()))
    if truth_str:
        lines.append("truth     : " + truth_str)
    lines.append("")
    header = (f"{'rk':>2} {'topology':<58} {'V':>2} {'SP':>2} "
              f"{'wRMSE':>9} {'maxRel':>9} {'RSS':>9} {'#eq':>3}")
    lines.append(header)
    lines.append("-" * len(header))
    for rank, cl in enumerate(classes[:top_k], start=1):
        c: Candidate = cl.representative
        wiring = network_str(c.network, compset)
        if len(wiring) > 58:
            wiring = wiring[:55] + "..."
        note = f" x{cl.n_members}" if cl.n_members > 1 else ""
        lines.append(f"{rank:>2} {wiring:<58} {c.network.structure.V:>2} "
                     f"{'Y' if c.sp else 'N':>2} {_sci(c.wrmse):>9} "
                     f"{_sci(c.max_rel_err):>9} {_sci(c.rss):>9}{note:>4}")
    if extra_lines:
        lines.append("")
        lines.extend(extra_lines)
    return "\n".join(lines)
