"""Request schemas for the ESP32 upload contract."""
from __future__ import annotations
from pydantic import BaseModel, model_validator


class PointUpload(BaseModel):
    """One frequency point. ESP32 POSTs one of these per frequency in a sweep.

    time axis:  t[k] = k * dt,  k = 0..n   ->  n+1 samples.
    """
    device: str
    frequency: float           # excitation frequency (Hz)
    dt: float                  # sampling interval t (seconds); fs = 1/dt
    n: int                     # last index; sample count = n+1
    voltage: list[float]       # len == n+1, unit V
    current: list[float]       # len == n+1, unit A

    @model_validator(mode="after")
    def _check(self) -> "PointUpload":
        expected = self.n + 1
        if len(self.voltage) != expected:
            raise ValueError(f"voltage length {len(self.voltage)} != n+1 ({expected})")
        if len(self.current) != expected:
            raise ValueError(f"current length {len(self.current)} != n+1 ({expected})")
        if self.dt <= 0:
            raise ValueError("dt must be > 0")
        if self.frequency <= 0:
            raise ValueError("frequency must be > 0")
        if self.n < 2:
            raise ValueError("n must be >= 2 (need >=3 samples to fit)")
        return self


class ScanStart(BaseModel):
    device: str
    freq_list: list[float]
    note: str | None = None


class ScanBatch(BaseModel):
    device: str
    points: list[PointUpload]
