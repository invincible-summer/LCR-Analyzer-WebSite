# AGENTS.md

This file provides guidance to Agents (codex / claude code / ... ) when working with code in this repository.

## What this is

An ESP32-based web LCR impedance analyzer. ESP32 uploads **raw V/I waveforms** per
excitation frequency over WiFi (HTTP); the FastAPI backend does all DSP, impedance
derivation, and equivalent-circuit fitting; the Vue3 frontend visualizes the full
processing pipeline in an instrument-style dark UI with KaTeX-rendered equations.
ESP32 firmware is not yet written — the contract it must implement is
`docs/api_contract.md`.

## Reference docs

- **`README.md`** — §算法原理与公式推导（正弦拟合 / 阻抗 / 电路拟合 / FFT / OSL 的完整公式与推导）与 §实现细节（后端 `add_point` 数据处理流水、前端 `charts.ts` / `EChart.vue` 绘图流程、`circuit_fit` 的 log 空间参数化 / 加权残差 / 初值启发式 / 求解器），**与代码逐项同步**。改 DSP、改拟合或写文档前先读这两节，勿重复推导。
- **`docs/api_contract.md`** — ESP32 上传契约（固件照此实现）。
- 仓库：`origin → github.com/invincible-summer/LCR-Analyzer-WebSite`，单分支 `main`，直接提交并推送（不走 PR）。

## Commands

Backend uses a conda env named `lcr` (Python 3.11); the frontend uses pnpm.

```bash
# one-click launcher (preferred) — picks free ports, injects real backend port into frontend
./start.sh            # all: backend (0.0.0.0) + frontend, Ctrl+C to stop
./start.sh stop       # kills by recorded ports (any terminal); ./start.sh clean clears vite cache + dist

# backend (manual)
conda run -n lcr pip install -r backend/requirements.txt        # first time
cd backend && conda run -n lcr uvicorn app.main:app --port 8001 --reload
conda run -n lcr python -m pytest                                # all tests
conda run -n lcr python -m pytest tests/test_sine_fit.py -k robust   # single test

# mock ESP32 (no hardware): generates known R/L/C waveforms and POSTs them
cd backend && conda run -n lcr python -m app.services.simulator --model series_RLC --R 50 --L 1e-3 --C 1e-6 --f-points 30

# frontend
cd frontend && pnpm install
LCR_BACKEND=http://localhost:8001 pnpm dev      # dev proxy target (default :8000)
pnpm build                                      # vue-tsc typecheck + vite build (run in frontend/)
```

Environment gotchas:
- `python3` on this WSL2 box is 3.14 (scipy wheels unreliable) — always use the `lcr`
  conda env, not system python.
- Port 8000 is often occupied by another local process; run the backend on 8001 and
  point the frontend at it with `LCR_BACKEND` (or just use `start.sh`, which falls back
  8000→8003 / 5173→5176 automatically).
- pnpm (v11+) blocks dependency build scripts: `frontend/pnpm-workspace.yaml` sets
  `allowBuilds: { esbuild: true, vue-demi: true }`. If `pnpm install`/`build` fail with
  `ERR_PNPM_IGNORED_BUILDS`, that file is the fix — do not delete it.

## Architecture (the parts that span files)

**Request flow:** `POST /api/scan/start` → `scan_id` → per frequency
`POST /api/scan/{scan_id}/point` (raw arrays). `app/services/scan_service.py:add_point`
runs the DSP immediately and persists both raw waveforms and derived impedance, so the
measurement row is self-sufficient for every later view.

**Two distinct "fits" — do not conflate them:**
- *Time-domain* (`app/dsp/sine_fit.py` + `impedance.py`): per frequency, a 3-parameter
  least-squares sine fit `a·sin(ωt)+b·cos(ωt)+c` at the **known** excitation ω. This is
  the impedance estimator (no FFT leakage) **and** the curve overlaid on the u-t/i-t
  plots. `∠Z = φv − φi`; the phase convention (`φ = atan2(b,a)`) must stay identical
  across V and I channels — correctness depends on it.
- *Frequency-domain* (`app/dsp/circuit_fit.py`): across a whole sweep, fits RLC/RC/RL
  topologies to complex Z(f) via `scipy.optimize.least_squares` in **log10 space**
  (R/L/C span orders of magnitude). Produces R/L/C + a dense theory curve for
  Bode/Nyquist overlay. FFT (`dsp/spectrum.py`) is diagnostic-only, never feeds Z.

**Frontend rendering pattern:** `src/lib/charts.ts` builds plain ECharts option objects
from a `Palette` (`src/lib/palette.ts`); every chart option is a Vue `computed` depending
on `app.theme`, so toggling theme recomputes all options and `EChart.vue` re-renders. The
palette mirrors the validated dataviz reference set (CVD-safe; dark is primary). V and I
are **always separate charts** (different units → no shared axis, no dual-axis).
Equations are rendered by `src/components/Latex.vue` (KaTeX) and rich text by
`Markdown.vue` (markdown-it + texmath, supports `$...$` / `$$...$$`); KaTeX CSS is
imported once in `src/main.ts`. **Gotcha:** in TypeScript strings, write math with
`String.raw\`...\`` (or double-escaped `\\frac`); in Vue template attributes (`tex="..."`)
backslashes are literal, so single `\frac` is correct there.

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
- When adding an equation: drop a `<div class="eq-block">` with a `<Latex :tex=… :display>`;
  the styling (accent halo, overflow scroll) is already in `src/style.css`.
