"""Read endpoints: scan lists, scan detail, single measurement detail."""
from __future__ import annotations
from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from ..models.db import get_db
from ..schemas.scan import ScanOut, ScanDetail, MeasurementDetail
from ..services import scan_service

router = APIRouter(tags=["results"])


@router.get("/scans", response_model=list[ScanOut])
def list_scans(db: Session = Depends(get_db)):
    return scan_service.list_scans(db)


@router.get("/scan/{scan_id}", response_model=ScanDetail)
def get_scan(scan_id: str, db: Session = Depends(get_db)):
    d = scan_service.get_scan_detail(db, scan_id)
    if not d:
        raise HTTPException(404, "scan not found")
    return d


@router.get("/scan/{scan_id}/measurement/{measurement_id}", response_model=MeasurementDetail)
def get_measurement(scan_id: str, measurement_id: int, db: Session = Depends(get_db)):
    d = scan_service.get_measurement_detail(db, scan_id, measurement_id)
    if not d:
        raise HTTPException(404, "measurement not found")
    return d
