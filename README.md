# ESP32 在线 LCR 阻抗分析平台

> ESP32 采集原始 V/I 波形 → WiFi HTTP 上传 → FastAPI 数字信号处理 → 复阻抗 → **三引擎等效电路辨识（C++ → WebAssembly，浏览器本地计算）** → Vue3 期刊风可视化

一个把 ESP32 变成网页版 **LCR 阻抗分析仪**的全栈项目。两条数据流相互独立：

1. **测量流（后端）**：后端用**正弦最小二乘拟合**（而非 FFT）从原始波形求阻抗；前端呈现完整处理链路：采集 → 去直流 → 正弦拟合 → 残差 → 频谱 → 阻抗 → 扫频 Bode / Nyquist。
2. **拟合流（前端本地）**：「电路辨识拟合」页把测量数据（CSV 上传 / 示例生成 / 历史扫描导入）交给 `AlgorithmLcr/` 三个 C++17 引擎的 **WebAssembly 编译版**，在浏览器里完成 **Try1 完全未知辨识 / Try2 已知元件穷举接线 / Try3 已知拓扑参数反演**，输出 Top-K 候选电路与精美原理图——测量数据不出本机。

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
流 A · 测量流（后端计算）
ESP32 (WiFi · HTTP POST 原始 v/i 波形)
        │
        ▼
FastAPI 后端 (Python · numpy / scipy)              SQLite
  ├─ dsp/    正弦拟合 · 阻抗 · FFT · OSL
  ├─ api/    upload · results · fit · export · ws
  └─ services/  scan_service · simulator(假 ESP32) · hub(WS)
        │   JSON / WebSocket
        ▼
Vue3 + ECharts 前端（期刊风 · KaTeX 方程 · Markdown）
  时域分析 · 扫频 Bode/Nyquist · 实时监测 · 历史 · OSL 校准

流 B · 拟合流（前端本地，WASM，零后端依赖）
CSV 上传 / 示例生成 / 历史扫描导入      [ESP32 蓝牙导入：规划中]
        │
        ▼
Web Worker → lcr_wasm（AlgorithmLcr 三个 C++17 引擎的 WebAssembly 版）
  Try1 未知辨识 · Try2 已知元件 · Try3 已知拓扑
        │
        ▼
电路辨识拟合页：Top-K 候选表 + 原理图（SP）/ 图论视图（桥式）+ Bode/Nyquist 叠加
        +
使用文档（/docs）：数据格式 · 三引擎说明 · 约束总表 · 算法背景
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

### 3. 频域等效电路辨识：三引擎（C++ → WASM，前端本地）

「电路辨识拟合」页（`/fit`）内置三个引擎，源码在 `AlgorithmLcr/`（C++17、零第三方依赖、各 2000 组随机案例双端对拍），经 Emscripten 编译为单一 WASM 模块在 Web Worker 中运行（改算法源码后重跑 `frontend/wasm/build.sh` 并提交产物）：

| 引擎 | 已知先验 | 方法 | 输出 |
|---|---|---|---|
| **Try 1 · 未知辨识** | 无（可选器件总数 exact-N） | 串并联规范树库枚举（R1–R4 规则）+ 复数域加权最小二乘（前向 AD 精确 Jacobian、多起点箱约束 LM）+ SK 有理拟合/Foster 综合回传，AICc 排序等价类 | Top-K 候选电路 |
| **Try 2 · 已知元件** | 元件多重集（类型/数值/数量，电感带 DCR；数值按 ±20% 可信标称值处理） | 多重图穷举所有接线（含桥式/重边，自同构去重）+ 探针漏斗 + **有界数值精调**（头部结构 log10 空间 ±0.3 十进位，显著性守则防过拟合） | Top-K 接线（含精调值）+ SP 标注 |
| **Try 3 · 已知拓扑** | 拓扑 + 每边元件类型（图编辑器输入，节点 0/1 为端口） | 精确减支（F1–F4）+ 双重归一化 + 多起点漏斗 + 伴随法解析 Jacobian 箱约束 LM + 数据驱动升级阈值 | 唯一拟合 + 弱参数/触边界/Jacobi 秩诊断 |

统一约定：**一个电感 = L 与串联 DCR 绑定的 1 个器件（2 参数）**；结果为上三角邻接矩阵（`AlgorithmLcr/OUTPUT_FORMAT.md`），前端规约为串并联树渲染精美原理图，桥式等非 SP 结构自动切换图论视图。完整输入输出格式与约束见网站内「使用文档」与 `AlgorithmLcr/INPUT_FORMAT.md`。

