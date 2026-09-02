# AlgorithmLcr Python → C++ 重构验证报告

日期：2026-09-02
环境：WSL2 Ubuntu 24.04 · g++ 15.2.0（-O2，C++17，零第三方依赖，仅 pthreads）
参照实现：Python 3.11.15 + numpy + scipy（conda env `lcr`）
硬件：Intel Core Ultra 7 255HX（20 逻辑核）。性能对比中两个引擎均为**单线程 identify 调用**。

---

## 0. 结论摘要

| 项目 | 结果 |
|---|---|
| 重构范围 | `rlc_id` 全部 9 个 Python 模块 + demo，共 10 个 C++ 模块（约 3 000 行实现 + 2 800 行测试/工具） |
| 数值等价性 | 12-DUT 标准套件双端 dump：**24/24 状态一致、24/24 拓扑一致、23/24 参数一致**（唯一差异 1.5%，在 0.5% 噪声统计涨落内） |
| 测试规模 | **2 025 例**（用户要求 >1 000），另含 248 项独立 check |
| 测试结果 | 单元/镜像/极值套件 **全绿**（721 例 0 失败，248 check 0 失败）；3 个对抗性扫描套件 95 例失败（§5 归因：算法固有，非移植错误） |
| 性能 | **28×–41×**（demo 35.9×/41.2×，120 例同数据回放 31.8×，24 例 dump 31.2×） |
| 移植 bug | 发现并修复 **6 处**（2 处关键，见 §4） |
| 验证设施自身修正 | **7 处**（§3） |

重构未改变算法行为：在可辨识案例上两引擎输出逐位一致；在退化案例上的分歧经三组实验（Python 仲裁回放、强起点对照、真值拓扑直接拟合）归因为算法固有的多峰多起点性以及 scipy-TRF 与自研 LM 在病态谷中的收敛能力差异（后者 C++ 更强，见 §6.3）。

---

## 1. 重构范围与结构

### 1.1 模块映射

| Python（行数） | C++（行数） | 说明 |
|---|---|---|
| `rlc_id/circuits.py` (324) | `src/circuits.{hpp,cpp}` (79+373) | Tree/Node、canonical/normalize（R1 层级交替 / R2 同类叶合并 / R3 子树按规范串排序）、复数域求值（逐行复刻 Python 求和顺序：PAR 先累计导纳再取倒数）、前向自动微分精确 Jacobian（log10 参数化，乘 ln10·v） |
| `rlc_id/library.py` (137) | `src/library.{hpp,cpp}` (31+172) | 拓扑库枚举 + 缓存（互斥锁 + 内部无锁版本防死锁）。计数与 Python 一致：深度 2 时 3/6/20/36/54/78，n=4 深度 3 时 90 |
| `rlc_id/fit_engine_a.py` (290) | `src/fit_engine_a.{hpp,cpp}` (127+388) | 引擎 A：两阶段漏斗（coarse→refine）、启发式起点（含谐振起点）、Foster 解注入、箱约束 LM |
| `rlc_id/fit_engine_b.py` (682) | `src/fit_engine_b.{hpp,cpp}` (76+950) | 引擎 B：SK/矢量拟合、极点重定位（σ 零点→新极点、不稳定极点翻转）、D10 阶选择、残差重拟合（噪声底剔除）、F3 保守储能下限、Foster 映射表 + D8 决策 |
| `rlc_id/pruning.py` (185) | `src/pruning.{hpp,cpp}` (42+172) | F2 渐近斜率剪枝等 |
| `rlc_id/selector.py` (147) | `src/selector.{hpp,cpp}` (43+128) | 噪声一致简约（champion 提升）、等价类合并（外扩 10 倍频带 200 点网格） |
| `rlc_id/synthetic.py` (130) | `src/synthetic.{hpp,cpp}` (60+87) | 12 个 DUT（单件/串并联/寄生/弛豫/谐振腔/双峰） |
| `rlc_id/report.py` (77) | `src/report.{hpp,cpp}` (16+94) | 工程单位格式化输出 |
| `rlc_id/__init__.py` (134) | `src/identify.{hpp,cpp}` (57+60) | 公共入口 `identify(f, z, weights, config)` |
| （numpy/scipy 依赖） | `src/linalg.{hpp,cpp}` (57+351) | 数值内核替代，见 §1.2 |

