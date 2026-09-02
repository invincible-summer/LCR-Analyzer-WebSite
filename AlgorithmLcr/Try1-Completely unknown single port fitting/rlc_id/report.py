"""ASCII identification report (DESIGN.md §5.5 D6.4, §8.1 metrics)."""

from __future__ import annotations

import numpy as np

from .circuits import to_string
from .selector import EquivalenceClass


def _fmt(x: float, width: int = 10, prec: int = 4) -> str:
    if not np.isfinite(x):
        return f"{'inf' if x > 0 else '-inf':>{width}}"
    return f"{x:>{width}.{prec}g}"


def format_report(title: str, classes: list[EquivalenceClass],
                  truth: str | None = None, top_k: int = 5,
                  extra_lines: list[str] | None = None) -> str:
    """Ranked candidate table: rank / topology+values / n / wRMSE / maxRelErr
    / AICc / equivalence annotation."""
    lines: list[str] = []
    lines.append("=" * 96)
    lines.append(f"DUT: {title}")
    if truth:
        lines.append(f"truth: {truth}")
    lines.append("-" * 96)
    header = (f"{'rk':>3} {'topology':<46} {'n':>2} {'wRMSE':>10} "
              f"{'maxRelErr':>10} {'AICc':>10}  note")
    lines.append(header)
    lines.append("-" * 96)
    if not classes:
        lines.append("(no valid candidates)")
    aicc0 = classes[0].aicc if classes else 0.0
    for rank, eq in enumerate(classes[:top_k], start=1):
        rep = eq.representative
        topo = to_string(rep.tree, rep.theta)
        if len(topo) > 46:
            topo = topo[:43] + "..."
        daicc = rep.aicc_val - aicc0
        notes: list[str] = []
        if len(eq.members) > 1:
            notes.append(f"equiv x{len(eq.members)}")
        if 0 < daicc < 2.0:
            notes.append("dAICc<2")
        if rep.engine == "B":
            notes.append("engB")
        if rep.note:
            notes.append(rep.note)
        lines.append(
            f"{rank:>3} {topo:<46} {rep.n_params:>2} "
            f"{_fmt(rep.wrmse)} {_fmt(rep.max_rel_err)} {_fmt(rep.aicc_val, 10, 2)}"
            f"  {'; '.join(notes)}")
        alt_count = 0
        for member in eq.members:
            if member is rep:
                continue
            if alt_count >= 2:
                break
            alt_count += 1
            mtopo = to_string(member.tree, member.theta)
            if len(mtopo) > 40:
                mtopo = mtopo[:37] + "..."
            lines.append(f"    └─ equiv: {mtopo:<40} (eng{member.engine}, "
                         f"AICc {member.aicc_val:.2f})")
    if extra_lines:
        lines.append("-" * 96)
        lines.extend(extra_lines)
    lines.append("=" * 96)
    return "\n".join(lines)


def format_skipped(skipped_notes: list[str]) -> str:
    """Format engine-B D8 skip annotations."""
    if not skipped_notes:
        return ""
    return "engine-B skipped candidates (D8): " + " | ".join(skipped_notes)
