"""topofit_id -- single-port identification with known topology and known
per-edge component kinds (Try3; see DESIGN.md in this directory).

Problem: given (a) discrete impedance measurements (f_k, z_k) and (b) the
DUT multigraph (nodes = junctions, 0/1 = port, edges = components with
known kind R/C/L, parallel edges and mixed kinds allowed), infer the most
likely element values.  Inductors carry two parameters (ideal L + series
DC resistance).  Error metrics follow Try1 DESIGN sec.5.1/8.1 verbatim.

Main entry points:
    identify(f, z, edges, config)        -> FitResult
    identify_many(f, z, graphs, config)  -> [FitResult] ranked by AICc
"""

from .fit import (FitConfig, FitResult, GroupReport, EdgeReport,
                  PHYS_BOUNDS, fit_graph, identify, identify_many)
from .graph import (KINDS, PortOpenError, ReductionResult, ReducedEdge,
                    eval_group, reduce_graph)
from .nodal import NodalModel, model_from_reduced

__all__ = [
    "identify", "identify_many", "fit_graph", "FitConfig", "FitResult",
    "GroupReport", "EdgeReport", "PHYS_BOUNDS", "KINDS", "PortOpenError",
    "ReductionResult", "ReducedEdge", "eval_group", "reduce_graph",
    "NodalModel", "model_from_reduced",
]
