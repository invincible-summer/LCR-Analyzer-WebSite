"""Equivalent-circuit fitting endpoints."""
from __future__ import annotations
from fastapi import APIRouter, Depends, HTTPException
from fastapi.responses import PlainTextResponse
from sqlalchemy.orm import Session

from ..models.db import get_db, FitResult
from ..schemas.fit import FitRequest, FitOut, FitSummary
from ..services import scan_service
from ..dsp.topology_fit import MODELS as TOPOLOGIES

router = APIRouter(tags=["fit"])


def _fit_to_out(fr: FitResult) -> FitOut:
    spice = None
    if fr.netlist:
        from ..dsp.synthesis import SynthesisResult
        spice = SynthesisResult(netlist=fr.netlist).spice()
    return FitOut(
        id=fr.id, scan_id=fr.scan_id, model=fr.model, kind=fr.kind,
        params=fr.params, param_ci=fr.param_ci, rmse=fr.rmse,
        chi2_red=fr.chi2_red, aicc=fr.aicc,
        converged=fr.converged if fr.converged is not None else True,
        passive=fr.passive, theory=fr.theory, residuals=fr.residuals,
        netlist=fr.netlist,
        poles=fr.poles, zeros=fr.zeros,
        warnings=fr.warnings, ranking=fr.ranking,
        spice=spice, created_at=fr.created_at,
    )


@router.get("/models")
def list_models():
    out = [{"name": "auto", "params": [], "label": "自动（矢量拟合 + 拓扑排名）"}]
    out += [
        {"name": m.name, "params": m.params, "label": m.label, "tex": m.tex}
        for m in TOPOLOGIES.values()
    ]
    return out


@router.post("/fit", response_model=FitOut)
def fit(req: FitRequest, db: Session = Depends(get_db)):
    if req.model != "auto" and req.model not in TOPOLOGIES:
        raise HTTPException(400, f"unknown model; choose 'auto' or {list(TOPOLOGIES)}")
    try:
        fr = scan_service.fit_scan(db, req.scan_id, req.model, req.calib_id)
    except ValueError as e:
        raise HTTPException(400, str(e))
    return _fit_to_out(fr)


@router.get("/fit/{fit_id}", response_model=FitOut)
def get_fit(fit_id: int, db: Session = Depends(get_db)):
    fr = db.get(FitResult, fit_id)
    if not fr:
        raise HTTPException(404, "fit not found")
    return _fit_to_out(fr)


@router.get("/fit/{fit_id}/spice", response_class=PlainTextResponse)
def get_fit_spice(fit_id: int, db: Session = Depends(get_db)):
    fr = db.get(FitResult, fit_id)
    if not fr:
        raise HTTPException(404, "fit not found")
    if not fr.netlist:
        raise HTTPException(404, "fit has no synthesised netlist (topology models only)")
    from ..dsp.synthesis import SynthesisResult
    return SynthesisResult(netlist=fr.netlist).spice()


@router.get("/scan/{scan_id}/fits", response_model=list[FitSummary])
def list_fits(scan_id: str, db: Session = Depends(get_db)):
    rows = db.query(FitResult).filter(FitResult.scan_id == scan_id)\
        .order_by(FitResult.created_at.desc()).all()
    return [
        FitSummary(id=r.id, scan_id=r.scan_id, model=r.model, kind=r.kind,
                   params=r.params, rmse=r.rmse, chi2_red=r.chi2_red,
                   aicc=r.aicc, created_at=r.created_at)
        for r in rows
    ]