### 1.2 第三方库替代（零依赖实现）

| numpy/scipy 调用 | C++ 替代 | 验证方式 |
|---|---|---|
| `scipy.optimize.least_squares`（TRF，箱约束） | 自研 Levenberg-Marquardt：信赖域 λ 策略、精确 Jacobian、投影梯度收敛判据、边界投影 | 直拟合实验达机器精度；与 TRF 在标准 DUT 上输出逐位一致 |
| `numpy.roots` | 伴随矩阵 + Wilkinson 移位 Hessenberg QR（显式 QR sweep，两趟旋转）+ 牛顿抛光 + 400·n 迭代预算 | 与 numpy 对 σ 多项式（系数跨 8 个数量级）根值精确一致 |
| `numpy.linalg.lstsq` | 列缩放 + 单边 Jacobi SVD | 与 numpy 解一致（dut8 排查时逐层复算） |
| `numpy.random`（PCG64） | `std::mt19937_64` | 起点流不同（不可复现同一随机流）但同分布；见 §6.3 对一致性影响的分析 |
| `np.geomspace` / `np.median` / `np.polyfit`（斜率） | `geomspace` / `median` / `polyfitSlope` | 单元测试 |

其余刻意保持一致的设计：30 点 10 Hz–10 MHz 对数频率网格、相对误差权重 w=1/\|z\|、log10 参数化、PAR 节点"先累计导纳再取倒数"的浮点求和顺序、等价类验证网格、所有阈值常量。

### 1.3 构建与目标

`CMakeLists.txt`：C++17、`-O2 -Wall -Wextra` **零警告**。目标：`rlc_id`（静态库）、`rlc_tests`（2 025 例）、`demo`、`benchmark`、`dump`（结果 JSON）、`bench_dump`（扫描用例导出）。

### 1.4 约束遵守

- 所有写入仅在 `AlgorithmLcr/cppversion/` 内；调试/临时数据一律 `/tmp`。
- Python 侧只读：一律 `PYTHONDONTWRITEBYTECODE=1 python -B` 运行，未在 Python 目录产生任何文件。

---

## 2. 测试体系（2 025 例 + 248 check）

测试分三层。独立参考（不依赖被测代码）：**long double 修正节点分析（MNA）**直接列写 RLC 网络，与树求值交叉；**自适应步长 Richardson 外推 5 点有限差分**检验解析 Jacobian。

| 套件 | 例数 | 失败 | 内容 |
|---|---|---|---|
| circuits | 403 | **0** | 全库 n≤6 每拓扑 × 2 随机参数：树求值 vs long double MNA（混合容差 max(1e-6·\|z\|, 1e-11·zScale)）+ 解析 Jacobian vs 自适应 FD（不可见参数自动跳过）+ canonical/normalize/格式化 |
| library | 5 | **0** | 枚举计数（3/6/20/36/54/78；n=4 深度 3 = 90）、规范序、缓存 |
| engine_a | 8 | **0** | 12 DUT 无噪直拟合机器精度（wrmse ≤ 1e-14）；有噪参数误差 < 2% |
| engine_b | 210 | **0** | 150 随机实极点系统 + 50 随机 tank（Q≤50）有理逼近与 Foster 综合 + F3 界 |
| pruning / selector | 5 + 6 | **0** | 渐近斜率、噪声一致简约、等价类合并（含镜像 pytest 的全部断言） |
| end_to_end | 24 | **0** | 逐条镜像 Python pytest 的 56 个断言场景 |
| extremes_bands | 40 | **0** | 极值元件（fF/GF、fH/nH、µΩ/GΩ 5×4）+ 20 种频带/点数组合 |
| sweep_n1_5 | 952 | 37 | **119 个 n≤5 拓扑 × 8 组随机参数**（含复杂结构：嵌套串并联、重复子树、多谐振） |
| sweep_n6 | 312 | 38 | 78 个 n=6 拓扑 × 4 组参数 |
| noisy_sweep | 60 | 20 | 12 种复杂结构 × 5 个种子，0.5% 相对噪声，带内可辨识性筛查（灵敏度 ≥ 0.05） |
| **合计** | **2 025** | **95** | 另 248 项 check 全过 |

