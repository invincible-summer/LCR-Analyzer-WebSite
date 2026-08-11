"""Experiment management: export (CSV/JSON) and delete."""
from __future__ import annotations
import csv
import io
import json
from fastapi import APIRouter, Depends, HTTPException
from fastapi.responses import Response, StreamingResponse
from sqlalchemy.orm import Session

from ..models.db import get_db
from ..services import scan_service

router = APIRouter(tags=["experiments"])

_CSV_COLS = [
    "frequency", "z_real", "z_imag", "z_mag", "z_phase_deg",
    "R", "X", "D", "Q", "esr", "L_eq", "C_eq", "v_amp", "i_amp",
]


@router.get("/scan/{scan_id}/export")
def export(scan_id: str, format: str = "csv", db: Session = Depends(get_db)):
    d = scan_service.get_scan_detail(db, scan_id)
    if not d:
        raise HTTPException(404, "scan not found")

    if format == "json":
        body = json.dumps(d.model_dump(mode="json"), ensure_ascii=False, default=str)
        return Response(
            content=body, media_type="application/json",
            headers={"Content-Disposition": f'attachment; filename="scan_{scan_id}.json"'},
        )

    buf = io.StringIO()
    w = csv.writer(buf)
    w.writerow(_CSV_COLS)
    for m in d.measurements:
        w.writerow([getattr(m, c) for c in _CSV_COLS])
    return StreamingResponse(
        iter([buf.getvalue()]), media_type="text/csv",
        headers={"Content-Disposition": f'attachment; filename="scan_{scan_id}.csv"'},
    )


@router.delete("/scan/{scan_id}")
def delete_scan(scan_id: str, db: Session = Depends(get_db)):
    if not scan_service.delete_scan(db, scan_id):
        raise HTTPException(404, "scan not found")
    return {"ok": True}
