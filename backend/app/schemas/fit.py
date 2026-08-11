"""Schemas for equivalent-circuit fitting."""
from __future__ import annotations
from datetime import datetime
from pydantic import BaseModel


class FitRequest(BaseModel):
    scan_id: str
    model: str            # one of dsp.circuit_fit.MODELS
    calib_id: int | None = None   # optional calibration set to apply before fitting


class FitSummary(BaseModel):
    id: int
    scan_id: str
    model: str
    params: dict
    rmse: float
    accuracy: float
    created_at: datetime


class FitOut(FitSummary):
    cost: float
    theory: dict
