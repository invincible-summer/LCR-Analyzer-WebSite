"""Application configuration (env-overridable constants)."""
from __future__ import annotations
import os
from pathlib import Path

# backend/
BASE_DIR = Path(__file__).resolve().parent.parent
DATA_DIR = Path(os.environ.get("LCR_DATA_DIR", BASE_DIR / "data"))
DATA_DIR.mkdir(parents=True, exist_ok=True)

DB_PATH = Path(os.environ.get("LCR_DB_PATH", DATA_DIR / "lcr.db"))
DB_URL = f"sqlite:///{DB_PATH}"

API_PREFIX = "/api"

# Comma-separated list of allowed origins, or "*" for all (dev).
_cors = os.environ.get("LCR_CORS_ORIGINS", "*")
CORS_ORIGINS = [o.strip() for o in _cors.split(",") if o.strip()]
