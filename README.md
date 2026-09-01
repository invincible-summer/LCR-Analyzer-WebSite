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

**不确定度传播**（重构新增）：三参数拟合的幅值/相位标准误为 $\sigma_x\sqrt{2/N}$，于是

$$
\frac{\sigma_{|Z|}}{|Z|}=\sigma_{\angle Z}=\sqrt{\Bigl(\frac{\sigma_v}{V_{\text{amp}}}\Bigr)^2+\Bigl(\frac{\sigma_i}{I_{\text{amp}}}\Bigr)^2}\cdot\sqrt{\frac{2}{N}}
$$

$\sigma_{|Z|}$ 与 $\sigma_{\angle Z}$ 随测量点入库（`z_sigma` / `z_phase_sigma_deg`），是 Bode 误差棒与频域加权拟合 $\sigma_m$ 的来源。

### 3. 频域等效电路拟合：矢量拟合 + 网络综合 + 拓扑库

频域拟合有两条互补的引擎，全部推导与实现细节见 **[docs/algorithms.md](docs/algorithms.md)**（与代码逐项同步）：

**(a) 矢量拟合（自动模式，`app/dsp/rational_fit.py`）** —— 不预设任何拓扑公式。
对 $Z(f)$ 拟合有理函数的极点-留数形式

$$
Z(s)\;\approx\;\sum_{k=1}^{N}\frac{c_k}{s-a_k}\;+\;\frac{c_0}{s}\;+\;d\;+\;s\,e,
\qquad s=j2\pi f
$$

- 极点位置由 **Sanathanan–Koerner 迭代**（矢量拟合）自动重定位，阶数 $N$ 从 1 逐级升至 $N_{\max}$，以「最小可接受阶」（$\chi^2_{\text{red}}\le4$）选择，回退 AICc；
- 不可无源实现的极点（负留数实极点、$\gamma$ 越界的极点对，含元件级噪声容差）被**剪枝**后联合重解；
- **Foster 综合**（`app/dsp/synthesis.py`）把每个极点项翻译成具体 RLC 支路：实极点 → 并联 RC，原点极点 → 串联电容，共轭对 → $\parallel\{C,\ R,\ 串RL\}$，输出**网表树**（前端 SVG 原理图）与 **SPICE `.subckt`**；
- 电路的**结构与阶数由数据决定**——双谐振网络、带外谐振节等没有任何固定拓扑能表达的电路都能恢复。

**(b) 固定拓扑库（`app/dsp/topology_fit.py`）** —— 可解释对照组，8 种模型
（串联/并联 RLC·RC·RL、`R+L∥C` 电感 SRF、`Rs+Rp∥C` 电解电容），
log10 空间 + **Latin 超立方多起点**（每拓扑 24 起点）+ σ 加权残差 + soft_l1 鲁棒损失，
并从 Jacobian 报告每个参数的 **95% 置信区间**。

**(c) 自动排名（`app/dsp/fit_auto.py`）** —— `POST /api/fit` 的 `model="auto"`：
VF 候选与全部拓扑候选用同一 σ 加权残差计算 **AICc** 排名；
$\Delta\text{AICc}\le2$ 或 $\chi^2_{\text{red}}\le4$ 视为统计不可区分，此时优先选用可解释的命名拓扑。

加权与噪声地板约定：残差按每点测量噪声 $\sigma_m$ 加权（来自时域拟合的误差传播，见 §2），
$\sigma_m$ 下限为 $|Z_m|\times10^{-5}$（~100 ppm 相对噪声地板，低于它的"噪声"是数值残差而非物理）。

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
Z_{\text{DUT}}(f)=k(f)\cdot Z_{2},\qquad k(f)=\frac{Z_{L}^{\text{true}}}{Z^{\text{load}}(f)}\quad(\text{逐频点复数增益/相位校正})
$$

$Z_L^{\text{true}}$ 为**复数**（可含相位），校正逐频点进行（未测频点线性插值）。
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
3. 落 SQLite：`Measurement` 表存全部阻抗派生量（`z_real/z_imag/z_mag/z_phase_deg/z_sigma/z_phase_sigma_deg/R/X/D/Q/esr/L_eq/C_eq/v_amp/…/resid_rms_v/i_dc` 等），`RawWave` 表存原始 V/I、正弦拟合曲线、逐点残差、FFT 频率与幅度——单行自足，供后续所有视图读取。

> 改算法只需动 `app/dsp/`；上传契约、表结构、固件均无感。

### 前端图表与设计系统

浅色"科学期刊"风格（Origin/Keysight IA 质感）：白纸面、发丝网格、扁平无辉光，
自托管 **IBM Plex Sans/Mono**（数字 `tabular-nums`），图标用 **lucide**（`@lucide/vue`）。
调色板单一来源：`src/lib/palette.ts` 与 `style.css` 的 CSS 变量镜像同一组 token。

- 图表卡片为 **`FigBlock`**（`Fig. 1` 编号 + 标题 + 图注），builder 在 `src/lib/charts.ts`：
  `waveformOpt / spectrumOpt / bodeOpt / nyquistOpt / complexPointOpt / poleZeroOpt / residualsOpt`。
