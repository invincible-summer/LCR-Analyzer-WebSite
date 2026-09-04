# AGENTS.md

This file provides guidance to Agents (codex / claude code / ... ) when working with code in this repository.

## What this is

An ESP32-based web LCR impedance analyzer with **two data flows** (see root `DESIGN.md` for the authoritative architecture):

1. **测量流（后端计算）**: ESP32 uploads raw V/I waveforms per excitation frequency over
   WiFi (HTTP); the FastAPI backend does the DSP (sine-fit impedance + σ propagation) and
   persists everything in SQLite. 时域分析 / 扫频 / 实时监测 / 实验历史 views read this.
2. **拟合流（前端本地计算，不经后端）**: 测量数据（CSV 上传 / 示例生成 / 历史扫描导入）
   → **`AlgorithmLcr/` 三个 C++17 引擎的 WebAssembly 编译版**在浏览器 Web Worker 里跑
   （Try1 未知辨识 / Try2 已知元件 / Try3 已知拓扑）→ Top-K 候选 + 电路图。
   The fitting page (`/fit`, 电路辨识拟合) is entirely client-side.

ESP32 firmware for the HTTP contract is not yet written — the contract is
`docs/api_contract.md`. **ESP32 蓝牙直传测量文件（Web Bluetooth）是规划项**：页面按钮为
禁用占位，落地时同步 `frontend/src/docs/esp32.md` 与 `docs/api_contract.md`。

The UI is a light "scientific journal" style (not dark); all text is Chinese.

## Reference docs

- **`DESIGN.md`（根目录）** — 项目架构与数据契约权威规范：两条数据流、WASM C ABI、
  响应 JSON schema、SP 规约语义、验证基线。**改任何契约前先读并同步它**。
- **`AlgorithmLcr/INPUT_FORMAT.md` / `OUTPUT_FORMAT.md`** — 三个引擎输入输出的数学规范
  （唯一权威；WASM glue 与之对齐）。各 `DESIGN.md`（Try1/2/3 子目录）是算法推导。
- **`docs/algorithms.md`** — 旧 Python VF/固定拓扑拟合的推导文档（后端引擎仍在、前端已无入口）。
- **`README.md`** — 面向用户的总览。
- **`docs/api_contract.md`** — ESP32 HTTP 上传契约（测量流）。
- **`ino/README.md`** — ESP32 本地仪表固件（TFT UI + 本地 DSP + 蓝牙通路）。
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
pnpm test                                       # vitest unit tests (csv/adjacency/docToc/synthData)
pnpm build                                      # vue-tsc typecheck + vite build (run in frontend/)

# WASM 计算层（改 AlgorithmLcr 源码后必须重跑，并把产物一并提交）
#   一次性安装: git clone https://github.com/emscripten-core/emsdk ~/emsdk &&
#              ~/emsdk/emsdk install latest && ~/emsdk/emsdk activate latest
cd frontend/wasm && ./build.sh                  # → frontend/src/wasm/lcr_wasm.{js,wasm}
cmake -S . -B build-native && cmake --build build-native --target glue_test && ./build-native/glue_test   # 原生 glue 测试
node tests/smoke.mjs                            # 同一 C ABI 的 Node 烟测
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
- vitest 必须用 2.x（vite 5 不兼容 vitest 5 的 module-runner）。
- **WASM 产物（`frontend/src/wasm/lcr_wasm.{js,wasm}`）提交入 git**，克隆即可运行；
  修改 `AlgorithmLcr/**/cppversion` 后重跑 `build.sh` 并提交新产物，否则网页引擎与算法源脱节。

## Architecture (the parts that span files)

**测量流 request flow:** `POST /api/scan/start` → `scan_id` → per frequency
`POST /api/scan/{scan_id}/point` (raw arrays). `app/services/scan_service.py:add_point`
runs the DSP immediately and persists both raw waveforms and derived impedance.

**Time-domain DSP** (`app/dsp/sine_fit.py` + `impedance.py`): per frequency, a 3-parameter
least-squares sine fit at the **known** excitation ω — the impedance estimator (no FFT
leakage). `∠Z = φv − φi`; the phase convention (`φ = atan2(b,a)`) must stay identical
across V and I channels — correctness depends on it. σ propagation (→ `z_sigma`) feeds
everything downstream. FFT (`dsp/spectrum.py`) is diagnostic-only.

