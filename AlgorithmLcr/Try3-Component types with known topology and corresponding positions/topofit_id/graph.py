"""Multigraph representation and exact structure reductions (DESIGN.md sec.3).

The DUT is a multigraph: nodes are junctions (0 and 1 are the port
terminals), edges are components.  Parallel edges between the same node
pair are legal and may carry different kinds.  Self loops carry zero
voltage hence zero current and are dropped.

Working at topology level only (kinds known, values unknown), four classes
of *exact* reductions remove edges/parameters that cannot influence Z:

  F1  self loops, edges outside the port-connected component, dangling
      edges (leaf at a non-port node carries no current);
  F2  same-kind parallel merge  (R: G=G1+G2, C: C=C1+C2; L is NOT mergeable
      in parallel -- two series-RL in parallel are second order);
  F3  same-kind series merge at degree-2 internal nodes (R: sum,
      C: reciprocal sum, L: L and Rd both sum);
  F4  series absorb of an R edge into an adjacent L edge (R + (L+Rd)
      = L + (Rd+R): the resistor only shifts the inductor's DCR).

Every rule preserves Z(s) exactly for ALL positive element values, so the
reduction is value-independent and can run before fitting.  Merged edges
keep an aggregation expression tree over the original edges; the tree is
evaluated on true values for benchmarks and reported to the user because
the members of a merged group are structurally unidentifiable
individually (only the aggregate is determined by Z).
"""

from __future__ import annotations

from dataclasses import dataclass

KINDS = ("R", "C", "L")

# value tuples carried by an edge/group: ("R", r) | ("C", c) | ("L", l, rd)
Val = tuple


class PortOpenError(ValueError):
    """The graph leaves the port (nodes 0-1) open at every frequency."""


def _pair(u: int, v: int) -> tuple:
    return (u, v) if u <= v else (v, u)


# ---------------------------------------------------------------------------
# aggregation expressions over original edges
# ---------------------------------------------------------------------------

def expr_leaf(idx: int, kind: str) -> tuple:
    return ("e", idx, kind)


def _agg(tag: str, children: list, kind: str) -> tuple:
    return (tag, tuple(children), kind)


def eval_group(expr: tuple, orig_vals: dict) -> tuple:
    """Evaluate an aggregation tree on original-edge values.

    ("e", i, k)        -> orig_vals[i]
    ("ser", xs, "R")   -> ("R", sum r)
    ("ser", xs, "C")   -> ("C", 1 / sum(1/c))
    ("ser", xs, "L")   -> ("L", sum l, sum of R-children r + sum of L rd)
    ("par", xs, "R")   -> ("R", 1 / sum(1/r))
    ("par", xs, "C")   -> ("C", sum c)
    """
    tag = expr[0]
    if tag == "e":
        return orig_vals[expr[1]]
    children, kind = expr[1], expr[2]
    vals = [eval_group(ch, orig_vals) for ch in children]
    if tag == "par":
        if kind == "R":
            return ("R", 1.0 / sum(1.0 / v[1] for v in vals))
        return ("C", sum(v[1] for v in vals))
    # series
    if kind == "R":
        return ("R", sum(v[1] for v in vals))
    if kind == "C":
        return ("C", 1.0 / sum(1.0 / v[1] for v in vals))
    l_tot = sum(v[1] for v in vals if v[0] == "L")
    rd_tot = sum(v[2] for v in vals if v[0] == "L") + \
        sum(v[1] for v in vals if v[0] == "R")
    return ("L", l_tot, rd_tot)


# ---------------------------------------------------------------------------
# reduction
# ---------------------------------------------------------------------------

@dataclass
class ReducedEdge:
    u: int                      # original node labels
    v: int
    kind: str                   # resulting kind
    expr: tuple                 # aggregation tree over original edges
    members: tuple              # original edge indices in the group


@dataclass
class ReductionResult:
    edges: list
    dropped: dict               # original edge idx -> reason
    n_passes: int

    @property
    def n_groups(self) -> int:
        return len(self.edges)

    def group_values(self, orig_vals: dict) -> list:
        return [eval_group(e.expr, orig_vals) for e in self.edges]

    def describe(self) -> str:
        parts = []
        for g, e in enumerate(self.edges):
            parts.append("g{}:{}[{}-{}]{{{}}}".format(
                g, e.kind, e.u, e.v, ",".join(map(str, e.members))))
        for i, why in sorted(self.dropped.items()):
            parts.append("e{}:dropped({})".format(i, why))
        return " ".join(parts)


@dataclass
class _WEdge:
    u: int
    v: int
    kind: str
    expr: tuple
    members: tuple


