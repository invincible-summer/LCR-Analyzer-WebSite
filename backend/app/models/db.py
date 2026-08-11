"""SQLAlchemy models. SQLite stores waveform/FFT arrays as JSON text."""
from __future__ import annotations
from datetime import datetime, timezone
from typing import Optional

from sqlalchemy import (
    create_engine, String, Float, Integer, ForeignKey, DateTime, Text, JSON,
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
    model: Mapped[str] = mapped_column(String)
    params: Mapped[dict] = mapped_column(JSON)
    rmse: Mapped[float] = mapped_column(Float)
    accuracy: Mapped[float] = mapped_column(Float)
    cost: Mapped[float] = mapped_column(Float)
    theory: Mapped[dict] = mapped_column(JSON)
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
    Base.metadata.create_all(engine)


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
