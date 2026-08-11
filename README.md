# ESP32 在线 LCR 阻抗分析平台

基于 ESP32 的网页版 LCR 阻抗分析仪：ESP32 在不同激励频率下采集 V/I 时序，经 WiFi HTTP 上传原始波形，后端做数字信号处理（正弦拟合）→ 计算复阻抗 → 等效电路拟合，前端以工业仪表风格呈现**完整数据处理全过程**（采集 → 去直流 → 正弦拟合 → 残差 → 频谱 → 阻抗 → 扫频 Bode/Nyquist → 电路拟合）。

## 架构

```
ESP32 (WiFi HTTP POST 原始 v/i 波形)
        │
        ▼
FastAPI 后端 (Python)          SQLite
  ├─ dsp/  正弦拟合 · 阻抗 · FFT · 电路拟合 · OSL
  ├─ api/  upload · results · fit · export · ws
  └─ services/  scan_service · simulator(假ESP32) · hub(WS)
        │   JSON / WebSocket
        ▼
Vue3 + ECharts 前端 (工业暗色仪表风)
  时域分析 · 扫频 Bode/Nyquist · 等效电路拟合 · 实时监测 · 历史 · OSL校准
```

## 关键技术决策

1. **阻抗算值用正弦最小二乘拟合（IEEE 1057 三参数），不是 FFT。** 激励频率已知，对 v(t)、i(t) 各拟合 `a·sin(ωt)+b·cos(ωt)+c`，得幅值/相位 → `|Z|=V_amp/I_amp`、`∠Z=φv−φi`。无泄漏、自带去直流、相位干净，且**拟合曲线就是 u-t 图上画的曲线**。FFT 降级为频谱诊断。
2. **两种"拟合"分层**：① 时域正弦拟合（每频率，得 Z，画 u-t）；② 频域电路模型拟合（跨扫频，得 R/L/C，画 Bode/Nyquist）。
3. **OSL 校准**接口已预留（开/短/负载 + 通道相位补偿），随硬件定型启用。

## 目录

```
backend/   FastAPI + numpy/scipy + SQLAlchemy/SQLite
  app/dsp/         sine_fit, impedance, spectrum, circuit_fit, calibration
  app/api/         upload, results, fit, experiments, ws
  app/services/    scan_service, simulator, hub
  tests/           pytest（正弦拟合 / 阻抗 / 电路拟合）
frontend/  Vue3 + Vite + TS + ECharts + Pinia
  src/lib/         palette, format, charts(ECharts 选项库), generate(合成数据)
  src/components/  EChart, StatTile, PanelStage, ScanBar, ...
  src/views/       AnalysisView, SweepView, FitView, LiveView, CalibrateView, HistoryView
docs/api_contract.md   ← ESP32 固件按此实现上传
```

## 运行

### 一键启动（推荐）

```bash
./start.sh            # 同时启动前后端（前台运行，Ctrl+C 停）
./start.sh backend    # 仅后端
./start.sh frontend   # 仅前端
./start.sh stop       # 停止（任意终端可执行，按记录端口精准清理）
./start.sh clean      # 清 vite 依赖缓存与 dist
```

脚本特性：端口用 `socket.bind` 真实探测自动回退（后端 8000→8001→…，前端 5173→…）；后端真实端口写入 `/tmp/lcr_backend_port` 并经 `LCR_BACKEND` 注入前端；停止按端口精准杀、不误伤他人；后端监听 `0.0.0.0`，启动时会打印 **ESP32 上传地址**（用你机器的真实局域网 IP）。

### 后端（conda 环境 `lcr`，Python 3.11）
```bash
# 首次：建环境 + 装依赖
conda create -y -n lcr python=3.11
conda run -n lcr pip install -r backend/requirements.txt

# 启动（默认 8000；若被占用用 8001）
cd backend && conda run -n lcr uvicorn app.main:app --port 8001 --reload
```

### 前端（pnpm）
```bash
cd frontend && pnpm install
# dev 代理默认指向 http://localhost:8000；后端在别的端口时：
LCR_BACKEND=http://localhost:8001 pnpm dev
# 打开 http://localhost:5173
```

### 模拟器（无硬件即可跑通全链路）
```bash
cd backend
conda run -n lcr python -m app.services.simulator \
  --model series_RLC --R 50 --L 1e-3 --C 1e-6 \
  --f-start 100 --f-stop 100000 --f-points 30 --noise 0.005
```
前端也有「生成示例」按钮（串联RLC / RC / RL / RLC+谐波）可直接合成数据。

### 测试
```bash
cd backend && conda run -n lcr pytest
```

## ESP32 固件对接

固件只需按 **`docs/api_contract.md`** 的契约，对每个激励频率 POST 一条原始波形即可。后端负责全部 DSP 与拟合，固件无需改动算法。典型流程：`POST /api/scan/start` 拿到 `scan_id` → 对每个频率 `POST /api/scan/{scan_id}/point`。

## 已验证

- pytest：正弦拟合 / 阻抗（R/C/L）/ 电路拟合（串联RLC·RC·并联RC）全绿。
- 端到端：模拟器生成串联 RLC（R=50Ω L=1mH C=1µF）扫频，平台恢复 **R=49.99Ω / L=1.000mH / C=1.000µF，精度 99.9%**；逐频 |Z| 与相位和理论值吻合。
- 前端：`vue-tsc` 类型检查零错误，`vite build` 通过；浏览器实测图表正确渲染（暗色主题 + 校验通过的色盲安全配色）。