def _components(nodes: set, wedges: list) -> dict:
    parent = {n: n for n in nodes}

    def find(a: int) -> int:
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for e in wedges:
        ra, rb = find(e.u), find(e.v)
        if ra != rb:
            parent[ra] = rb
    return {n: find(n) for n in nodes}


def _drop_dangling(wedges: list, dropped: dict, nodes: set):
    """Iteratively strip edges hanging off non-port leaf nodes."""
    changed_any = False
    while True:
        deg = {n: 0 for n in nodes}
        for e in wedges:
            deg[e.u] += 1
            deg[e.v] += 1
        victim = None
        for n, d in sorted(deg.items()):
            if n not in (0, 1) and d == 1:
                victim_edge = next(e for e in wedges if e.u == n or e.v == n)
                victim = (n, victim_edge)
                break
        if victim is None:
            return wedges, changed_any
        n, edge = victim
        wedges = [e for e in wedges if e is not edge]
        nodes = {m for m in nodes if m != n}
        dropped[edge.members[0]] = "dangling"
        changed_any = True


def reduce_graph(edges: list) -> ReductionResult:
    """Topology-level exact reduction to fixpoint (DESIGN.md sec.3.3)."""
    if not edges:
        raise PortOpenError("empty edge list")
    for (u, v, k) in edges:
        if k not in KINDS:
            raise ValueError("kind must be one of {}, got {!r}".format(KINDS, k))

    work = [_WEdge(u, v, k, expr_leaf(i, k), (i,))
            for i, (u, v, k) in enumerate(edges)]
    dropped = {}
    n_passes = 0
    changed = True
    while changed:
        changed = False
        n_passes += 1

        # -- F1a: self loops ------------------------------------------------
        keep = []
        for e in work:
            if e.u == e.v:
                dropped[e.members[0]] = "self-loop"
                changed = True
            else:
                keep.append(e)
        work = keep

        # -- F1b: connectivity of the port ----------------------------------
        nodes = {n for e in work for n in (e.u, e.v)} | {0, 1}
        comp = _components(nodes, work)
        root0 = comp[0]
        if comp[1] != root0:
            raise PortOpenError("nodes 0 and 1 are not connected")
        keep = []
        for e in work:
            if comp[e.u] == root0:
                keep.append(e)
            else:
                dropped[e.members[0]] = "disconnected"
                changed = True
        work = keep

        # -- F1c: dangling branches -----------------------------------------
        nodes = {n for e in work for n in (e.u, e.v)} | {0, 1}
        work, ch = _drop_dangling(work, dropped, nodes)
        changed = changed or ch

        # -- F2: same-kind parallel merges (R, C only) ----------------------
        groups = {}
        for e in work:
            groups.setdefault(_pair(e.u, e.v), []).append(e)
        merged = []
        for pair, es in groups.items():
            by_kind = {}
            for e in es:
                by_kind.setdefault(e.kind, []).append(e)
            for kind, same in by_kind.items():
                if kind in ("R", "C") and len(same) > 1:
                    merged.append(_WEdge(pair[0], pair[1], kind,
                                         _agg("par", [e.expr for e in same], kind),
                                         tuple(m for e in same for m in e.members)))
                    changed = True
                else:
                    merged.extend(same)
        work = merged

        # -- F3/F4: series merges at degree-2 internal nodes -----------------
        deg = {}
        for e in work:
            for n in (e.u, e.v):
                deg[n] = deg.get(n, 0) + 1
        series_node = None
        for n, d in sorted(deg.items()):
            if n not in (0, 1) and d == 2:
                inc = [e for e in work if e.u == n or e.v == n]
                if len(inc) == 2:
                    e1, e2 = inc
                    a = e1.v if e1.u == n else e1.u
                    b = e2.v if e2.u == n else e2.u
                    if a != b:
                        ks = {e1.kind, e2.kind}
                        if ks in ({"R"}, {"C"}, {"L"}, {"R", "L"}):
                            series_node = (n, e1, e2, a, b)
                            break
        if series_node is not None:
            n, e1, e2, a, b = series_node
            kind = "L" if "L" in {e1.kind, e2.kind} else e1.kind
            work = [e for e in work if e is not e1 and e is not e2]
            work.append(_WEdge(a, b, kind,
                               _agg("ser", [e1.expr, e2.expr], kind),
                               e1.members + e2.members))
            changed = True

    if not work:
        raise PortOpenError("no edge influences the port after reduction")
    return ReductionResult(edges=[ReducedEdge(e.u, e.v, e.kind, e.expr, e.members)
                                  for e in work],
                           dropped=dropped, n_passes=n_passes)
