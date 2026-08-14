"""SQLAlchemy models. SQLite stores waveform/FFT arrays as JSON text."""
from __future__ import annotations
from datetime import datetime, timezone
from typing import Optional

from sqlalchemy import (
    create_engine, String, Float, Integer, ForeignKey, DateTime, Text, JSON,
    Boolean,
)
from sqlalchemy.orm import (
    DeclarativeBase, Mapped, mapped_column, relationship, sessionmaker, Session,
)

from ..config import DB_URL


def _now() -> datetime:
    return datetime.now(timezone.utc)


engine = create_engine(DB_URL, connect_args={"check_same_thread": False})
SessionLocal = sessionmaker(bind=engine, autoflush=False, expire_on_commit=False)


class Base(DeclarativeBase):
    pass


class Scan(Base):
    __tablename__ = "scans"

    id: Mapped[str] = mapped_column(String, primary_key=True)
    device: Mapped[str] = mapped_column(String, index=True)
    note: Mapped[Optional[str]] = mapped_column(Text, nullable=True)
    freq_list: Mapped[list] = mapped_column(JSON, default=list)
    status: Mapped[str] = mapped_column(String, default="open")  # open | done
    created_at: Mapped[datetime] = mapped_column(DateTime, default=_now)

    measurements: Mapped[list["Measurement"]] = relationship(
        back_populates="scan", cascade="all, delete-orphan",
        order_by="Measurement.frequency",
    )


class Measurement(Base):
    __tablename__ = "measurements"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    scan_id: Mapped[str] = mapped_column(ForeignKey("scans.id"), index=True)
    frequency: Mapped[float] = mapped_column(Float, index=True)
    dt: Mapped[float] = mapped_column(Float)
    n: Mapped[int] = mapped_column(Integer)

    # impedance
    z_real: Mapped[float] = mapped_column(Float)
    z_imag: Mapped[float] = mapped_column(Float)
    z_mag: Mapped[float] = mapped_column(Float)
    z_phase_deg: Mapped[float] = mapped_column(Float)
    z_sigma: Mapped[float] = mapped_column(Float, default=0.0)        # 1-sigma on |Z| (Ω)
    z_phase_sigma_deg: Mapped[float] = mapped_column(Float, default=0.0)
    R: Mapped[float] = mapped_column(Float)
    X: Mapped[float] = mapped_column(Float)
    D: Mapped[Optional[float]] = mapped_column(Float, nullable=True)
    Q: Mapped[Optional[float]] = mapped_column(Float, nullable=True)
    esr: Mapped[float] = mapped_column(Float)
    L_eq: Mapped[Optional[float]] = mapped_column(Float, nullable=True)
    C_eq: Mapped[Optional[float]] = mapped_column(Float, nullable=True)

    # per-channel sine-fit
    v_amp: Mapped[float] = mapped_column(Float)
    v_phase_deg: Mapped[float] = mapped_column(Float)
    i_amp: Mapped[float] = mapped_column(Float)
    i_phase_deg: Mapped[float] = mapped_column(Float)
    resid_rms_v: Mapped[float] = mapped_column(Float)
    resid_rms_i: Mapped[float] = mapped_column(Float)
    v_dc: Mapped[float] = mapped_column(Float)
    i_dc: Mapped[float] = mapped_column(Float)

    created_at: Mapped[datetime] = mapped_column(DateTime, default=_now)

    scan: Mapped["Scan"] = relationship(back_populates="measurements")
    raw: Mapped[Optional["RawWave"]] = relationship(
        back_populates="measurement", uselist=False, cascade="all, delete-orphan",
    )


