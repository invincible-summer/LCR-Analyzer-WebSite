"""FastAPI application entry point."""
from __future__ import annotations
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from .config import CORS_ORIGINS
from .models.db import init_db
from .api import upload, results, fit, experiments, ws as ws_api

app = FastAPI(
    title="ESP32 LCR Analyzer",
    description="Online LCR impedance analysis platform: ingest -> DSP -> impedance -> fit -> visualize.",
    version="0.1.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=CORS_ORIGINS or ["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)

# create tables eagerly so the app (and TestClient) is ready immediately
init_db()

app.include_router(upload.router, prefix="/api")
app.include_router(results.router, prefix="/api")
app.include_router(fit.router, prefix="/api")
app.include_router(experiments.router, prefix="/api")
app.include_router(ws_api.router, prefix="/api")


@app.get("/")
def root():
    return {"service": "ESP32 LCR Analyzer", "status": "ok"}


@app.get("/health")
def health():
    return {"ok": True}