**旧 VF/固定拓扑拟合（后端，无前端入口）**: `app/dsp/rational_fit.py` + `synthesis.py` +
`fit_auto.py` + `topology_fit.py`; `/api/models`、`/api/fit` 端点仍在。文档
`docs/algorithms.md`。保留但不再演进。

**拟合流（前端，`/fit` 页）** — 全部本地、零后端依赖：
- `frontend/wasm/` 把 `AlgorithmLcr` 三个 cppversion 编译成一个 WASM 模块（三个引擎
  头文件同名，**必须一引擎一 TU**；glue 的 C ABI 与 JSON 契约见 `DESIGN.md` §2）。
- `src/lib/lcrWasm.ts` + `src/workers/fitWorker.ts`：module worker 加载 emscripten
  ES6 模块（`locateFile` → `?url` 的 wasm）；**postMessage 前对 job 做 JSON 净化**（Vue
  reactive proxy 不可结构化克隆——这是踩过的坑，别删）；取消 = terminate + 重建。
- `src/lib/csv.ts`：网站标准 CSV（`f, Re(Z), Im(Z)` 每行）解析，宽容 measurements.txt。
- `src/lib/adjacency.ts`：`graphToNetlist` 把引擎输出的上三角邻接矩阵（OUTPUT_FORMAT.md）
  规约成 SP 树；**非 SP（桥式）返回 null** → `GraphSchematic`（图论视图）兜底。
- 数据入口四通道：CSV 上传 / 示例生成（`lib/synthData.ts` 本地合成）/ 历史扫描导入
  （`api.getScan` → f/z 点，连接测量流）/ ESP32 蓝牙（禁用占位，稍后）。
- `src/docs/*.md` + `views/DocsView.vue`：内置使用文档（左列表/中正文/右 TOC，sticky +
  scrollspy）；内容与各面板 `HelpBubble` 的约束文案**同步维护**。

**Frontend rendering pattern:** `src/lib/charts.ts` builds plain ECharts option objects
from a `Palette`; every chart option is a Vue `computed`. Measured-vs-fit is always
scatter+line (slot 1 = measured/blue, slot 2 = theory/orange). V and I are **always
separate charts**. Equations: `Latex.vue` (KaTeX); markdown: `Markdown.vue`
(markdown-it + texmath `$...$`/`$$...$$`). **Gotcha:** in TS strings use
`String.raw\`...\`` for math; in Vue template attributes single `\frac` is correct.

**Circuit schematics:** `src/lib/schematic.ts` is a pure recursive SP-tree layout engine
(wires, junction dots, IEC symbols, 2-line labels, port terminals `1`/`0`);
`Schematic.vue` renders it declaratively. An L leaf may carry `dcr` — rendered as
`L 值 · DCR 值` in the value line (still ONE device). Non-SP results render through
`GraphSchematic.vue`; the Try3 editor is `GraphEditor.vue` (csacademy-style, four modes,
ports 0/1 locked).

**State:** `src/store/scan.ts` (Pinia) holds the scan list + detail (测量流 views).
The fitting page keeps its own local state and does NOT persist to the backend.

**Calibration:** `app/dsp/calibration.py` (OSL) exists but capture endpoints are
deferred — `CalibrateView` is an intentional placeholder.

## Conventions worth keeping

- **改 `AlgorithmLcr` 引擎** → 重跑 `frontend/wasm/build.sh`、过原生 glue 测试 + Node
  烟测、提交新 WASM 产物；`DESIGN.md` §2 契约若变则同步 `fitTypes.ts` 与 glue。
- **glue JSON ↔ `fitTypes.ts` 镜像**：任一侧字段变化必须两侧同步（注意 Try3 诊断在
  `try3` 子对象，不在 `stats`——踩过的坑）。
- ESP32 HTTP contract changes start in `docs/api_contract.md` and `app/schemas/upload.py`;
  mirror new fields through `models/db.py`, `services/scan_service.py`, `schemas/scan.py`.
- 测量流 DSP changes start in `docs/algorithms.md` (derivation), then the module.
- Raw arrays are stored as JSON in SQLite — fine for a single-user lab tool.
- 新增图表复用 `charts.ts` 构建器并传 palette；新增公式用 `eq-block` + `Latex`。
- 用户可见文案全部中文；数量级约束同时写进 `HelpBubble` 的 rows 与 `src/docs/`（保持一致）。