- **Bode / Nyquist 带 dataZoom**（滚轮/滑块缩放平移）；Bode 幅值叠加 ±1σ 误差棒（ECharts custom series）；
  拟合页另有 **s 平面极零图**（× 极点 / ○ 零点）与 **残差-频率图**（±1σ 参考带）。
- **V 与 I 永远是两张独立图**（单位不同 → 不共用纵轴、绝不用双轴）；**测量值 = 散点**（蓝），**理论 / 拟合 = 折线**（橙）。
- `Schematic.vue` 把 Foster 网表树渲染成 SVG 原理图（IEC 符号：电阻矩形 / 电感弧 / 电容极板，并联节点画汇流轨与结点）。
- 数据状态集中在 `src/store/scan.ts`（Pinia），加载时自动选中最近一次扫描，各页共享选中态。

### 拟合实现细节（与 docs/algorithms.md 同步）

- `rational_fit.vector_fit(freqs, Z, sigma, n_max=6)`：SK 迭代 5 次 × 2 组起点（阻尼 100/10）→
  极点分类（近实数对拆成两个实极点、合并重合极点、结构性原点极点）→ 实参数化线性终解 →
  `least_squares` 非线性抛光（极点+留数联合，原点极点钉死）→ 无源性剪枝循环（判据为
  综合元件 $-R_s\le5\%R_p$，A 在比较中相消）。低阶达到噪声水平即**早停**，高阶不再计算。
- `synthesis.synthesise(rfit, f_lo, f_hi)`：输出网表树 + 密集网格无源性检查（min Re Z）+
  `spice()` 导出；带外谐振节在带内自动退化为串联电容（原点极点支路）。
- `topology_fit.fit_topology(model, freqs, Z, sigma)`：log10 空间 LHS 多起点 + 物理启发式起点
  （低频端估 L、高频端估 C）；soft_l1（f_scale = 中位 σ）；协方差 $(J^\top J)^{-1}\chi^2_{\text{red}}$
  → 95% CI 变换回线性空间；报告收敛状态。
- `fit_auto.fit_auto(freqs, Z, sigma)`：VF（含综合）+ 8 拓扑全部拟合，AICc 排序，
  平局规则偏好命名拓扑；`to_summary()` 输出排名表 JSON。
- `scan_service.fit_scan(db, scan_id, model="auto")`：σ 从 `Measurement.z_sigma` 读取
  （有非零值才用），校准后拟合，结果含网表/极零/CI/排名/SPICE 全部入库 `FitResult`。

## 目录结构

```
backend/   FastAPI + numpy/scipy + SQLAlchemy/SQLite
  app/dsp/         sine_fit, impedance, spectrum, rational_fit(矢量拟合),
                   synthesis(Foster 综合), topology_fit(拓扑库), fit_auto(排名), calibration
  app/api/         upload, results, fit, experiments, ws
  app/services/    scan_service, simulator, hub
  tests/           pytest（正弦拟合 / 阻抗 / 电路拟合）
frontend/  Vue3 + Vite + TS + ECharts + Pinia + KaTeX + markdown-it
  src/lib/         palette, format, charts(ECharts 选项库), generate(合成数据)
  src/components/  EChart, Latex, Markdown, FigBlock, Schematic(SVG原理图), StatTile, PanelStage, ScanBar
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

### 本地仪表固件（TFT 彩屏 UI）

[`ino/`](ino/) 是 ESP32 Arduino 固件：TFT 彩屏 + 4 按键 + 编码器的本地交互
（信号发生器 / 单频点复阻抗 / 幅相频扫频曲线），全部 DSP 在设备端完成；
同时保留**经典蓝牙(SPP)通路**——`ino/tools/bt_bridge.py` 把行协议数据按
api_contract 转发到本后端，前端零改动。详见 **[`ino/README.md`](ino/README.md)**。

---

## 验证结果

- **pytest**（23 项）：正弦拟合 / 阻抗（含 σ 传播的统计一致性）/ 矢量拟合与 Foster 综合
  （干净数据精确恢复、含噪数据、带外谐振信息极限）/ 拓扑库全部模型自恢复 / 置信区间覆盖真值 /
  σ 加权抑制坏点 / 自动排名选对模型。
- **端到端**（模拟器，无硬件）：
  - 串联 RLC 恢复 $R=49.99\Omega$、$L=1.000\text{mH}$、$C=1.000\mu\text{F}$；
  - **双谐振网络**（两节并联 RLC 串联，任何固定拓扑都无法表达）：`model="auto"` 选中矢量拟合，
    $\chi^2_{\text{red}}\approx10^{-13}$，综合网表恢复真实元件值，全部固定拓扑 $\chi^2\sim10^9$——排名表一目了然；
  - SPICE `.subckt` 导出可直接仿真。
- **前端**：`vue-tsc` 零错误、`vite build` 通过；期刊风格浅色 UI + 编号图卡 + SVG 原理图 + 极零图。
