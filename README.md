# ESP32 在线 LCR 阻抗分析平台

> ESP32 采集 → WiFi HTTP 上传原始 V/I 波形 → FastAPI 数字信号处理 → 复阻抗 → 等效电路拟合 → Vue3 工业仪表风可视化

一个把 ESP32 变成网页版 **LCR 阻抗分析仪**（轻量化 Keysight/Agilent 风格）的全栈项目。前端以**科学仪表**风格呈现完整数据处理全过程：采集 → 去直流 → 正弦拟合 → 残差 → 频谱 → 阻抗 → 扫频 Bode/Nyquist → 等效电路拟合。

## 系统架构

```
ESP32 (WiFi · HTTP POST 原始 v/i 波形)
        │
        ▼
FastAPI 后端 (Python · numpy/scipy)        SQLite
  ├─ dsp/  正弦拟合 · 阻抗 · FFT · 电路拟合 · OSL
  ├─ api/  upload · results · fit · export · ws
  └─ services/  scan_service · simulator(假ESP32) · hub(WS)
        │   JSON / WebSocket
        ▼
Vue3 + ECharts 前端（工业暗色 · KaTeX 方程 · Markdown）
  时域分析 · 扫频 Bode/Nyquist · 等效电路拟合 · 实时监测 · 历史 · OSL 校准
```

## 关键技术决策

**1. 阻抗算值用正弦最小二乘拟合（IEEE 1057 三参数），不是 FFT。** 激励频率 ω 已知，对 `v(t)`、`i(t)` 各拟合：

```
u(t) = a·sin(ωt) + b·cos(ωt) + c
```

得幅值与相位，进而得到阻抗：

```
A = √(a² + b²)            φ = atan2(b, a)
Z = (V_amp / I_amp) · e^{j(φv − φi)} = R + jX
```

无频谱泄漏、自带去直流（c 项）、相位干净，且**这条拟合曲线就是 u-t 图上要画的曲线**。FFT 仅作频谱诊断（看谐波/噪声底）。

**2. 两种"拟合"分层**：① **时域**正弦拟合（每频率，得 Z，画 u-t）；② **频域**电路模型拟合（跨扫频，得 R/L/C，画 Bode/Nyquist）。以串联 RLC 为例：

```
Z = R + j·(ωL − 1/(ωC))
```

**3. OSL 校准**接口已预留（开 / 短 / 负载 + 通道相位补偿），随硬件定型启用。

> 完整的数学推导、最小二乘求解、拟合策略与数值验证见 **[`docs/dsp_methodology.md`](docs/dsp_methodology.md)**。

## 目录

```
backend/   FastAPI + numpy/scipy + SQLAlchemy/SQLite
  app/dsp/         sine_fit, impedance, spectrum, circuit_fit, calibration
  app/api/         upload, results, fit, experiments, ws
  app/services/    scan_service, simulator, hub
  tests/           pytest（正弦拟合 / 阻抗 / 电路拟合）
frontend/  Vue3 + Vite + TS + ECharts + Pinia + KaTeX + markdown-it
  src/lib/         palette, format, charts(ECharts 选项库), generate(合成数据)
  src/components/  EChart, Latex, Markdown, StatTile, PanelStage, ScanBar, ...
  src/views/       AnalysisView, SweepView, FitView, LiveView, CalibrateView, HistoryView
docs/api_contract.md      ← ESP32 固件按此实现上传
docs/dsp_methodology.md   ← 数据处理方法（DSP 详细推导）
start.sh               ← 一键启动（见下）
```

## 快速开始

### 一键启动（推荐）

```bash
./start.sh            # 同时启动前后端（前台，Ctrl+C 停）
./start.sh backend    # 仅后端
./start.sh frontend   # 仅前端
./start.sh stop       # 停止（任意终端可执行，按记录端口精准清理）
./start.sh clean      # 清 vite 依赖缓存与 dist
```

特性：端口用 `socket.bind` 真实探测自动回退（后端 8000→8003，前端 5173→5176）；真实端口写入 `/tmp` 并经 `LCR_BACKEND` 注入前端；后端监听 `0.0.0.0`，启动时打印 **ESP32 上传地址**。

### 手动启动

```bash
# 后端（conda 环境 lcr，Python 3.11）
conda create -y -n lcr python=3.11
conda run -n lcr pip install -r backend/requirements.txt
cd backend && conda run -n lcr uvicorn app.main:app --port 8000 --reload

# 前端（pnpm）
cd frontend && pnpm install
LCR_BACKEND=http://localhost:8000 pnpm dev     # → http://localhost:5173

# 测试
cd backend && conda run -n lcr pytest
```

### 无硬件跑通全链路（模拟器）

```bash
cd backend && conda run -n lcr python -m app.services.simulator \
  --model series_RLC --R 50 --L 1e-3 --C 1e-6 --f-points 30 --noise 0.005
```

前端 ScanBar 也有「生成示例」按钮（串联 RLC / RC / RL / RLC+谐波）可直接合成数据。

## ESP32 固件对接

固件只需按 **[`docs/api_contract.md`](docs/api_contract.md)** 的契约，对每个激励频率 POST 一条原始波形：

```
POST /api/scan/start                         → 拿到 scan_id
POST /api/scan/{scan_id}/point               → { frequency, dt, n, voltage[], current[] }
```

后端负责全部 DSP 与拟合，固件无需改算法。文档里还写了三个精度命门：**V/I 同步采样**、**外挂 ADC**、**OSL 校准**。

## 已验证

- **pytest**：正弦拟合 / 阻抗（R / C / L）/ 电路拟合（串联 RLC · RC · 并联 RC）全绿。
- **端到端**：模拟器生成串联 RLC（R = 50 Ω, L = 1 mH, C = 1 µF）扫频，平台恢复 R = 49.99 Ω / L = 1.000 mH / C = 1.000 µF，精度 **99.9%**；逐频 |Z| 与相位和理论值吻合。
- **前端**：`vue-tsc` 类型检查零错误，`vite build` 通过；KaTeX 方程与 ECharts 图表在浏览器实测正确渲染（CVD 安全配色，暗 / 亮主题可切换）。
