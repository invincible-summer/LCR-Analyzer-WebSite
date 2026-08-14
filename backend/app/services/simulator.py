"""Mock ESP32: generate V/I waveforms for a known DUT and POST them to the server.

This lets the whole platform run and be validated without real hardware. The
generated physics is the time-domain counterpart of the circuit models in
``app.dsp.topology_fit``:

    drive  i(t) = I0 * sin(w t)
    DUT    Z(w) = R + jX(w)
    measure v(t) = I0 * |Z| * sin(w t + angle(Z))   (+ noise / DC / harmonic)

Run after starting the backend, e.g.:

    conda run -n lcr python -m app.services.simulator --model series_RLC \
        --R 50 --L 1e-3 --C 1e-6 --f-start 100 --f-stop 100000 --f-points 30
"""
from __future__ import annotations
import argparse
import asyncio
import math
import sys
from typing import Sequence

import numpy as np

from ..dsp.topology_fit import MODELS


def ordered_params(model: str, R: float, L: float, C: float) -> list[float]:
    md = MODELS[model]
    table = {"R": R, "L": L, "C": C}
    return [table[k] for k in md.params]


# a deliberately non-trivial DUT: two parallel-RLC sections in series --
# not expressible by any single library topology; only the vector-fitting
# engine can recover its structure
TWO_SECTION = {"R1": 1000.0, "L1": 1e-2, "C1": 1e-6,
               "R2": 50.0, "L2": 1e-5, "C2": 1e-9}


def _z_two_section(w: float) -> complex:
    p = TWO_SECTION
    z1 = 1.0 / (1.0 / p["R1"] + 1.0 / (1j * w * p["L1"]) + 1j * w * p["C1"])
    z2 = 1.0 / (1.0 / p["R2"] + 1.0 / (1j * w * p["L2"]) + 1j * w * p["C2"])
    return z1 + z2


def _model_z(model: str, params, w) -> complex:
    if model == "two_section":
        return _z_two_section(float(w))
    return complex(MODELS[model].func(np.asarray(params, dtype=float), np.array([w]))[0])


def make_waveforms(model: str, params: Sequence[float], f: float,
                   samples_per_cycle: int, cycles: int, i0: float,
                   noise: float, dc_v: float, harmonic: float,
                   rng: np.random.Generator):
    omega = 2.0 * math.pi * f
    n = samples_per_cycle * cycles - 1            # indices 0..n  -> n+1 samples
    dt = 1.0 / (f * samples_per_cycle)
    t = np.arange(n + 1, dtype=float) * dt

    Z = _model_z(model, params, omega)
    z_mag, z_ph = abs(Z), math.atan2(Z.imag, Z.real)

    i = i0 * np.sin(omega * t)
    v = i0 * z_mag * np.sin(omega * t + z_ph)
    if harmonic > 0:
        v = v + harmonic * i0 * z_mag * np.sin(2 * omega * t + z_ph)
    v = v + dc_v
    if noise > 0:
        v = v + rng.normal(0.0, noise * i0 * z_mag, v.shape)
        i = i + rng.normal(0.0, noise * i0, i.shape)
    return v, i, dt, n


def build_freqs(f_start: float, f_stop: float, points: int) -> np.ndarray:
    if points < 1:
        raise ValueError("f-points must be >= 1")
    if points == 1:
        return np.array([f_start], dtype=float)
    return np.logspace(math.log10(f_start), math.log10(f_stop), points)


async def run(url: str, device: str, freqs: np.ndarray, model: str,
              params: Sequence[float], note: str, seed: int,
              samples_per_cycle: int, cycles: int, i0: float,
              noise: float, dc_v: float, harmonic: float) -> None:
    rng = np.random.default_rng(seed)
    async with __import__("httpx").AsyncClient(base_url=url, timeout=30.0) as client:
        r = await client.post("/api/scan/start", json={
            "device": device, "freq_list": freqs.tolist(), "note": note,
        })
        r.raise_for_status()
        scan_id = r.json()["id"]
        print(f"[sim] scan {scan_id} started ({len(freqs)} points, model={model})")

        for f in freqs:
            v, i, dt, n = make_waveforms(
                model, params, float(f), samples_per_cycle, cycles,
                i0, noise, dc_v, harmonic, rng,
            )
            r = await client.post(f"/api/scan/{scan_id}/point", json={
                "device": device, "frequency": float(f), "dt": dt, "n": n,
                "voltage": v.tolist(), "current": i.tolist(),
            })
            r.raise_for_status()
            rec = r.json()
            Z_true = _model_z(model, params, 2 * math.pi * f)
            print(f"  f={f:>10.1f} Hz  |Z|_meas={rec['z_mag']:>10.3f} "
                  f"(true {abs(Z_true):>10.3f})  phase={rec['z_phase_deg']:>7.2f}° "
                  f"(true {math.degrees(math.atan2(Z_true.imag, Z_true.real)):>7.2f}°)")

        print(f"[sim] done. open the Analysis page and select scan {scan_id}")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="Mock ESP32 LCR data generator")
    p.add_argument("--url", default="http://localhost:8000")
    p.add_argument("--device", default="ESP32_LCR_SIM")
    p.add_argument("--model", default="series_RLC",
                   choices=list(MODELS) + ["two_section"])
    p.add_argument("--R", type=float, default=50.0)
    p.add_argument("--L", type=float, default=1e-3)
    p.add_argument("--C", type=float, default=1e-6)
    p.add_argument("--f-start", type=float, default=100.0)
    p.add_argument("--f-stop", type=float, default=100000.0)
    p.add_argument("--f-points", type=int, default=30)
    p.add_argument("--samples-per-cycle", type=int, default=64)
    p.add_argument("--cycles", type=int, default=16)
    p.add_argument("--i0", type=float, default=0.01)
    p.add_argument("--noise", type=float, default=0.0,
                   help="noise as fraction of signal amplitude (0..1)")
    p.add_argument("--dc", type=float, default=0.0, help="DC offset added to voltage")
    p.add_argument("--harmonic", type=float, default=0.0,
                   help="2nd-harmonic fraction added to voltage (makes u 'not a sine')")
    p.add_argument("--note", default="simulator run")
    p.add_argument("--seed", type=int, default=0)
    args = p.parse_args(argv)

    freqs = build_freqs(args.f_start, args.f_stop, args.f_points)
    params = [] if args.model == "two_section" else ordered_params(
        args.model, args.R, args.L, args.C)
    asyncio.run(run(
        args.url, args.device, freqs, args.model, params, args.note, args.seed,
        args.samples_per_cycle, args.cycles, args.i0,
        args.noise, args.dc, args.harmonic,
    ))
    return 0


if __name__ == "__main__":
    sys.exit(main())
