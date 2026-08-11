"""Upload endpoints: scan lifecycle + per-frequency point ingestion."""
from __future__ import annotations
from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from ..models.db import get_db, Scan
from ..schemas.upload import PointUpload, ScanStart, ScanBatch
from ..schemas.scan import ScanOut, MeasurementOut
from ..services import scan_service
from ..services.hub import hub

router = APIRouter(tags=["upload"])


@router.post("/scan/start", response_model=ScanOut)
def scan_start(payload: ScanStart, db: Session = Depends(get_db)):
    s = scan_service.start_scan(db, payload)
    return ScanOut(
        id=s.id, device=s.device, note=s.note, freq_list=s.freq_list,
        status=s.status, created_at=s.created_at, measurement_count=0,
    )


@router.post("/scan/{scan_id}/point", response_model=MeasurementOut)
async def scan_point(scan_id: str, p: PointUpload, db: Session = Depends(get_db)):
    if not db.get(Scan, scan_id):
        raise HTTPException(404, "scan not found")
    m = scan_service.add_point(db, scan_id, p)
    await hub.broadcast({
        "type": "point", "scan_id": scan_id,
        "frequency": m.frequency, "z_mag": m.z_mag, "z_phase_deg": m.z_phase_deg,
    })
    return scan_service.measurement_to_out(m)


@router.post("/scan/{scan_id}/batch", response_model=list[MeasurementOut])
async def scan_batch(scan_id: str, payload: ScanBatch, db: Session = Depends(get_db)):
    if not db.get(Scan, scan_id):
        raise HTTPException(404, "scan not found")
    out: list[MeasurementOut] = []
    for p in payload.points:
        m = scan_service.add_point(db, scan_id, p)
        await hub.broadcast({
            "type": "point", "scan_id": scan_id,
            "frequency": m.frequency, "z_mag": m.z_mag, "z_phase_deg": m.z_phase_deg,
        })
        out.append(scan_service.measurement_to_out(m))
    return out