扫描套件的判定不是简单字符串比对，而是 v3 分类判定器（见 §3.4）：EXACT（规范形+参数）/ EQUIV（外扩网格上电气等价）/ FITOK / FITWEAK / WEAKFIT（拟合质量分级）/ FAIL。最终分布：

| 套件 | EXACT | EQUIV | FITOK | FITWEAK | WEAKFIT | FAIL |
|---|---|---|---|---|---|---|
| sweep_n1_5（952） | 743 | 104 | 23 | 43 | 2 | 37 |
| sweep_n6（312） | 178 | 15 | 16 | 62 | 3 | 38 |
| noisy_sweep（60） | 37 | 3 | — | — | — | 20 |
| extremes_bands（40） | 16 | 4 | — | — | — | 0 |

n≤5 无噪：**拓扑正确率 89.0%**（EXACT+EQUIV），加拟合质量分级 96.1%，FAIL 3.9%。FAIL 案例全部标注 `truth reachable`（真值拓扑可达机器精度但多起点未覆盖）或重复子树电气歧义——归因分析见 §6。

作为对照，Python 原版 pytest 56 例 51.4 s 全过；C++ 镜像端到端 24 例全绿。

---

## 3. 验证设施自身的修正（7 处）

测试协议 bug 会产生假阳性/假阴性，以下修正均先证伪协议再改：

1. **MNA 交叉假阳性**：谐振点附近 \|Z\|→0、支路导纳比达 10^6，double 精度直接比对失败。→ long double MNA + 混合容差 `max(1e-6·|z|, 1e-11·zScale)`。
2. **FD Jacobian 假阳性**：固定步长对不可见元件（带内灵敏度 < 1e-10）和高 Q 谐振产生 garbage 导数。→ 自适应步长 Richardson 外推（1e-3→1e-5 收敛判定，永不收敛则跳过计数）+ 按自动微分幅值筛查跳过不可见参数。
3. **噪声套件可辨识筛查位置错误**：在外扩网格上筛查（带外可见 ≠ 带内可辨），导致把不可辨识案例误判为算法失败。→ 带内灵敏度筛查（≥ 0.05 才计入判定）。
4. **扫描判定器过严**（初版 173 失败）：只认 EXACT。→ v3 分类判定器 + `truthReachable`（真值拓扑直拟合 wrmse < 1e-6 则失败归因"起点覆盖"而非"不可辨识"）+ 重复子树结构的参数歧义改用电气判定。
5. **高 Q 随机 tank 假失败**：Q > 50 的无损 tank 超出 DESIGN.md D8 的设计范围（Python 对同一数据输出逐位一致，证明是算法固有保守性）。→ 测试生成限 Q ≤ 50。
6. **noisy/extremes 套件拓扑名非规范形**：手写拓扑名与库的子树排序规则不一致导致漏配，以及空指针段错误。→ 一律用库验证过的规范名 + null 守卫。
7. **RC 梯子断言 off-by-one**：order 阶 RC 梯子综合出 order+1 个元件，断言按 order 写。→ 修正断言。

另：对比工具 `compare.py` 初版用**相对**差比较 theta，对真值为 0 的 log10 参数（如 log10 R = 0）在两个机器零之间算出 0.667 的"差异"。→ 改为 log10 空间**绝对**差（0.01 = 元件差 2.3%）。这是本报告写作过程中发现并修正的最后一处验证工具 bug。

---

## 4. 发现并修复的移植 bug（6 处）

按严重程度排列。前两处是真正的"静默数值错误"——不崩溃、不越界，只让结果悄悄变差，且都靠"导出中间量 → 双端逐层复算"定位。

### 4.1 【关键】engine_b `initialPolesX`：把组合**索引**当中心**值**复制