class RawWave(Base):
    __tablename__ = "rawwaves"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    measurement_id: Mapped[int] = mapped_column(ForeignKey("measurements.id"), index=True)

    voltage: Mapped[list] = mapped_column(JSON)
    current: Mapped[list] = mapped_column(JSON)
    fitted_voltage: Mapped[list] = mapped_column(JSON)   # sine-fit overlay (AC+DC)
    fitted_current: Mapped[list] = mapped_column(JSON)
    resid_v: Mapped[list] = mapped_column(JSON)          # residual = raw - fitted (per sample)
    resid_i: Mapped[list] = mapped_column(JSON)
    fft_freqs: Mapped[list] = mapped_column(JSON)
    fft_mag_v: Mapped[list] = mapped_column(JSON)
    fft_mag_i: Mapped[list] = mapped_column(JSON)

    measurement: Mapped["Measurement"] = relationship(back_populates="raw")


class FitResult(Base):
    __tablename__ = "fitresults"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    scan_id: Mapped[str] = mapped_column(ForeignKey("scans.id"), index=True)
    model: Mapped[str] = mapped_column(String)     # "auto" | "auto_vf" | topology key
    kind: Mapped[str] = mapped_column(String)      # "vf" | "topology"
    params: Mapped[dict] = mapped_column(JSON)     # {name: value}; vf: {d, e}
    param_ci: Mapped[dict | None] = mapped_column(JSON, nullable=True)
    rmse: Mapped[float] = mapped_column(Float)
    chi2_red: Mapped[float] = mapped_column(Float)
    aicc: Mapped[float] = mapped_column(Float)
    converged: Mapped[bool] = mapped_column(Boolean, default=True)
    passive: Mapped[bool | None] = mapped_column(Boolean, nullable=True)
    theory: Mapped[dict] = mapped_column(JSON)     # dense curve {frequency, z_mag, ...}
    residuals: Mapped[dict | None] = mapped_column(JSON, nullable=True)
    netlist: Mapped[dict | None] = mapped_column(JSON, nullable=True)   # Foster tree (vf)
    poles: Mapped[list | None] = mapped_column(JSON, nullable=True)     # [[re, im], ...]
    zeros: Mapped[list | None] = mapped_column(JSON, nullable=True)
    warnings: Mapped[list | None] = mapped_column(JSON, nullable=True)
    ranking: Mapped[list | None] = mapped_column(JSON, nullable=True)   # auto: candidate rows
    created_at: Mapped[datetime] = mapped_column(DateTime, default=_now)


class CalibSet(Base):
    __tablename__ = "calibsets"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    name: Mapped[str] = mapped_column(String)
    # each standard: {freq: [re, im]}
    short: Mapped[dict] = mapped_column(JSON, default=dict)
    open_: Mapped[dict] = mapped_column(JSON, default=dict)
    load: Mapped[dict] = mapped_column(JSON, default=dict)
    load_true: Mapped[float] = mapped_column(JSON, default=0.0)  # known load value (Ω)
    created_at: Mapped[datetime] = mapped_column(DateTime, default=_now)


def init_db() -> None:
    """Create tables and lightly migrate legacy dev databases.

    The fit-results schema was rebuilt (accuracy/cost -> chi2_red/aicc/CI/
    netlist/...). All data in existing dev DBs is simulator-generated (the
    firmware does not exist yet), so the legacy table is simply dropped.
    New measurement columns are added via ALTER TABLE.
    """
    from sqlalchemy import inspect, text
    Base.metadata.create_all(engine)
    insp = inspect(engine)
    if "fitresults" in insp.get_table_names():
        cols = {c["name"] for c in insp.get_columns("fitresults")}
        if "aicc" not in cols:
            with engine.begin() as conn:
                conn.execute(text("DROP TABLE fitresults"))
            FitResult.__table__.create(engine)
    if "measurements" in insp.get_table_names():
        cols = {c["name"] for c in insp.get_columns("measurements")}
        with engine.begin() as conn:
            if "z_sigma" not in cols:
                conn.execute(text("ALTER TABLE measurements ADD COLUMN z_sigma FLOAT DEFAULT 0"))
            if "z_phase_sigma_deg" not in cols:
                conn.execute(text(
                    "ALTER TABLE measurements ADD COLUMN z_phase_sigma_deg FLOAT DEFAULT 0"))


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
