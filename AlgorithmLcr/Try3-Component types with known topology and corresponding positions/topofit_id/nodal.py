"""Batched nodal analysis with adjoint sensitivities (DESIGN.md sec.4).

Stamping (Try2 netgraph_id/nodal.py convention): edge (u,v) with
admittance y contributes Y[u,u]+=y, Y[v,v]+=y, Y[u,v]-=y, Y[v,u]-=y.
Node 0 is grounded; with unit current injected at node 1,

    Z(s) = (Y_red^{-1})[0, 0]

over the (V-1)x(V-1) reduced matrix of non-ground nodes (node 1 first).

Parameter vector theta = log10 of linear values, laid out per edge in
order: R -> [log R];  C -> [log C];  L -> [log L, log Rd].

The Jacobian dZ/dtheta comes from the adjoint (Tellegen/direct) method:
with x = Y_red^{-1} e_0 (already computed for Z),

    dZ/dtheta_t = -x^T (dY/dtheta_t) x.

dY/dtheta_t has support only on edge(t)'s 2x2 stamp block, so

    dZ/dtheta_t[k] = -(dy_t/dtheta_t)[k] * (x[k, ri] - x[k, rj])^2

with x[ground] := 0.  After one batched LU per frequency the whole p x M
Jacobian costs O(M*p) -- essentially free (Director & Rohrer 1969).
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .graph import PortOpenError

LN10 = float(np.log(10.0))
# sanitized stand-in for non-finite Z (singular Y at an exact internal
# resonance); large enough to dominate any relative residual
_BIG_Z = 1e12


@dataclass
class NodalModel:
    """Reduced-matrix evaluator for an edge list [(u, v, kind), ...]."""

    edges: list              # [(u, v, kind)]
    node_of: list            # reduced index -> original label; [0] is node 1
    n_params: int
    edge_param_slices: list  # per edge, (start, count); L: (start, 2)
    ri_list: list            # per edge reduced index of u (None = ground)
    rj_list: list            # per edge reduced index of v (None = ground)

    @classmethod
    def from_edges(cls, edges: list) -> "NodalModel":
        nodes = {n for (u, v, _k) in edges for n in (u, v)}
        for t in (0, 1):
            if t not in nodes:
                raise PortOpenError("port node {} not incident to any edge".format(t))
        others = sorted(nodes - {0, 1})
        node_of = [1] + others
        index_of = {lab: i for i, lab in enumerate(node_of)}

        n_params = 0
        slices = []
        ri_list, rj_list = [], []
        for (u, v, kind) in edges:
            cnt = 2 if kind == "L" else 1
            slices.append((n_params, cnt))
            n_params += cnt
            ri_list.append(index_of.get(u))
            rj_list.append(index_of.get(v))
        return cls(edges=list(edges), node_of=node_of, n_params=n_params,
                   edge_param_slices=slices, ri_list=ri_list, rj_list=rj_list)

    @property
    def n_nodes_red(self) -> int:
        return len(self.node_of)

    # -- admittances ---------------------------------------------------------
    def _edge_y_dy(self, kind: str, vals: np.ndarray, s: np.ndarray):
        """y(s) and dy/dlog10param for one edge; vals = linear values.

        R: y=1/r,        dy/dt = -ln10 * y
        C: y=s*c,        dy/dt = +ln10 * y
        L: y=1/(rd+s*l), dy/dl = -ln10*l*s*y^2, dy/drd = -ln10*rd*y^2
        """
        if kind == "R":
            y = 1.0 / vals[0]
            dy = (-LN10 * y) * np.ones_like(s, dtype=complex)
            return y * np.ones_like(s, dtype=complex), [dy]
        if kind == "C":
            y = s * vals[0]
            return y, [(LN10 * y)]
        l, rd = vals
        z = rd + s * l
        y = 1.0 / z
        y2 = y * y
        return y, [(-LN10 * l * s) * y2, (-LN10 * rd) * y2]

    def _solve(self, Y: np.ndarray) -> np.ndarray:
        """Solve Y x = e_0 batched over frequencies; returns x (M, k)."""
        M, k, _ = Y.shape
        rhs = np.zeros((M, k, 1), dtype=complex)
        rhs[:, 0, 0] = 1.0
        try:
            return np.linalg.solve(Y, rhs)[:, :, 0]
        except np.linalg.LinAlgError:
            x = np.full((M, k), np.nan, dtype=complex)
            for m in range(M):
                try:
                    x[m] = np.linalg.lstsq(Y[m], rhs[m, :, 0], rcond=None)[0]
                except np.linalg.LinAlgError:
                    pass
            return x

    def z_and_jac(self, theta: np.ndarray, s: np.ndarray):
        """Z (M,) and full Jacobian dZ/dtheta (p, M)."""
        theta = np.asarray(theta, dtype=float)
        s = np.asarray(s, dtype=complex)
        M = len(s)
        k = self.n_nodes_red
        vals = np.power(10.0, theta)

        Y = np.zeros((M, k, k), dtype=complex)
        edge_dy = []  # per edge: list of dy arrays (len 1 or 2)
        for e, (u, v, kind) in enumerate(self.edges):
            sl, cnt = self.edge_param_slices[e]
            y, dys = self._edge_y_dy(kind, vals[sl:sl + cnt], s)
            edge_dy.append(dys)
            ri, rj = self.ri_list[e], self.rj_list[e]
            if ri is not None:
                Y[:, ri, ri] += y
            if rj is not None:
                Y[:, rj, rj] += y
            if ri is not None and rj is not None:
                Y[:, ri, rj] -= y
                Y[:, rj, ri] -= y

        x = self._solve(Y)
        Z = x[:, 0].copy()
        bad = ~np.isfinite(Z) | (np.abs(Z) > _BIG_Z)
        if bad.any():
            Z[bad] = _BIG_Z

        J = np.zeros((self.n_params, M), dtype=complex)
        for e, (u, v, kind) in enumerate(self.edges):
            ri, rj = self.ri_list[e], self.rj_list[e]
            d = np.zeros(M, dtype=complex)
            if ri is not None:
                d = d + x[:, ri]
            if rj is not None:
                d = d - x[:, rj]
            d2 = d * d
            sl, cnt = self.edge_param_slices[e]
            for j, dy in enumerate(edge_dy[e]):
                dz = -(dy * d2)
                dz[bad] = 0.0
                dz[~np.isfinite(dz)] = 0.0
                J[sl + j] = dz
        return Z, J

    def z_linear(self, vals: np.ndarray, s: np.ndarray) -> np.ndarray:
        """Z from linear values (synthetic truth, reports)."""
        vals = np.asarray(vals, dtype=float)
        if np.any(vals <= 0):
            raise ValueError("linear values must be positive")
        Z, _ = self.z_and_jac(np.log10(vals), s)
        return Z

    def elasticity(self, theta: np.ndarray, s: np.ndarray) -> np.ndarray:
        """E[t, k] = dlnZ_k / dln(value_t) at theta (p, M).

        dZ/dv_t = J_t / (ln10 * v_t)  so  E_t = (v_t/Z) dZ/dv_t = J_t/(Z ln10)
        -- the value factors cancel (regression-tested against central
        differences in tests/test_nodal.py)."""
        theta = np.asarray(theta, dtype=float)
        Z, J = self.z_and_jac(theta, s)
        return J / (Z[None, :] * LN10)


def model_from_reduced(red) -> NodalModel:
    """NodalModel over a ReductionResult's reduced edges."""
    return NodalModel.from_edges([(e.u, e.v, e.kind) for e in red.edges])