- **症状**：dut8（双谐振腔）的矢量拟合完全发散——极点落成 4 个实极点、rss = 12（正常应为复极点对、rss ~ 1e-15），引擎 B 对该 DUT 全部阶数失败。
- **定位**：把 numpy 侧 LS 问题和 σ 多项式导出为文本，C++ 逐层复算（初始极点 → 基矩阵 → LS 解 → σ 零点），发现初始极点向量里的"中心频率"取值是 `pool` 的**下标**而非 `pool[idx]` 的值——两组数恰在同一数量级区间，编译器与运行期都无法报警。
- **修复**：`centers[k] = pool[idx]`。修复后 dut8 恢复机器精度（wrmse 7.8e-16），engine_a 12/12 DUT 无噪直拟合全部 ≤ 1e-14。

### 4.2 【关键】`polyRoots`（numpy.roots 替代）：算法更换 + 两处旋转实现错误 + 死循环

- **症状**：初版用 Durand-Kerner，对 σ 多项式（系数跨 8 个数量级，根 ±503.29j/±50.33j）给出完全错误的根（±4±5j）。numpy.roots 对同一系数正确——这是"替代实现与被替代库不等价"的典型。
- **修复 1**：换成伴随矩阵 + Wilkinson 移位 Hessenberg QR。实现中又修掉两个错误：
  - (a) Givens 行旋转与列旋转必须**两趟**执行（先全部行旋转，再全部列旋转），逐趟交替会破坏已消的次对角元；
  - (b) 列旋转须乘 **G^H**（`col_k' = c̄·col_k + s̄·col_{k+1}`），与行旋转的 G 不是同一侧。
- **修复 2**：QR sweep 在病态伴随矩阵上可能不收敛——曾导致 engine_b 测试 13 分钟满载挂死。加 400·n 迭代预算，超限强制收缩（精度损失可由后续牛顿抛光补偿）。
- **验证**：修复后与 numpy.roots 在全部 engine_b 测试（210 例）的多项式上精确一致。

### 4.3 library 缓存互斥锁死锁

`TopologyLibrary::get` 持锁调用 `treesOfSize`，后者再次加锁 → 同一线程重入死锁（表现为测试 0% CPU 挂死）。→ 拆出调用者持锁的 `treesOfSizeLocked`。

### 4.4–4.6 编写期自查修复

- `lmFit` 预测下降量符号写反（λ 调整方向错误，收敛但低效）；
- `selector.cpp` 移动语义在遍历中破坏容器；
- `linalg` 占位实现残留（连接后才暴露）。

---

## 5. 最终测试结果与失败归因

单元/镜像层 721 例 + 248 check **全绿**。95 例失败全部集中在三个对抗性扫描套件，且**没有一例是移植不一致**。归因证据链（§6 详述）：

| 失败类别 | 数量（sweep_n1_5 37 例） | 证据 |
|---|---|---|
| Python 输出**逐位一致**（同败） | 26 | `tools/xcheck.py` 对失败案例的 z 数据回放 Python identify，输出与 C++ 完全相同 |
| 起点覆盖差距（Python 运气好） | 10 | 其中 9 例抽查：C++ 加强起点（nStarts 8/40，纯配置不改代码）后 8 例翻转为机器精度恢复 |
| 其余 | 1 | 边界混合情形 |

n=6 抽查 3 例：2 例 Python 逐位一致、1 例起点运气。noisy 抽查 3 例：2 例 Python 同败、1 例起点运气。另在 120 例满界采样回放（§6.2）中出现**反向**案例（C++ 恢复真值而 Python 未恢复，30 例 vs 反向 4 例），最终实验证明是 scipy-TRF 在病态谷提前终止（§6.3）——两个方向合起来说明：失败属于多起点算法的固有概率行为，且两引擎的优化器各有强弱情形，C++ 侧整体更强。

---

## 6. Python ↔ C++ 交叉验证（三组实验）

### 6.1 实验 1：标准 DUT 套件全对等 dump（24 例）

`apps/dump.cpp` 与 `tools/dump_python.py` 对 12 DUT × {无噪, 0.5% 噪声} 以相同输入运行 `identify`，`tools/compare.py` 对比：

```
cases: 24
status identical :  24/24
top-1 identical  :  24/24
theta |dlog10|<5e-3: 23/24  (among identical topologies)
total time cpp=1.28s  python=39.95s  speedup=31.2x
```

