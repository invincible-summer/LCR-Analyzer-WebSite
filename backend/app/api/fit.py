"""Equivalent-circuit fitting endpoints."""
from __future__ import annotations
from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from ..models.db import get_db, FitResult
from ..schemas.fit import FitRequest, FitOut, FitSummary
from ..services import scan_service
from ..dsp.circuit_fit import MODELS

router = APIRouter(tags=["fit"])


@router.get("/models")
def list_models():
    return [
        {"name": m.name, "params": m.params, "label": m.label}
        for m in MODELS.values()
    ]


@router.post("/fit", response_model=FitOut)
def fit(req: FitRequest, db: Session = Depends(get_db)):
    if req.model not in MODELS:
        raise HTTPException(400, f"unknown model; choose from {list(MODELS)}")
    try:
        fr = scan_service.fit_scan(db, req.scan_id, req.model, req.calib_id)
    except ValueError as e:
        raise HTTPException(400, str(e))
    return FitOut(
        id=fr.id, scan_id=fr.scan_id, model=fr.model, params=fr.params,
        rmse=fr.rmse, accuracy=fr.accuracy, cost=fr.cost,
        theory=fr.theory, created_at=fr.created_at,
    )


@router.get("/scan/{scan_id}/fits", response_model=list[FitSummary])
def list_fits(scan_id: str, db: Session = Depends(get_db)):
    rows = db.query(FitResult).filter(FitResult.scan_id == scan_id)\
        .order_by(FitResult.created_at.desc()).all()
    return [
        FitSummary(id=r.id, scan_id=r.scan_id, model=r.model, params=r.params,
                   rmse=r.rmse, accuracy=r.accuracy, created_at=r.created_at)
        for r in rows
    ]
