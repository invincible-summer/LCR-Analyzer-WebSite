# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An ESP32-based web LCR impedance analyzer. ESP32 uploads **raw V/I waveforms** per
excitation frequency over WiFi (HTTP); the FastAPI backend does all DSP, impedance
derivation, and equivalent-circuit fitting; the Vue3 frontend visualizes the full
processing pipeline in an instrument-style dark UI. ESP32 firmware is not yet written
— the contract it must implement is `docs/api_contract.md`.

## Commands

Backend uses a conda env named `lcr` (Python 3.11); the frontend uses pnpm.

```bash
# backend (from repo root)
conda run -n lcr pip install -r backend/requirements.txt        # first time
cd backend && conda run -n lcr uvicorn app.main:app --port 8001 --reload
conda run -n lcr python -m pytest                                # all tests
conda run -n lcr python -m pytest tests/test_sine_fit.py -k robust   # single test

# mock ESP32 (no hardware needed): generates known R/L/C waveforms and POSTs them
cd backend && conda run -n lcr python -m app.services.simulator --model series_RLC --R 50 --L 1e-3 --C 1e-6 --f-points 30

# frontend
cd frontend && pnpm install
LCR_BACKEND=http://localhost:8001 pnpm dev      # dev proxy target (default :8000)
pnpm build                                      # vue-tsc typecheck + vite build (run in frontend/)
```

`python3` on this WSL2 box is 3.14 (scipy wheels unreliable) — always use the `lcr`
conda env, not system python. Port 8000 is often occupied by another local process;
run the backend on 8001 and point the frontend at it with `LCR_BACKEND`.

## Architecture (the parts that span files)

**Request flow:** ESP32 `POST /api/scan/start` → `scan_id` → per frequency
`POST /api/scan/{scan_id}/point` (raw arrays). `app/services/scan_service.py:add_point`
runs the DSP immediately and persists both raw waveforms and derived impedance, so
the measurement row is self-sufficient for every later view.

**Two distinct "fits" — do not conflate them:**
- *Time-domain* (`app/dsp/sine_fit.py` + `impedance.py`): per frequency, a 3-parameter
  least-squares sine fit `a·sin(ωt)+b·cos(ωt)+c` at the **known** excitation ω. This is
  the impedance estimator (no FFT leakage) **and** the curve overlaid on the u-t/i-t
  plots. `∠Z = φv − φi`; phase convention (`φ = atan2(b,a)`) must stay identical across
  V and I channels — correctness depends on it.
- *Frequency-domain* (`app/dsp/circuit_fit.py`): across a whole sweep, fits RLC/RC/RL
  topologies to complex Z(f) via `scipy.optimize.least_squares` in **log10 space**
  (R/L/C span orders of magnitude). Produces R/L/C + a dense theory curve for
  Bode/Nyquist overlay. FFT (`dsp/spectrum.py`) is diagnostic-only, never feeds Z.

**Frontend chart pattern:** `src/lib/charts.ts` builds plain ECharts option objects
from a `Palette` (`src/lib/palette.ts`); every option is a Vue `computed` depending on
`app.theme`, so toggling theme recomputes all options and `EChart.vue` re-renders. The
palette is the dataviz reference set (CVD-validated; dark is primary). V and I are
**always separate charts** (different units → no shared axis, no dual-axis).

**State:** `src/store/scan.ts` (Pinia) holds the scan list + current scan detail and
auto-selects the most recent scan on load. Analysis/Sweep/Fit views all read it, so
selecting a scan on one page carries to the others.

**Calibration:** `app/dsp/calibration.py` (OSL open/short/load) exists and the
`CalibSet` model is in place, but capture endpoints are deferred until the analog
front-end topology is fixed — `CalibrateView` is an intentional placeholder.

## Conventions worth keeping

- ESP32 contract changes start in `docs/api_contract.md` and `app/schemas/upload.py`
  (pydantic validates `len(voltage)==len(current)==n+1`); mirror any new field through
  `models/db.py`, `services/scan_service.py`, and `schemas/scan.py`.
- Raw arrays are stored as JSON in SQLite — fine for a single-user lab tool, do not
  migrate to Postgres without reason.
- When adding a chart: reuse a builder in `charts.ts`, pass the active palette, keep
  measured-vs-fit as scatter+line (series slot 1 = measured/blue, slot 2 = theory/orange).
