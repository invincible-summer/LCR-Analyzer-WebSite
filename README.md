# ESP32 在线 LCR 阻抗分析平台

> ESP32 采集原始 V/I 波形 → WiFi HTTP 上传 → FastAPI 数字信号处理 → 复阻抗 → 等效电路拟合 → Vue3 工业仪表风可视化

一个把 ESP32 变成网页版 **LCR 阻抗分析仪**（轻量化 Keysight / Agilent 风格）的全栈项目。后端用**正弦最小二乘拟合**（而非 FFT）从原始波形求阻抗，再跨频率做**等效电路拟合**；前端以科学仪表风格呈现完整数据处理链路：采集 → 去直流 → 正弦拟合 → 残差 → 频谱 → 阻抗 → 扫频 Bode / Nyquist → 电路拟合。

---

## 目录

- [系统架构](#系统架构)
- [算法原理与公式推导](#算法原理与公式推导)
  - [1. 时域正弦拟合（IEEE 1057）](#1-时域正弦拟合ieee-1057)
  - [2. 复阻抗推导](#2-复阻抗推导)
  - [3. 频域等效电路拟合](#3-频域等效电路拟合)
  - [4. FFT 频谱诊断](#4-fft-频谱诊断)
  - [5. OSL 校准](#5-osl-校准)
- [实现细节](#实现细节)
- [目录结构](#目录结构)
- [快速开始](#快速开始)
- [ESP32 固件对接](#esp32-固件对接)
- [验证结果](#验证结果)

---

## 系统架构

```
ESP32 (WiFi · HTTP POST 原始 v/i 波形)
        │
        ▼
FastAPI 后端 (Python · numpy / scipy)              SQLite
  ├─ dsp/    正弦拟合 · 阻抗 · FFT · 电路拟合 · OSL
  ├─ api/    upload · results · fit · export · ws
  └─ services/  scan_service · simulator(假 ESP32) · hub(WS)
        │   JSON / WebSocket
        ▼
Vue3 + ECharts 前端（工业暗色 · KaTeX 方程 · Markdown）
  时域分析 · 扫频 Bode/Nyquist · 等效电路拟合 · 实时监测 · 历史 · OSL 校准
```

两类拟合分层、不可混淆：

| | 时域拟合 | 频域拟合 |
|---|---|---|
| 作用域 | 单个频率点 | 整个扫频 |
| 模型 | $a\sin\omega t+b\cos\omega t+c$ | RLC / RC / RL 等效电路 |
| 产出 | 该频率的复阻抗 $Z$，并叠加在 u-t / i-t 图上 | R / L / C 元件值，理论曲线叠加在 Bode / Nyquist |
| 代码 | `app/dsp/sine_fit.py`、`impedance.py` | `app/dsp/circuit_fit.py` |

---

## 算法原理与公式推导

### 1. 时域正弦拟合（IEEE 1057）

ESP32 生成频率确切已知为 $f$（$\omega=2\pi f$）的正弦激励。V、I 两路采样信号均可写成「正弦 + 直流偏置 + 噪声」：

$$
x(t)=A\sin(\omega t+\varphi)+d+n(t),\qquad x\in\{v,i\}
$$

其中 $d$ 为 ADC 直流偏置，$n(t)$ 为噪声。把正弦项按和角公式展开：

$$
A\sin(\omega t+\varphi)=\underbrace{A\cos\varphi}_{a}\sin(\omega t)+\underbrace{A\sin\varphi}_{b}\cos(\omega t)
$$

于是得到 **3 参数正弦拟合模型**（$t_k=k\Delta t$，$k=0,\dots,N-1$）：

$$
x[k]\approx a\sin(\omega t_k)+b\cos(\omega t_k)+c
$$

最小二乘目标——最小化残差平方和：

$$
J(a,b,c)=\sum_{k=0}^{N-1}\bigl(x[k]-a\sin\omega t_k-b\cos\omega t_k-c\bigr)^{2}
$$

由于 $J$ 对 $\boldsymbol\theta=(a,b,c)^{\top}$ 是线性的，构造设计矩阵 $\mathbf{M}\in\mathbb{R}^{N\times 3}$，其第 $k$ 行为 $[\sin\omega t_k,\ \cos\omega t_k,\ 1]$，求解线性方程组：

$$
\hat{\boldsymbol\theta}=\arg\min_{\boldsymbol\theta}\lVert \mathbf{x}-\mathbf{M}\boldsymbol\theta\rVert^{2}
\quad\Longrightarrow\quad
\mathbf{M}^{\top}\mathbf{M}\hat{\boldsymbol\theta}=\mathbf{M}^{\top}\mathbf{x}
$$

（后端用 `numpy.linalg.lstsq`。）由 $\hat a,\hat b$ 恢复幅值与相位：

$$
A=\sqrt{a^{2}+b^{2}},\qquad \varphi=\mathrm{atan2}(b,a)
$$

- 直流偏置由 $\hat c$ 直接给出，**无需预处理去直流**；
- 残差 RMS 作为该频率的噪声 / 失真指标：

$$
r[k]=x[k]-\hat a\sin\omega t_k-\hat b\cos\omega t_k-\hat c,\qquad
\sigma_{r}=\sqrt{\tfrac{1}{N}\sum_{k}r[k]^{2}}
$$

相较 FFT 取单 bin 的做法，正弦拟合在已知频率下**无频谱泄漏、无需加窗、相位干净**，且在 AWGN 下是最大似然估计。

### 2. 复阻抗推导

对 V、I 两路分别做 §1 的拟合，得到 $(V_{\text{amp}},\varphi_{v})$ 与 $(I_{\text{amp}},\varphi_{i})$。阻抗即两路相量之比：

$$
Z(\omega)=\frac{\dot V}{\dot I}=\frac{V_{\text{amp}}}{I_{\text{amp}}}e^{j(\varphi_{v}-\varphi_{i})}=|Z|e^{j\theta}
$$

$$
|Z|=\frac{V_{\text{amp}}}{I_{\text{amp}}},\qquad \theta=\angle Z=\varphi_{v}-\varphi_{i}
$$

注意 V、I 必须使用**同一相位约定** $\varphi=\mathrm{atan2}(b,a)$，否则 $\theta$ 出错。转换为直角坐标：

$$
Z=R+jX,\qquad R=|Z|\cos\theta,\qquad X=|Z|\sin\theta
$$

损耗因数与品质因数：

$$
D=\left|\frac{R}{X}\right|,\qquad Q=\frac{1}{D}
$$

由电抗符号判定容性 / 感性并反算等效元件值（$\omega=2\pi f$）：

$$
X<0\ (\text{容性}):\ C_{\text{eq}}=-\frac{1}{\omega X},\qquad
X>0\ (\text{感性}):\ L_{\text{eq}}=\frac{X}{\omega}
$$

等效串联电阻 $\text{ESR}=R$。

### 3. 频域等效电路拟合

跨整个扫频 $\{(f_{m},Z_{m})\}_{m=1}^{M}$，用集总模型 $Z_{\text{model}}(\omega;\mathbf p)$ 拟合测量阻抗。支持的拓扑及其阻抗表达式（见 `app/dsp/circuit_fit.py`）：

| 模型 | $Z_{\text{model}}(\omega)$ |
|---|---|
| 串联 RLC | $R+j\left(\omega L-\dfrac{1}{\omega C}\right)$ |
| 串联 RC | $R-\dfrac{j}{\omega C}$ |
| 串联 RL | $R+j\omega L$ |
| 并联 RLC | $\dfrac{1}{\dfrac{1}{R}+j\left(\omega C-\dfrac{1}{\omega L}\right)}$ |
| 并联 RC | $\dfrac{1}{\dfrac{1}{R}+j\omega C}$ |
| 并联 RL | $\dfrac{1}{\dfrac{1}{R}-\dfrac{j}{\omega L}}$ |

#### 3.1 log 空间参数化

R / L / C 跨越若干个数量级，直接拟合数值条件差。改在以 10 为底的对数空间优化：

$$
p_{i}=10^{\hat p_{i}},\qquad \hat p_{i}=\log_{10}p_{i}
$$

边界在对数空间给出：$R\in[10^{-3},10^{9}]\Omega$、$L\in[10^{-9},10^{3}]\text{H}$、$C\in[10^{-15},1]\text{F}$。

#### 3.2 加权的实虚部联合残差

为兼顾实部、虚部并适应 $|Z|$ 的动态范围，残差按 $|Z_{m}|$ 归一化：

$$
\mathbf r(\hat{\mathbf p})=
\begin{bmatrix}
\bigl(\Re Z_{\text{model}}(\omega_{m})-\Re Z_{m}\bigr)\big/s_{m}\\[2pt]
\bigl(\Im Z_{\text{model}}(\omega_{m})-\Im Z_{m}\bigr)\big/s_{m}
\end{bmatrix}_{m=1}^{M},
\qquad s_{m}=\max\bigl(|Z_{m}|,\ \varepsilon\bigr)
$$

最小化 $\lVert\mathbf r(\hat{\mathbf p})\rVert^{2}$，由 `scipy.optimize.least_squares`（`method='trf'`，支持上述边界）求解。

#### 3.3 拟合质量

$$
\text{RMSE}=\sqrt{\tfrac{1}{M}\sum_{m=1}^{M}\bigl|Z_{m}-Z_{\text{model}}(\omega_{m};\hat{\mathbf p})\bigr|^{2}},\qquad
\text{Accuracy}=1-\frac{\text{RMSE}}{\overline{|Z_{m}|}}
$$

并在 $[f_{\min},f_{\max}]$ 上生成对数均匀密网格（默认 200 点）计算 $Z_{\text{model}}$，作为 Bode / Nyquist 图的理论曲线叠加在测量散点上。

### 4. FFT 频谱诊断

FFT **不参与阻抗计算**，仅用于诊断（核对主频、观察谐波与噪声底）。对去直流后的序列加 Hann 窗 $w[n]$ 做单边幅度谱：

$$
X[k]=\sum_{n=0}^{N-1}w[n]x[n]e^{-j2\pi kn/N},\qquad k=0,\dots,N/2
$$

### 5. OSL 校准

模拟前端在 V、I 两通道引入不同相移，线缆引入寄生参数，必须做 **开 / 短 / 负载（OSL）** 校准。按频率存储三组标准件测量，依次校正：

$$
Z_{1}=Z_{\text{meas}}-Z_{\text{short}}\quad(\text{扣除串联引线阻抗})
$$

$$
Z_{2}=\frac{1}{\dfrac{1}{Z_{1}}-\dfrac{1}{Z_{\text{open}}}}\quad(\text{扣除并联寄生导纳})
$$

$$
Z_{\text{DUT}}=k\cdot Z_{2},\qquad k=\mathrm{median}\left(\frac{Z_{L}^{\text{true}}}{Z_{2}^{\text{load}}}\right)\quad(\text{增益 / 相位校正})
$$

> 校正算法（`app/dsp/calibration.py`）与数据模型（`CalibSet`）已就位；采集接口随模拟前端硬件定型后启用。

---

## 实现细节

> 下列描述与代码一一对应，可按文件行号核对。

### 后端数据处理流程

每收到一个频率点 `POST /api/scan/{scan_id}/point`，由 `backend/app/services/scan_service.py:add_point`（第 31 行）**同步**完成 DSP 并落库，而非查询时重算：

1. `measure_impedance(voltage, current, dt, frequency)`（`app/dsp/impedance.py`）：
   - 构造时间轴 $t_k=k\Delta t$ 与 $\omega=2\pi f$；
   - 对 V、I 各调用 `sine_fit`，用 `numpy.linalg.lstsq` 解设计矩阵 $[\sin\omega t,\ \cos\omega t,\ \mathbf 1]$，得 $(V_{\text{amp}},\varphi_v)$、$(I_{\text{amp}},\varphi_i)$、直流 $\hat c$ 与残差 RMS；
   - 按 §2 公式算出 $|Z|,\theta,R,X,D,Q,\text{ESR},C_{\text{eq}}/L_{\text{eq}}$。
2. `fft_spectrum(voltage/current, dt, window='hann')`（`app/dsp/spectrum.py`）：Hann 加窗、去直流后的单边幅度谱（`np.fft.rfft`），**仅用于诊断**，不参与阻抗。
3. 落 SQLite：`Measurement` 表存全部阻抗派生量（`z_real/z_imag/z_mag/z_phase_deg/R/X/D/Q/esr/L_eq/C_eq/v_amp/…/resid_rms_v/i_dc` 等），`RawWave` 表存原始 V/I、正弦拟合曲线、逐点残差、FFT 频率与幅度——单行自足，供后续所有视图读取。

> 改算法只需动 `app/dsp/`；上传契约、表结构、固件均无感。

### 前端图表绘制流程

所有图表由 `frontend/src/lib/charts.ts` 中的纯函数生成 ECharts option 对象（`waveformOpt / spectrumOpt / bodeOpt / nyquistOpt / complexPointOpt`），统一传入当前 `Palette`（`src/lib/palette.ts`）。约定：

- `src/components/EChart.vue` 封装 `echarts.init` + `ResizeObserver`(自适应缩放) + `setOption(option, true)`(notMerge) + 卸载时 `dispose()`；props `option` 变化即重绘。
- 每个 option 都是 Vue 的 `computed`，且依赖 `app.theme`——切换主题会重算全部 option、`EChart` 自动重绘（暗 / 亮两套色板均已通过 CVD 安全校验）。
- **V 与 I 永远是两张独立图**（单位不同 → 不共用纵轴、绝不用双轴）；**测量值 = 散点**（series 1，蓝），**理论 / 拟合 = 折线**（series 2，橙）。
- 时域图：原始采样点（散点）+ 正弦拟合曲线（折线）+ 直流电平虚线；残差图过零基准线；频谱图对数横轴 + 在激励频率 $f_0$ 处画竖虚线；Bode 横轴对数；Nyquist 横轴 $\Re(Z)$、纵轴 $-\Im(Z)$（容性弧在上半平面）。
- 数据状态集中在 `src/store/scan.ts`（Pinia），加载时自动选中最近一次扫描，分析 / 扫频 / 拟合页共享同一选中态。

### 等效电路拟合实现细节

`backend/app/dsp/circuit_fit.py:fit_circuit`（第 111 行）与 §3 公式一一对应：

- **参数化**：优化变量 $\hat{\mathbf p}=\log_{10}\mathbf p$，残差函数内 $p_i=10^{\hat p_i}$ 再代入模型。
- **边界**（对数空间）：$R\in[10^{-3},10^{9}]$、$L\in[10^{-9},10^{3}]$、$C\in[10^{-15},1]$；初值裁剪进边界内。
- **加权残差**：`np.concatenate([(Zm.real-Z.real)/s, (Zm.imag-Z.imag)/s])`，其中 $s_m=\max(|Z_m|,10^{-12})$。
- **求解器**：`scipy.optimize.least_squares(method="trf", max_nfev=4000)`（支持上下界）。
- **初值启发式**（`_initial_guess`，第 90 行）：$R_0=\mathrm{clip}(\mathrm{median}\Re Z,10^{-3},10^{8})$；$C_0$ 取自最负电抗点 $C=-1/(\omega X)$；$L_0$ 取自最正电抗点 $L=X/\omega$；缺失则回退 $L_0=10^{-3}$、$C_0=10^{-6}$。
- **质量与理论曲线**：收敛后计算 RMSE、Accuracy，并在 $[f_{\min},f_{\max}]$ 上 `np.logspace` 取 200 点，输出 `theory = {frequency, z_mag, z_phase_deg, z_real, z_imag}` 供 Bode / Nyquist 叠加。

---

## 目录结构

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
docs/api_contract.md   ← ESP32 固件按此实现上传
start.sh               ← 一键启动
```

---

## 快速开始

### 一键启动（推荐）

```bash
./start.sh            # 同时启动前后端（前台，Ctrl+C 停）
./start.sh backend    # 仅后端
./start.sh frontend   # 仅前端
./start.sh stop       # 停止（任意终端可执行，按记录端口精准清理）
./start.sh clean      # 清 vite 依赖缓存与 dist
```

`start.sh` 用 `socket.bind` 真实探测端口并自动回退（后端 8000→8003，前端 5173→5176）；真实端口写入 `/tmp` 再经 `LCR_BACKEND` 注入前端；后端监听 `0.0.0.0`，启动时打印 **ESP32 上传地址**。

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

> WSL2 上系统 `python3` 为 3.14（scipy wheel 不可靠），务必用 `lcr` conda 环境；8000 端口常被占用，可用 8001 并以 `LCR_BACKEND` 指向。

### 无硬件跑通全链路（模拟器）

```bash
cd backend && conda run -n lcr python -m app.services.simulator \
  --model series_RLC --R 50 --L 1e-3 --C 1e-6 --f-points 30 --noise 0.005
```

前端 ScanBar 的「生成示例」按钮（串联 RLC / RC / RL / RLC+谐波）也可直接合成数据。

---

## ESP32 固件对接

固件只需按 **[`docs/api_contract.md`](docs/api_contract.md)** 的契约，对每个激励频率 POST 一条原始波形，所有 DSP 与拟合都在服务器完成：

```
POST /api/scan/start                → 拿到 scan_id
POST /api/scan/{scan_id}/point      → { frequency, dt, n, voltage[], current[] }
```

文档中还写了三个精度命门：**V / I 同步采样**、**外挂 ADC**、**OSL 校准**。

---

## 验证结果

- **pytest**：正弦拟合 / 阻抗（R / C / L）/ 电路拟合（串联 RLC · RC · 并联 RC）全绿。
- **端到端**：模拟器生成串联 RLC（$R=50\Omega$、$L=1\text{mH}$、$C=1\mu\text{F}$）扫频，平台恢复 $R=49.99\Omega$、$L=1.000\text{mH}$、$C=1.000\mu\text{F}$，精度 **99.9 %**；逐频 $|Z|$ 与相位和理论值吻合。
- **前端**：`vue-tsc` 类型检查零错误，`vite build` 通过；KaTeX 方程与 ECharts 图表在浏览器实测正确渲染（CVD 安全配色，暗 / 亮主题可切换）。
