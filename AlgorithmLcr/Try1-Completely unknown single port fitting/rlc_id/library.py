"""Canonical series-parallel topology enumerator (DESIGN.md section 4.2).

Recursive generation following appendix B.1: split the leaf budget into
integer partitions, recurse with alternating node kinds (R1), enforce at most
one leaf per kind per node (R2) and store children in canonical order (R3),
so deduplication reduces to a canonical-string lookup.

Depth limit (deviation note): the generator defaults to ``max_idepth=2``,
i.e. non-root internal nodes have leaf-only children.  This reproduces the
locked counts 3/6/20/36 for n=1..4 and covers every DUT of the section 8.2
suite (all Foster I/II outputs also have internal depth <= 2).  A full-depth
R1-R3 enumeration would give 90 trees at n=4; the difference are trees like
S(R, P(R, S(R, C))) which are excluded here.  Pass ``max_idepth>=3`` to
``library``/``get_library`` to enumerate deeper trees.

Measured counts at the default depth (locked by tests):
  n = 1..6  ->  3, 6, 20, 36, 54, 78
"""

from __future__ import annotations

from functools import lru_cache

from .circuits import SER, PAR, Leaf, Node, Tree, canonical, make_node, n_leaves, opposite

DEFAULT_MAX_IDEPTH = 2

_LEAVES = (Leaf("R"), Leaf("L"), Leaf("C"))


def _leaf_combos(k: int) -> list[tuple[Leaf, ...]]:
    """All k-element subsets of distinct-kind leaves (R2)."""
    from itertools import combinations

    return list(combinations(_LEAVES, k))


@lru_cache(maxsize=None)
def _subtree_options(size: int, parent_kind: str, max_idepth: int) -> tuple[Tree, ...]:
    """All canonical subtrees with ``size`` leaves attachable under a node of
    ``parent_kind``; may include leaves (size 1) or internal nodes of the
    opposite kind within the remaining internal-depth budget."""
    if size == 1:
        return _LEAVES
    if max_idepth < 1:
        return ()
    kind = opposite(parent_kind)
    return tuple(_rooted_trees(size, kind, max_idepth))


def _rooted_trees(n: int, kind: str, max_idepth: int) -> list[Tree]:
    """All canonical trees of size n whose root node has the given kind."""
    # options[size] = subtrees of that size attachable under this node
    options: dict[int, tuple[Tree, ...]] = {}
    for size in range(1, n):
        opts = _subtree_options(size, kind, max_idepth - 1)
        if opts:
            options[size] = opts

    results: list[Tree] = []

    def dfs(remaining: int, chosen: list[Tree]) -> None:
        if remaining == 0:
            if len(chosen) >= 2 and _r2_ok(chosen):
                results.append(make_node(kind, chosen))
            return
        # candidate next child: canonical order >= previous (multiset order)
        start = canonical(chosen[-1]) if chosen else ""
        for size, opts in options.items():
            if size > remaining:
                continue
            for opt in opts:
                if start and canonical(opt) < start:
                    continue
                dfs(remaining - size, chosen + [opt])

    dfs(n, [])
    # The multiset DFS is duplicate-free by construction; dedup defensively.
    seen: set[str] = set()
    uniq: list[Tree] = []
    for t in results:
        cs = canonical(t)
        if cs not in seen:
            seen.add(cs)
            uniq.append(t)
    return uniq


def _r2_ok(children: list[Tree]) -> bool:
    """R2: at most one leaf of each kind among the direct children."""
    kinds = [c.kind for c in children if isinstance(c, Leaf)]
    return len(kinds) == len(set(kinds))


@lru_cache(maxsize=None)
def _trees_of_size(n: int, max_idepth: int) -> tuple[Tree, ...]:
    """All canonical topologies with exactly n elements."""
    if n == 1:
        return _LEAVES
    out: list[Tree] = []
    for kind in (SER, PAR):
        out.extend(_rooted_trees(n, kind, max_idepth))
    return tuple(sorted(out, key=canonical))


@lru_cache(maxsize=None)
def get_library(max_n: int = 4, max_idepth: int = DEFAULT_MAX_IDEPTH) -> tuple[Tree, ...]:
    """All canonical topologies with 1..max_n elements (cached)."""
    out: list[Tree] = []
    for n in range(1, max_n + 1):
        out.extend(_trees_of_size(n, max_idepth))
    return tuple(out)


def counts(max_n: int = 6, max_idepth: int = DEFAULT_MAX_IDEPTH) -> dict[int, int]:
    """Number of canonical topologies for each element count 1..max_n."""
    return {n: len(_trees_of_size(n, max_idepth)) for n in range(1, max_n + 1)}


def stats(max_n: int = 6, max_idepth: int = DEFAULT_MAX_IDEPTH) -> str:
    """Print the canonical-topology count table (n = 1..max_n)."""
    cs = counts(max_n, max_idepth)
    lines = [
        f"canonical series-parallel topologies (max internal depth = {max_idepth})",
        "+-----+-----------------+",
        "|   n | topologies      |",
        "+-----+-----------------+",
    ]
    for n, c in cs.items():
        lines.append(f"| {n:3d} | {c:15d} |")
    lines.append("+-----+-----------------+")
    text = "\n".join(lines)
    print(text)
    return text


__all__ = ["get_library", "counts", "stats", "DEFAULT_MAX_IDEPTH", "n_leaves"]
