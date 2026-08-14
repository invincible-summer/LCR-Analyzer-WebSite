"""Schemas for equivalent-circuit fitting."""
from __future__ import annotations
from datetime import datetime
from pydantic import BaseModel


class FitRequest(BaseModel):
    scan_id: str
    model: str = "auto"   # "auto" or one of dsp.topology_fit.MODELS
    calib_id: int | None = None   # optional calibration set to apply before fitting


class RankingRow(BaseModel):
    rank: int
    kind: str            # "vf" | "topology"
    model: str
    label: str
    n_params: int
    chi2_red: float
    aicc: float
    rmse: float
    delta_aicc: float
    selected: bool


class FitSummary(BaseModel):
    id: int
    scan_id: str
    model: str
    kind: str                     # "vf" | "topology"
    params: dict
    rmse: float
    chi2_red: float
    aicc: float
    created_at: datetime


class FitOut(FitSummary):
    param_ci: dict | None = None  # {param: [lo, hi]} 95% CI
    converged: bool = True
    passive: bool | None = None
    theory: dict
    residuals: dict | None = None
    netlist: dict | None = None   # Foster tree (kind == "vf")
    poles: list[list[float]] | None = None
    zeros: list[list[float]] | None = None
    warnings: list[str] | None = None
    ranking: list[RankingRow] | None = None
    spice: str | None = None      # .subckt text when a netlist exists