针对实测含噪数据的鲁棒性（2026-09 优化，10 轮论证与基准回归全记录见 [`AlgorithmLcr/OPTIMIZATION_LOG.md`](AlgorithmLcr/OPTIMIZATION_LOG.md)）：Try1 的排序改为"数据驱动噪声底 + ρ 体系制门控 + 足够好集合内最少参数"（抑制实测系统误差导致的过拟合霸榜）；Try2 新增有界数值精调（元件容差不再直接变成拟合误差）；三引擎均含离群点稳健重拟合（IRLS 单遍，5σ 降权）。随机合成套件（n=1000，噪声 0–3% + 野点）：Try1 pass@1 77→83%、Try2 19→86%、Try3 89→95%；四组实测扫频数据全部达到理论可达误差。基准工具：`frontend/wasm/bench/`（real4 验收门 + suite 随机套件 + 探针）。

> 后端另有一套旧拟合引擎（矢量拟合 + Foster 综合 + 8 种固定拓扑，`app/dsp/fit_auto.py`，推导见 [docs/algorithms.md](docs/algorithms.md)）——代码与 API 保留，前端已无入口。

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
AlgorithmLcr/          三个拟合引擎的算法源（C++17 零依赖 cppversion + Python 参考 + DESIGN.md）
                       Try1 完全未知辨识 · Try2 已知元件 · Try3 已知拓扑
                       INPUT_FORMAT.md / OUTPUT_FORMAT.md = 输入输出唯一权威规范
backend/   FastAPI + numpy/scipy + SQLAlchemy/SQLite（测量流）
  app/dsp/         sine_fit, impedance, spectrum, rational_fit(旧VF), synthesis, topology_fit, fit_auto, calibration
  app/api/         upload, results, fit, experiments, ws
  app/services/    scan_service, simulator, hub
  tests/           pytest（正弦拟合 / 阻抗 / 电路拟合）
frontend/  Vue3 + Vite + TS + ECharts + Pinia + KaTeX + markdown-it
  wasm/            WASM 构建层：glue(C ABI→JSON) + CMakeLists + build.sh + 原生/Node 测试
  src/wasm/        编译产物 lcr_wasm.{js,wasm}（提交入 git，克隆即可运行）
  src/lib/         palette, format, charts, csv, adjacency(SP规约), fitTypes(WASM契约),
                   lcrWasm(worker客户端), synthData(示例), schematic, graphLayout, docToc
  src/workers/     fitWorker（module worker，加载 WASM）
  src/components/  EChart, Latex, Markdown, FigBlock, Schematic(原理图), GraphEditor(拓扑编辑器),
                   GraphSchematic(图论视图), HelpBubble, Tabs, StatTile, PanelStage, ScanBar
  src/views/       AnalysisView, SweepView, FitView(三引擎拟合), DocsView(使用文档), LiveView, CalibrateView, HistoryView
  src/docs/        使用文档源（markdown，?raw 导入）
DESIGN.md              ← 项目架构与数据契约权威规范
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

- **拟合流（三引擎）**：
  - WASM glue 原生测试 21 项（三引擎已知电路回收至机器精度 + 全错误路径），Node 烟测同 ABI；
  - 前端 vitest 25 项（CSV 解析容错 / 邻接矩阵 SP 规约含桥式判否 / 文档 TOC / 示例合成数值）；
  - Playwright 端到端 20 项（示例生成 → 三引擎全跑通 → 电路图与叠加图 → CSV 上传 → 文档页）；
  - 引擎本体：各 2000 组随机案例 py↔cpp 对拍（详见各 DESIGN.md）。
- **测量流 pytest**（23 项）：正弦拟合 / 阻抗（含 σ 传播的统计一致性）/ 矢量拟合与 Foster 综合
  （干净数据精确恢复、含噪数据、带外谐振信息极限）/ 拓扑库全部模型自恢复 / 置信区间覆盖真值 /
  σ 加权抑制坏点 / 自动排名选对模型。
- **端到端**（模拟器，无硬件）：
  - 串联 RLC 恢复 $R=49.99\Omega$、$L=1.000\text{mH}$、$C=1.000\mu\text{F}$；
  - **双谐振网络**（两节并联 RLC 串联，任何固定拓扑都无法表达）：`model="auto"` 选中矢量拟合，
    $\chi^2_{\text{red}}\approx10^{-13}$，综合网表恢复真实元件值，全部固定拓扑 $\chi^2\sim10^9$——排名表一目了然；
  - SPICE `.subckt` 导出可直接仿真。
- **前端**：`vue-tsc` 零错误、`vite build` 通过；期刊风格浅色 UI + 编号图卡 + SVG 原理图。