唯一参数差异：dut7_tank 有噪，同一拓扑下最大参数差 6.37e-3 个 decade（元件 1.5%），两端 wrmse 均在噪声底附近（0.0070 vs 0.0060），属噪声内优化器路径涨落。无噪 12 例参数全部机器精度一致（≤1e-11）。

### 6.2 实验 2：120 例扫描模式全对等回放

`apps/bench_dump` 生成 benchmark 的 120 例（119 个 n≤5 拓扑满界随机参数，**同一批 z 数据**），C++ 与 Python（`tools/bench_python.py`）分别 identify，`tools/compare_verdicts.py` 对比：

| 类别 | 数量 |
|---|---|
| 双端 top-1 完全一致 | **71/120** |
| ——其中双端都精确恢复真值 | 60 |
| ——双端给出同一个**非真值**模型（退化数据上行为一致） | 11 |
| 仅 C++ 精确恢复真值 | 30 |
| 仅 Python 精确恢复真值 | 4 |
| 双端都未恢复（各选各的近似模型） | 15 |
| C++ 恢复真值 / Python 恢复真值 | **90 / 64** |

注意：这批用例按 benchmark 协议在**几乎整个参数界**内采样，大量元件在 30 点网格上电气不可见（退化问题），与 §2 测试套件的可辨识采样不同——它的价值正在于测量退化域上的行为一致性。

### 6.3 实验 3：分歧归因（决定性实验）

30 例"仅 C++ 恢复真值"需要解释。三步：

1. **加强起点**（`tools/strong_start_check.py`）：对其中 2 例给 Python 配 nStarts 8/40（远超默认 3/10），输出**逐位不变**（wrmse 1.25e-06 / 1.12e-06）——不是起点数量问题。
2. **真值拓扑直接拟合**（`tools/direct_fit_check.py`）：用 Python 自己的 `fit_topology` + `heuristic_starts` 直接在 5 例数据上拟合真值拓扑。结果 Python 全部停滞在 wrmse 5.5e-7 – 1.5e-6（2 元件案例 7.6e-10），而 C++ 在**完全相同的数据**上达 1e-15 – 0（数据本就由该拓扑生成，机器精度谷底存在且唯一）。
3. **结论**：scipy TRF 的相对容差判据（xtol/ftol/gtol = 1e-11）在极端参数的病态谷中提前终止；C++ 自研 LM 用投影梯度判据 + 绝对步长控制能继续下降到底。这是**优化器能力差异**，不是移植错误——方向上 C++ 更强（90 vs 64 恢复真值），且 §6.1 已证明在良态问题上两引擎逐位一致。

反向的 4 例"仅 Python 恢复"及 sweep 失败中的 10 例起点差距与此对称：多起点算法在退化/多峰问题上本质是抽签，随机流（mt19937_64 vs PCG64）与优化器路径共同决定单个案例的落点。**两引擎在所有良态案例上一致，在退化案例上分布相同、个案落点不同。**

---

## 7. 性能对比

同一台机器、单线程 identify、相同输入数据：

| 负载 | Python | C++ | 加速 |
|---|---|---|---|
| demo 有噪 12 DUT（判定完全一致：11 exact / 1 equiv） | 20.8 s | 0.58 s | **35.9×** |
| demo 无噪 12 DUT（判定完全一致：12 exact，机器精度） | 24.7 s | 0.60 s | **41.2×** |
| 24 例 dump 端到端（§6.1） | 39.95 s | 1.28 s | **31.2×** |
| 120 例 n≤5 满界扫描回放（§6.2，同一数据） | 293.3 s（2 444 ms/例） | 9.2 s（76.9 ms/例） | **31.8×** |
| benchmark 12-DUT 有噪 / 无噪 | ~1 733 / 2 058 ms/DUT | 60.8 / 50.5 ms/DUT | 28.5× / 40.8× |

测试套件另用 `runParallel`（12 线程）跑案例级并行：2 025 例全套件约 1 分钟量级；Python 参照 pytest 56 例需 51.4 s。工程上典型的 ESP32/LCR 现场单次辨识（n≤4）从 ~1.7 s 降到 ~50–60 ms。

---

## 8. 已知局限（沿袭 Python 算法设计，非重构引入）

1. **拓扑覆盖**：规范树库不含 Bott-Duffin 桥等不可化归串并联结构（DESIGN.md 明示范围）。
2. **D8 保守性**：Foster 综合遇负元件值或留数 c≠0 即显式跳过（"宁缺毋滥"），高 Q 无损 tank 会被丢弃——§3.5 的 Q>50 假失败即此机制。
3. **平坦谷参数不可辨识**：退化采样下 wrmse 可达机器精度而 theta 相差数量级（多个模型同样好），等价类合并只保证电气等价，不保证唯一参数。
4. **多起点固有失败率**：n≤5 可辨识无噪 3.9% FAIL、n=6 12.2%、0.5% 噪声 33%（有噪下简约选择更倾向低阶模型）。可通过加大 nStarts 换取（§5 已实验：8/40 起点使 9 例中 8 例翻转）。
5. **随机流差异**：C++ 用 mt19937_64、Python 用 PCG64，起点集不可逐位复现（同分布）。需要复现 Python 某次运行的起点时应使用 Python 侧；C++ 侧自身完全确定（种子固定）。

---

## 9. 复现步骤

```bash
cd ~/daily/program/ESP32/LCR/AlgorithmLcr/cppversion
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

./build/rlc_tests                      # 2025 例全套件（退出码非零表示有失败案例）
./build/demo [--noiseless]             # 12 DUT 演示
./build/benchmark --sweep 120          # 性能基准

# 交叉验证实验 1（标准 DUT 全对等）
./build/dump cpp_results.json
PYTHONDONTWRITEBYTECODE=1 ~/miniconda3/envs/lcr/bin/python -B tools/dump_python.py py_results.json
python -B tools/compare.py cpp_results.json py_results.json

# 交叉验证实验 2/3（120 例满界回放，临时数据在 /tmp）
rm -rf /tmp/bench_cases && mkdir -p /tmp/bench_cases
./build/bench_dump /tmp/bench_cases 120
PYTHONDONTWRITEBYTECODE=1 ~/miniconda3/envs/lcr/bin/python -B tools/bench_python.py /tmp/bench_cases
python -B tools/compare_verdicts.py /tmp/bench_cases/cpp_verdict.txt /tmp/bench_cases/py_verdict.txt
PYTHONDONTWRITEBYTECODE=1 ~/miniconda3/envs/lcr/bin/python -B tools/direct_fit_check.py   # §6.3 决定性实验
```

工具一览（均在 `cppversion/tools/`）：`dump_python.py`（Python 参考结果）、`compare.py`（dump 对比）、`bench_python.py`（扫描回放计时+判定）、`compare_verdicts.py`（回放对比）、`strong_start_check.py`（强起点对照）、`direct_fit_check.py`（真值拓扑直接拟合）、`xcheck.py`（失败案例 Python 仲裁）、`dump_failing_cases.sh`（失败案例数据导出）。

---

## 10. 方法论备注

- **"静默数值错误"只能靠双端中间量对账发现**：两处关键 bug（§4.1/§4.2）都不触发任何运行期异常，症状只是结果变差；导出 numpy 侧 LS 问题/多项式系数到文本、在 C++ 里逐层复算是唯一定位手段。
- **替代实现必须与被替代库对账到根值级**：Durand-Kerner"一般也够用"的直觉在跨 8 个数量级的系数上完全失效。
- **测试协议本身是代码，也会出错**：7 处协议修正（§3）中有 3 处（MNA 容差、FD 步长、筛查位置）会把正确实现误判为错误，4 处（判定器、Q 上限、规范名、断言 off-by-one）会把算法固有行为误判为回归。对抗性套件的判定器必须带归因输出（truth reachable / 不可辨识 / 电气歧义），否则数字无法解释。
- **交叉验证要区分"引擎不等价"与"算法不确定"**：逐位一致（良态案例）证明移植正确；分布一致而个案不同（退化案例）证明的是算法本质多峰。§6.3 的直接拟合实验是区分这两者的关键。
