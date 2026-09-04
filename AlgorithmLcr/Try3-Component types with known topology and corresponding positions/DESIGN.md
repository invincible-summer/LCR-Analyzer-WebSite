# 已知拓扑与元件类型的单端口网络参数反演算法 — 设计文档（Try3）

版本 v1.0 · 2026-09-03 · 状态：**P1 已实现并验证**（93 项单元测试全绿；
2000 组随机 DUT 战役（seed 20260903，含 E 段最后手段救援）：拟合达噪声底率 **99.95%**（1999/2000，唯一残留为已定性的极值播种信息边界），良态案例参数中位误差 0.11%）

> 目标读者：审查者 + 后续维护者。本文档记录每一次设计决策（标记为 **决策 Dn**）、
> 数学推理与合理性论证。所有公式与 `topofit_id/` 代码逐项对应；改算法先改本文档。
> 误差度量逐式沿用 Try1（`../Try1-Completely unknown single port fitting/DESIGN.md`
> §5.1/§8.1）；图模型与节点分析约定沿用 Try2
> （`../Try2-Known component types, quantities and parameters/DESIGN.md` §3/§5）。

---

## 0. 摘要

**输入**：
1. 不同频率正弦激励下测得的复阻抗离散点 $\{(f_k, \hat z_k)\}_{k=1}^{M}$；
2. DUT 的**多重图拓扑** $G=(V,E)$：节点为支路连接点，编号 0 与 1 为端口，
   每条边已知元件类型 $k_e \in \{R, C, L\}$（允许重边，重边类型可不同）。

**输出**：每条边（电感为 $L$ 与串联直流电阻 $R_d$ 两参数）的最可能数值、
拟合优度（Try1 度量）、以及可辨识性诊断。

**先验对比**（三个 Try 的分工）：

| | 元件类型 | 元件数量 | 拓扑 | 数值 | 本课题 |
|---|---|---|---|---|---|
| Try1 | 未知 | 未知 | 未知（限串并联规范树） | 未知 | 完全盲拟合 |
| Try2 | 已知 | 已知 | 未知（全多重图枚举） | **已知** | 免拟合拓扑识别 |
| **Try3** | **已知（逐边）** | 由图给定 | **已知（可含重边/桥式）** | **未知** | **连续参数反演** |

**核心结论**（后文逐一论证）：

1. 拓扑与类型已知时，剩余搜索空间是**连续的** $p = n_R + n_C + 2n_L$ 维正参数
   空间（对数尺度），无需任何离散枚举；问题化为带箱约束的加权复数最小二乘
   （§2、§5）。
2. 存在一类**与数值无关的精确减支规则**（自环/悬空/断连删除、同型并联合并、
   度-2 节点同型串联合并、$R$ 串联吸收进电感 $R_d$），它们把结构上不可辨识的
   参数在拟合前剔除（§3，定理 T2）。
3. 即便拓扑已知，参数仍可能**联合不可辨识**（雅可比秩亏，如 5 元件桥式仅一阶）
   ——算法显式输出秩/条件数诊断，而不是静默输出伪唯一解（§2.3 定理 T3，
   Try1 T2"拓扑不可辨识"在参数层的对应物）。
4. 高 $Q$ 谐振使目标景观出现指数窄的谷（相对宽度 $\sim 1/(2Q)$），纯随机多起点
   会以 ~1% 概率失手；谐振检测种子 + 拓扑感知 LC 配对种子 + 阻尼同伦救援 +
   E 段最后手段混合重启（多尺度盆地跳跃）把失手率压到 0.05%（1/2000，
   §5.3/§5.4，战役实测）。

---

## 1. 问题形式化

### 1.1 数据模型

与 Try1 §1.1 相同：$\hat z_k = Z(j\omega_k) + \varepsilon_k$，
$\varepsilon_k$ 复高斯，$\sigma_k \approx \sigma_0 |\hat z_k|$（相对噪声模型 A3，
默认 $\sigma_0 = 0.5\%$）；频带 $10\,\mathrm{Hz} \sim 10\,\mathrm{MHz}$、
30 个对数等距频点（与 Try1/Try2 合成协议一致，保证课题间难度可比）。

### 1.2 已知量与未知量

- 已知：图 $G$（含每边类型）；端口节点 0/1。
- 未知：边权 $v_e > 0$（$R$/$C$ 边 1 个；$L$ 边为 $(L_e, R_{d,e})$ 2 个，
  $L_e > 0$、$R_{d,e} > 0$——电感建模为理想电感与理想电阻串联）。
- 元件取值域（物理先验，与 Try1 §4.3 搜索箱一致）：
  $R \in [10^{-3}, 10^{7}]\,\Omega$，$L \in [10^{-10}, 10^{1}]\,$H，
  $C \in [10^{-13}, 10^{-3}]\,$F，$R_d \in [10^{-6}, 10^{7}]\,\Omega$。

### 1.3 目标

最小化 Try1 §1.3 的加权复残差（拓扑固定，无模型选择维度上的拓扑竞争；
若给出多个候选图，用 AICc 跨图排序，§5.6）：

$$
\chi^2(\theta) = \sum_{k=1}^{M} \frac{\big|\hat z_k - Z_G(j\omega_k;\theta)\big|^2}{|\hat z_k|^2},
\qquad \theta = (\log_{10} v_1, \dots, \log_{10} v_p).
$$

---

## 2. 数学基础

### 2.1 T1 — $Z_G(s)$ 是参数的有理函数（继承）

任意 RLC 单端口 $Z(s) = N(s)/D(s)$ 为正实有理函数（Brune 1931；Try1 §2.1）。
固定拓扑后，$N, D$ 的系数是元件值的多项式，因此 $\chi^2(\theta)$ 在
$\theta$（对数参数）上是**解析**函数——这保证：
(a) 局部极小点处一阶最优性条件可精确求解；
(b) 解析 Jacobian 存在且可由伴随法以近零成本获得（§5.2）。

### 2.2 T2 — 精确减支定理（值无关的结构归约）

**规则集**（对任意正值元件都精确保持 $Z$ 不变；证明为初等串并联/节点分析）：

| 编号 | 规则 | 条件 | 等价式 |
|---|---|---|---|
| F1a | 自环删除 | $u=v$ | 端压恒 0 ⟹ 电流恒 0 |
| F1b | 断连删除 | 边不在端口连通分量 | 与端口无电流通路 |
| F1c | 悬空删除 | 非端口节点度 1 | 该支路电流恒 0（KCL） |
| F2 | 同型并联合并 | 同节点对、同为 $R$（或 $C$） | $G = G_1+G_2$（$C = C_1+C_2$） |
| F3 | 同型串联合并 | 度-2 内部节点、同类型 | $R=R_1{+}R_2$；$\tfrac1C=\tfrac1{C_1}{+}\tfrac1{C_2}$；$L,R_d$ 分别相加 |
| F4 | $R$ 串入 $L$ 吸收 | 度-2 内部节点、类型 $\{R,L\}$ | $R+(R_d{+}sL)= (R{+}R_d)+sL$ |

**不可合并的边界**（诚实声明）：
- 并联 $L\|L$：$y = \tfrac{1}{R_{d1}+sL_1}+\tfrac{1}{R_{d2}+sL_2}$ 为二阶，
  不存在单边 $(L, R_d)$ 等价（保持为两条边，参数仍可辨识，仅标签可换）；
- 串联 $R{+}C$、$L{+}C$、$R\|L$、$R\|C$ 等均无单边原语等价。

**定理（减支不动点的正确性）**：规则 F1–F4 中每一步都保持
$Z_G(s;\theta)$ 对**一切**正值参数恒等，故归约与数值无关、可在拟合前执行；
合并群以表达式树记录（`graph.eval_group`），群内成员单独的数值是
**结构不可辨识**的——只有群的聚合值被 $Z$ 决定。
*测试*：`tests/test_graph.py::test_reduction_preserves_z_random`
（30 组随机图，归约前后 $Z$ 相对偏差 < 1e-6；实测通常 < 1e-12）。

**额外收益（实测发现）**：归约同时改善节点方程组的数值条件——随机图上
未归约 $Y$ 阵的条件数可达 $10^7$，悬空支路是主要病源；归约后直接求
解即可（战役中无一次求解失败）。

### 2.3 T3 — 已知拓扑 ≠ 参数可辨识（秩亏定理）

**命题**：$Z_G(s)$ 的 McMillan 阶 $n \le n_L + n_C$（Try1 §2.1）；固定拓扑的
可辨识连续自由度至多 $\dim = 2n + 2$（有理函数系数空间）。若参数数
$p > \dim$ 的独立可达维度，则存在**精确等价流形**，参数向量沿流形移动
不改变任何频率的 $Z$。

**典型例（命名 DUT `bridge`）**：4 $R$ + 1 $C$ 的惠斯通电桥，$n=1$（仅一个
储能元件）⟹ $Z$ 一阶有理函数仅 3 个自由度，而 $p=5$：存在 2 维精确等价族。
战役实测：曲线拟合到机器精度（wRMSE ~1e-15），参数却与真值差 30 倍——
**这不是失败，而是问题本身的不可辨识**。

**算法处置**：在解处对加权 Jacobian 做 SVD，输出数值秩与条件数
（`fit.FitResult.jac_rank/jac_cond`）；秩亏时显式声明
"参数向量联合不可辨识，仅秩个组合被数据决定"。**宁缺毋滥**（与 Try1
决策 D8 同一原则）。

**局部可辨识的判据**（Ljung & Glad 1994 意义下的局部结构可辨识）：
解处 $J$ 满列秩且条件数有限 ⟹ 局部唯一；$\mathrm{cond} > 10^4$ 时参数
不确定性由 Cramér–Rao 界主导（$\sigma_{\theta_i} \propto \mathrm{cond}$ 量级），
诊断输出提示"参数弱确定"。

### 2.4 T4 — 带内可辨识（弹性系数判据）

参数 $t$ 的**弹性** $E_{t,k} = \partial \ln Z_k / \partial \ln v_t$
（`nodal.NodalModel.elasticity`，解析计算）。判据：
$\max_k |E_{t,k}| < 0.1$ ⟹ 参数在带内**弱可见**（变化一个 e-fold 引起的
曲线变化 < 10%，淹没在噪声下）——拟合值无意义，输出 `weak` 标记。
理论依据：CRLB $\sigma(\ln v_t) \gtrsim \sigma_0 / (\sqrt{M}\,\max_k|E_{t,k}|)$；
带外转折频率（如 $f_c = R_d/(2\pi L)$ 远离频带）是弱可见的典型成因。
*实例*：`tests/test_fit.py::test_weak_param_flagged_for_out_of_band_element`
（$R \|$ 大 $C$：带内 $C$ 主导、$R$ 不可见被正确标记）。

### 2.5 T5 — 电学等价实现的参数歧义（离散对称）

同一 $Z$ 可由**电学等价的不同实现**给出（Foster/Cauer 对偶、Y-Δ、以及同位
同型边的标签置换）。减支规则消除其中与数值无关的一类（T2）；与数值有关的
等价（如二阶节的 Foster I/II 对偶实现）在参数层表现为**离散多解**：
拟合收敛到等价族中的某一成员。战役实测其发生率约 1.8%（良态案例中
参数误差 > 30% 而曲线误差在噪声级的案例），此时**曲线级成功**（本问题的
正确成功判据，Try1 T2 的立场），参数值是等价族的代表元。
同位（同节点对、同类型）边群的标签置换由 `metric.matched_group_errors`
在评估时排序对齐（如并联双 $L$）。

---

## 3. 图模型与减支管线

### 3.1 数据表示

- 输入 `edges = [(u, v, kind), ...]`（多重边合法：同 $(u,v)$ 可重复且类型不同）。
- 归约工作对象 `_WEdge`：端点 + 结果类型 + **聚合表达式树**
  （叶 `("e", i, k)`；内结点 `("ser"|"par", children, kind)`）。
- 输出 `ReductionResult`：精简边列表（每边 = 一个可辨识"群"）、
  被删边及原因、不动点迭代步数。

### 3.2 算法与终止性

对 F1a→F1b→F1c→F2→F3/F4 逐遍扫描，任一规则触发即重复，直到一遍无变化。
每条规则使边数严格减 1 或不变 ⟹ 至多 $O(E)$ 遍终止；每遍 $O(E\log E)$
（并查集 + 排序分组），总计 $O(E^2 \log E)$，$E \le 12$ 时微秒级。

**合流性（confluence）说明（诚实声明）**：经典串联-并行归约理论保证
2 端点 SP 归约的合流；本规则集是其带类型子集。我们不给出完整合流证明，
而以 **30 组随机图的 Z 不变性数值测试**锁定正确性
（`test_reduction_preserves_z_random`）；归约产物中边的顺序不影响后续
节点分析（ stamps 与边序无关）。

### 3.3 群聚合与报告

`eval_group` 递归求值聚合树：串联 $R$ 求和、并联 $R$ 电导求和、串联 $C$
倒数值求和、并联 $C$ 求和、串联 $L$：$L$ 与 $R_d$ 分别求和且 $R$ 成员
计入 $R_d$（F4 吸收的闭合）。拟合只在群上做；报告时逐条原始边给出
所在群、聚合值与合并说明，被删边给出原因（`fit.EdgeReport`）。

---

## 4. 阻抗求值与解析 Jacobian（伴随法）

### 4.1 节点分析（与 Try2 §5 同一约定）

节点 0 接地；每条边 $(u,v)$ 导纳 $y$ 按
$Y_{uu}{+}{=}y,\,Y_{vv}{+}{=}y,\,Y_{uv}{-}{=}y,\,Y_{vu}{-}{=}y$ 压印；
端口节点 1 排在约简矩阵首位，注入单位电流：

$$
Z(s) = \big(Y_\mathrm{red}^{-1}\big)_{00}.
$$

边导纳（$R_d{+}sL$ 为电感串联支路）：

$$
y_R = \tfrac1R,\qquad y_C = sC,\qquad y_L = \frac{1}{R_d + sL}.
$$

重边在矩阵单元内自然相加，因此**并联异型重边不需要任何特殊处理**
（Try1 规范树 R2 明确禁止的结构在节点分析里免费获得）。

### 4.2 伴随法 Jacobian（决策 D2）

设 $x = Y_\mathrm{red}^{-1} e_0$（求 $Z$ 时已解出）。边 $e$ 的参数 $\theta_t$
只通过 $y_e$ 进入 $Y$，且压印矩阵仅在 $e$ 的 2×2 块非零，故

$$
\frac{\partial Z}{\partial \theta_t}\Big|_k
= -\,\frac{\partial y_e}{\partial \theta_t}\Big|_k\,
\big(x_{r_i} - x_{r_j}\big)^2 ,
\qquad x_{\text{地}} := 0 .
$$

**成本**：每频次一次批量 LU $O(M V^3)$ 之后，整张 $p \times M$ Jacobian
仅 $O(Mp)$——精确到机器精度且近乎免费（Director & Rohrer 1969 的伴随
灵敏度；Try1 §5.2 前向 AD 的图版对应物）。相对有限差分（$p$ 次完整求值
且高偏导通道上数值消零）与复步长（要求实值响应，对本问题失效，Try1
已记录）优势明确。*测试*：`test_nodal.py::test_jacobian_matches_finite_differences`
（混合绝对/相对容差，因 FD 本身在 $|J|$ 微小通道上失真）。

对数参数 $\theta = \log_{10} v$ 下的边导纳导数（$\lambda = \ln 10$）：

$$
R:\ \tfrac{\partial y}{\partial\theta} = -\lambda y;\quad
C:\ \tfrac{\partial y}{\partial\theta} = \lambda y;\quad
L:\ \tfrac{\partial y}{\partial\log L} = -\lambda\, sL\, y^2,\ \
\tfrac{\partial y}{\partial\log R_d} = -\lambda\, R_d\, y^2 .
$$

弹性系数（T4 判据用）：$E_t = \dfrac{\partial\ln Z}{\partial\ln v_t}
= \dfrac{1}{\lambda}\,\dfrac{1}{Z}\dfrac{\partial Z}{\partial\theta_t}$
（值因子相消；实现曾因多乘 $v_t$ 出错，由
`test_elasticity_matches_finite_differences` 抓获并回归锁定——记录在案）。

### 4.3 奇异性处理

内部 LC 回路的精确谐振（测度零）或病态 $Y$：批量 `solve` 失败时逐频次
`lstsq` 兜底，仍失败的频点以 $10^{12}\cdot|\hat z|$ 级大阻抗替代（有限
残差引导优化器离开）；实测 2000 组零失败。

---

## 5. 拟合流程（决策汇总）

### 5.1 双重归一化（决策 D1）

$$
\tilde s = s/\omega_0,\quad \omega_0 = \exp\langle\ln\omega_k\rangle;\qquad
\tilde z = z/z_0,\quad z_0 = \exp\langle\ln|\hat z_k|\rangle ;
$$
$$
\tilde R = R/z_0,\qquad \tilde L = \omega_0 L / z_0,\qquad
\tilde C = z_0\,\omega_0\, C,\qquad \tilde R_d = R_d / z_0 .
$$

Try1 决策 D7a（频率归一化）的推广：再加阻抗尺度归一。归一化后"单位元件"
（$\tilde v = 1$）在带中产生 $O(1)$ 阻抗 ⟹ **单位起点**（全部 $\log_{10}\tilde v = 0$，
$\tilde R_d = 0.1$）成为普适的聪明初值，且把跨 12 个十进位的参数箱拉平，
TRF 的信赖域几何对各案例一致。物理箱界 $[10^{-3},10^{7}]\,\Omega$ 等按同一
尺度变换。

### 5.2 求解器（决策 D3）

`scipy.optimize.least_squares(method="trf")`（Branch–Coleman–Li 信赖域反射，
箱约束最小二乘），残差为 Try1 §5.1 的交错实虚堆叠
$[\operatorname{Re} r_1, \operatorname{Im} r_1, \dots]$，权重 $w_k = 1/|\hat z_k|$；
Jacobian 用 §4.2 解析式（同一 $\theta$ 处的 residual/Jacobian 共享一次求值，
`fit._Objective` 记忆化）。两阶段容差：粗筛 `1e-8` / 精修 `1e-13`。

### 5.3 多起点漏斗（决策 D4）

| 阶段 | 起点 | 数量（默认） |
|---|---|---|
| A | 单位起点 + 全箱 LHS + 中心箱 LHS（±2 dec） + 谐振种子 + 拓扑谐振种子 | 1+8+8+动态 |
| B | 围绕当前最优的高斯扰动（$\sigma = 1$ dec） | 12 |
| B2 | 围绕当前最优的 (L,C) 配对谐振对准重启 | 动态（≤32） |
| C | 升级轮：仍高于阈值时交替 全箱/中心箱 LHS + B2 | 3 轮 × 16 |
| D | 阻尼同伦救援（仍高于阈值时） | 2 |
| E | 最后手段（仍高于阈值时）：全箱 LHS + 中心箱 LHS + 围绕当前最优的多尺度扰动（σ = 2/1/0.5 dec，逐轮更新 incumbent 的盆地跳跃）+ B2 | 3 轮 × (24+24+3×12+动态) |
| 精修 | 前 3 名紧容差重跑 | 3 |

LHS 采样依据 McKay et al. (1979)（Try1 同款）；中心箱的动机：双重归一化后
真值通常落在 $\pm 2$ 个十进位内，中心箱把命中密度提高约 $(10/4)^p$ 倍。

### 5.4 谐振种子与同伦救援（决策 D5，战役驱动的设计修正）

**问题**：高 $Q$ 谐振的目标谷在对数空间中指数窄——单并联 tank 的
$|Z|$ 峰相对宽度 $\sim 1/Q$，$Q$ 可达 $10^4$+（战役 seed 20261508：
$Q \approx 3.5\times10^4$，17+8 个 LHS 起点全部失手，wrmse 卡在 0.56）。

**三层修复**（逐步引入、逐层验证）：

1. **谐振检测种子（A 段）**：$|\tilde z|$ 带内内部极值（相对中位数显著度
   ≥ 4×，至多 2 个）$\Rightarrow \tilde\omega_0$；对每个 $(L_i, C_j)$ 参数对
   生成 $\tilde L_i \tilde C_j = 1/\tilde\omega_0^2$ 的种子
   （`fit._resonance_starts`，Try1 §5.4 谐振启发的图版推广）。
2. **拓扑感知配对（A 段）**：同节点对的 $L\|C$（tank）与度-2 节点的
   $L{+}C$（串联谐振）是图上可直接识别的谐振模式，同样满足
   $\omega^2 = 1/(LC)$；对 2 个检测谐振枚举"两对同时对准"的组合种子
   （`fit._topo_resonance_starts`）。
3. **阻尼同伦（D 段）**：把所有 $R_d$ 下界逐级抬高
   （$\log_{10}\tilde R_d \ge 1, 0, -1, -2$）依次求解：高阻尼 = 低 $Q$ =
   平滑景观（无窄谷），阻尼解热启动下一级，最终回到真实下界
   （`fit._homotopy_rescue`；连续法/同伦的标准思路）。

**E 段（决策 D4a，2026-09-03 验证战役驱动）**：C/D 后仍卡在错误盆地的罕见案例
（战役实测 ~0.15%，多为多 tank + 弱确定复合病态）需要更大的**混合**预算才能移动：
全局 LHS 重抽提供多样性的盆地入口，围绕 incumbent 的多尺度扰动提供盆地跳跃
（不同 σ 覆盖不同宽度的势垒），B2 乘积对齐提供谐振对准。实测把可救案例
（seed 20261995 / 20261441 / 7007 类）从卡死救回噪声底；仅在 wrmse > 阈值时触发，
正常案例零开销（中位耗时不变）。**原理性残留**：当真值 tank 的谐振不在 |z| 数据
极值处（多储能网络的极值 ≠ 单 tank 频率）且真值盆地与所有可达盆地间无下坡路径时
（战役 seed 20262534 类），任何基于极值播种 + 随机重启的方法均不可达——根治需要
从数据有理拟合提取极点/零点做种子（Try1 引擎 B 思路，P2 扩展）。

**升级触发阈值** 0.03：$0.5\%$ 噪声的底是 $\sqrt2\sigma_0 \approx 0.0071$，
3×底 ≈ 0.021；0.03 容忍轻微超底同时避免对已达底的案例空转。

### 5.5 拟合优度与跨图排序（决策 D6）

单图：wRMSE、最大相对误差、RSS（Try1 §8.1 逐式相同）。
多候选图（`identify_many`）：AICc（$n_{obs}=2M$，$K=p+1$）升序——
$p$ 随图不同，AICc 的复杂度惩罚必不可少；战役级测试
`test_identify_many_ranks_true_first` 锁定"真图 vs 类型对换图"的排序行为
（两者可能同处噪声底，RSS 之差是噪声运气，AICc 才是正确判据）。

### 5.6 诊断输出（决策 D7）

- `weak_params`：带内弱可见参数（T4）；
- `at_bound`：触界参数（退化为开路/短路的信号，Try1 F5 的对应物）；
- `jac_rank / jac_cond`：联合可辨识性（T3）；
- 群合并与删边报告（T2）；
- `z_model(f)`：任意频率的拟合曲线（含带外外推，供 Bode 比对）。

---

## 6. 复杂度分析

设 $V$ = 节点数，$E$ = 边数，$p \le 2E$ 参数数，$M$ 频点数，$S$ 起点数，
$I$ 每次粗筛的 TRF 迭代数（经验 $\le \max(120, 25p)$ 次函数求值）。

| 步骤 | 复杂度 | 说明 |
|---|---|---|
| 减支 | $O(E^2 \log E)$ | $E \le 12$：微秒级 |
| 单次残差+Jacobian | $O(M V^3 + M p)$ | 批量 LU + 伴随回代 |
| 单起点粗筛 | $O(I\,(M V^3 + M p + M p^2))$ | $M p^2$：TRF 的 QR 子问题 |
| 全流程 | $O(S\,I\,M(V^3 + p^2))$ | $S$ 中位 29、最大 ~137 |

代入本课题默认规模（$V \le 7, E \le 8, p \le 13, M = 30$）：
单次求值 ~0.2 ms（numpy 批量化），单案例全程**中位 113 ms、p90 0.68 s、
p99 2.0 s**（2000 组战役实测，WSL2 / 20 核 / 单进程计时）。

对比：若不用解析 Jacobian 而用数值差分，单起点成本 ×$(p{+}1)$ ≈ ×14；
若不减支，病态 $Y$ 还会触发更慢的兜底路径。归一化把 TRF 迭代数本身
降低（条件数从 $10^{7}$ 级降到 $10^{2}$ 级，实测 ladder cond=90、
double_tank cond=2×10³）。

**渐近行为**：$M V^3$ 主导 ⟹ 节点数增长是唯一的超线性瓶颈；$V = 10$
（$k = 9$）时单次求值仍 < 1 ms，预计单案例 ~0.5–2 s。

---

## 7. 可行性论证

1. **问题适定**：拓扑与类型已知 + 正值箱约束 + 加权残差（相对噪声模型）
   ⟹ $\chi^2$ 连续且在紧箱上有下界，全局极小存在；数据信息量条件
   $2M \ge 2p$（$M = 30 \ge p \le 13$ ✓，且 Try1 A2 的 $M \ge 4n$ 频点
   覆盖建议同样适用）。
2. **局部极小的实践可控性**：$\chi^2$ 非凸，但三层证据支持多起点策略足够：
   (a) 良态结构上单位起点 + 少量 LHS 几乎必中（wrmse 中位 0.0069 ≈ 底）；
   (b) 病态景观的成因已被识别并针对性覆盖（高 Q → 谐振种子；多 tank →
   拓扑配对；深谷 → 同伦；错误盆地 → E 段混合重启），2000 组仅 1 例
   （0.05%，E 段后；v1 为 3 例 0.15%）未达 3× 底；
   (c) 未达底案例从真值出发均可收敛（可达性实验），即失败源于种子覆盖
   而非方法缺陷，且这 3 例均为秩亏/近简并结构（T3 类），其曲线级输出
   本就只有族代表元意义。
3. **统计最优性**：加权复残差 + 高斯假设下 TRF 解是 ML 估计；良态案例
   参数中位误差 0.11%、p90 0.86%，与 CRLB 量级一致（$\sigma_0/\sqrt{M}$
   × 条件数因子），无系统性偏差。
4. **与 Try1/Try2 的互验**：12 个命名 DUT 中 9 个与 Try1 §8.2 的 DUT
   电气同构（ser_rc、par_rc、ind_parasitic、cap_parasitic、ladder 等），
   无噪恢复达机器精度（wrmse ≤ 1e-14，参数 ≤ 5.6e-11），与 Try1 引擎 A
   的同类结果一致；桥式与重边结构则超出 Try1 表达范围、由本算法覆盖
   （Try2 的免拟合枚举验证过的同款求值内核）。

---

## 8. 能力边界与理想测量范围（诚实声明）

**建议工作范围**（超出仍可运行，但性能/可靠性不保证）：

| 量 | 理想范围 | 依据 |
|---|---|---|
| 边数 $E$ | ≤ 12 | $p \le 24 < 2M = 60$；战役实测 p100 = 13 |
| 节点数 $V$ | ≤ 8 | $M V^3$ 主导；实测 $V \le 7$ |
| 频点 $M$ | ≥ max(4·(储能元件数), 2p) | Try1 A2；带覆盖谐振 ±2 dec |
| 元件值 | 物理箱内（§1.2） | 超箱值会被钳到边界并标记 `at_bound` |
| 噪声 | 相对 σ₀ ≤ ~2% | σ₀ ↑ 则 CRLB 等比放大参数误差 |

**时间预期**（本机 WSL2，i9 / 20 核，conda lcr / numpy 2.4）：
单案例中位 ~0.1 s、p99 ~1.8 s（含全部救援阶段）；每案例中位 ~29 个起点、
卡住案例最多 ~425 个（E 段触发时）。

**已知边界**：
1. **秩亏结构**（T3，如桥式）：只输出族代表元 + 秩诊断；参数级真值恢复
   原理上不可能（任何算法皆然——数据 $Z(s)$ 本身只含秩个自由度）。
2. **电学等价族**（T5）：约 1.8% 的良态案例参数落在等价实现上（曲线在
   噪声级一致）；完整等价类枚举属 Try1 引擎 B（Foster/Cauer 综合）的
   职责，列为 Try3 的 P2 扩展。
3. **高 Q 多 tank 深谷**：0.05%（1/2000，E 段后）案例多起点未达底（§5.3
   决策 D4a 的原理性残留——真值谐振不在数据极值处且无可达下坡路径）；
   带外推不可信。
4. **弱可见参数**（T4）：值由噪声决定，输出已标记；频带设计（覆盖
   $\omega_c = R_d/L$ 等转折）是用户侧的责任。

---

## 9. 验证与实测结果

### 9.1 单元测试（93 项，全绿，~7 s）

| 文件 | 覆盖 |
|---|---|
| `test_graph.py` | 每条减支规则、聚合公式、端口开路、嵌套归约、30 组随机图 Z 不变性 |
| `test_nodal.py` | 闭式对照（串/并联 RC、并联 RLC 含 DCR、梯形）、桥式无源性 + 独立装配交叉验证、Jacobian/弹性 vs 中心差分 |
| `test_fit.py` | 无噪精确恢复（10 DUT）、桥式曲线级 + 秩亏标记、删边/合并报告、弱参数标记、噪声恢复 |
| `test_identify.py` | 多图 AICc 排序、真图胜出、带外外推 |
| `test_end_to_end.py` | 12 命名 DUT × {无噪, 0.5%}：wrmse、曲线（动态范围下限度量）、参数恢复 |

### 9.2 命名 DUT（12 个，含 Try1 §8.2 的图版同构与 Try3 特有结构）

无噪：全部 12/12 曲线机器精度；11 个可辨识 DUT 参数误差 ≤ 5.6e-11；
`bridge` 正确报告 rank 3/5。
0.5% 噪声：全部 wrmse ≤ 0.0064（底 0.0071）；可辨识参数（剔除 weak）
误差 ≤ 0.42%（多数 < 0.2%）。

### 9.3 随机战役（2000 组，seed 20260903，18 进程 33 s 墙钟）

随机 DUT：$V \sim U\{2..6\}$ 的均匀标记树 + 0–3 条额外重边/跨边，
类型均匀，真值对数均匀（$R \in [1,10^6]\,\Omega$、$C \in [1\,\mathrm{pF},1\,\mu\mathrm{F}]$、
$L \in [10\,\mathrm{nH},1\,\mathrm{H}]$、$R_d \in [1\,\mathrm{m}\Omega,1\,\mathrm{k}\Omega]$）；
测量协议同 Try1/Try2（30 点、10 Hz–10 MHz、0.5% 相对复高斯噪声）。

**判据**：fit_ok = wRMSE ≤ 3×底；curve_ok = 动态范围下限化（分母 ≥ 0.1×
中位 $|Z|$）的 100 点带内密网格最大相对误差 ≤ 5%（Try1 等价判据的噪声版）；
参数统计按"解处满秩"分层，并剔除真值处带内不可见（$\max|E| < 0.1$）参数。

| 指标 | 结果 |
|---|---|
| 运行错误 | 0 / 2000 |
| **fit_ok（达 3× 噪声底以内）** | **99.95%**（1 例残留：0.297；v1 为 99.85% 三例，其中两例被 E 段救回） |
| wRMSE p50 / p90 / p99 | 0.00687 / 0.00775 / 0.00852（底 0.00707） |
| 结构可辨识（满秩）占比 | 66.2% |
| curve_ok（可辨识案例） | **99.1%**（全体 97.8%，v1 97.75%） |
| curve_ok（秩亏案例） | 95.3%（p50 0.0026, p90 0.016——族代表元仍再现曲线） |
| 参数误差（满秩案例，4143 个可见参数） | 中位 **0.108%**、p90 1.09% |
| 参数误差（满秩且 cond ≤ 1e4，1285 案例） | 中位 0.106%、p90 0.86% |
| 单案例耗时 | 中位 101 ms、p90 0.61 s、p99 1.80 s（E 段仅对卡住案例触发，中位无回归） |

**参数 p99（161%）的构成（诚实分析）**：几乎全部来自 T5 电学等价族
（曲线误差仍在噪声级，如 seed 20262126：参数差 3400× 而曲线误差 0.33%）
与 39 例 cond > 1e4 的弱确定案例；两者都有显式诊断输出
（曲线一致性可查、cond 已报告），不是静默错误。

**残留失败（1/2000，E 段后）剖析**：seed 20262534（多 tank + 秩亏复合病态）。
v1 的另两例（20261995/20261441）已被 E 段混合重启救回噪声底。残留例从真值
出发可收敛，但其真值 tank 谐振不在 |z| 数据极值处（多储能网络的极值 ≠ 单
tank 频率），且真值盆地与所有可达盆地间无下坡路径——基于极值播种 + 随机
重启的方法原理上不可达；根治需数据有理拟合提极点/零点种子（Try1 引擎 B
思路，P2，见 §5.3 决策 D4a）。作为 0.05% 的已知边界记录在 §8。

---

## 10. 软件架构

```
Try3-Component types with known topology and corresponding positions/
├── DESIGN.md                  # 本文档
├── topofit_id/
│   ├── __init__.py            # identify / identify_many / FitConfig
│   ├── graph.py               # 多重图、减支 F1–F4、聚合表达式树
│   ├── nodal.py               # 批量节点分析、伴随 Jacobian、弹性系数
│   ├── fit.py                 # 归一化、多起点漏斗 A/B/B2/C/D、诊断、报告
│   ├── metric.py              # Try1 §5.1/8.1 度量 + 置换匹配 + 下限化曲线误差
│   ├── synthetic.py           # 12 命名 DUT、随机 DUT（Prüfer 树）、测量仿真
│   ├── adjacency.py           # 统一输出：拟合群 → 上三角邻接矩阵 + vector<Edge>
│   │                         # （规范见 ../../OUTPUT_FORMAT.md §5.3，独立实现；
│   │                         # 群聚合值放置于原始节点标签，删/并边转注释行）
│   └── iofmt.py              # 统一输入：测量数据 + 拓扑（上三角边数矩阵 + 类型队列）
│                             # （规范见 ../../INPUT_FORMAT.md §1/§2.3，独立实现）
├── tests/                     # 93 项 pytest + test_adjacency.py
│                              # （放置/守恒/稀疏标签 + NodalModel Z 交叉验证）
│                              # + test_iofmt.py（round-trip/校验/文本输入→identify 冒烟）
├── run_campaign.py            # N 组随机战役（多进程、JSON 报告）
├── campaign_results.json      # 2000 组结果（本文档 §9.3 的数据源）
└── demo.py                    # 命名 DUT 演示 / 多图排序演示
```

依赖：Python 3.11 + numpy + scipy（与 Try1/Try2 同一 conda env `lcr`）。

## 11. 决策记录汇总

- **D1 双重归一化**（频率 + 阻抗）：单位起点普适化、条件数拉平。
- **D2 伴随法解析 Jacobian**：一次 LU 后 $O(Mp)$，精确机器精度。
- **D3 TRF 箱约束求解器**：与 Try1 D5 一致，两阶段容差。
- **D4 多起点漏斗**：LHS 全箱/中心箱 + 扰动 + 升级。
- **D5 谐振种子 + 拓扑配对 + 阻尼同伦**：高 Q 窄谷的三层覆盖（战役驱动）。
- **D6 AICc 跨图排序**：多候选图时惩罚参数数；单图只报 Try1 度量。
- **D7 诊断三件套**：weak / at_bound / rank+cond，宁缺毋滥。

---

## 附录 A：参考文献

### A.1 网络分析与灵敏度

1. **Vlach, J., & Singhal, K. (1983).** *Computer Methods for Circuit Analysis
   and Design.* Van Nostrand Reinhold, New York.
   —— 节点分析（MNA）压印法与电路灵敏度分析的标准教材；§4.1 压印约定
   与 Try2 §5 同源。

2. **Director, S. W., & Rohrer, R. A. (1969).** "The generalized adjoint
   network and network sensitivities." *IEEE Transactions on Circuit
   Theory*, 16(3), 318–323. doi:10.1109/TCT.1969.1082967.
   —— 伴随网络灵敏度法原文；§4.2 解析 Jacobian 的理论出处。

3. **Brune, O. (1931).** "Synthesis of a finite two-terminal network whose
   driving-point impedance is a prescribed function of frequency."
   *Journal of Mathematics and Physics*, 10(1–4), 191–236.
   doi:10.1002/sapm1931101191.
   —— 正实函数定理（继承自 Try1 §2.1 T1）。

### A.2 系统辨识与可辨识性

4. **Ljung, L., & Glad, S. T. (1994).** "On global identifiability of
   arbitrary model parameterizations." *Automatica*, 30(2), 265–276.
   doi:10.1016/0005-1098(94)90029-9.
   —— 微分代数可辨识性理论；§2.3 局部/全局可辨识判据的框架。

5. **Walter, É., & Pronzato, L. (1997).** *Identification of Parametric
   Models from Experimental Data.* Springer, London. doi:10.1007/978-1-4471-1538-2.
   —— 参数辨识的可辨识性/信息矩阵/CRLB 系统论述；§2.4 弹性判据与
   §7.3 统计最优性论证的背景。

### A.3 数值优化

6. **Branch, M. A., Coleman, T. F., & Li, Y. (1999).** "A subspace,
   interior, and conjugate gradient method for large-scale bound-constrained
   minimization problems." *SIAM Journal on Scientific Computing*,
   21(1), 1–23. doi:10.1137/S1064827595289108.
   —— scipy `least_squares(method="trf")` 的算法基础（继承 Try1 D5）。

7. **Bertsekas, D. P. (1976).** "On the Goldstein-Levitin-Polyak gradient
   projection method." *IEEE Transactions on Automatic Control*, 21(2),
   174–184. doi:10.1109/TAC.1976.1101194.
   —— 投影/连续化处理箱约束与同伦思想的经典背景（§5.4 阻尼同伦）。

8. **McKay, M. D., Beckman, R. J., & Conover, W. J. (1979).** "A comparison
   of three methods for selecting values of input variables in the analysis
   of output from a computer code." *Technometrics*, 21(2), 239–245.
   doi:10.1080/00401706.1979.10489755.
   —— 拉丁超立方采样（继承 Try1 多起点）。

### A.4 内部文档（本项目）

9. **Try1 DESIGN.md**（`../Try1-Completely unknown single port fitting/`）：
   §1 数据/噪声模型、§2.1–2.2 正实性与可辨识性、§4.3 参数箱、
   §5.1/§5.2 残差与解析 Jacobian（前向 AD）、§5.4 谐振启发、§5.5 AICc、
   §8.1 度量——本文逐式沿用或图版推广。

10. **Try2 DESIGN.md**（`../Try2-Known component types, quantities and
    parameters/`）：§3 多重图双层表示、§5 批量节点分析压印、电感
    $L{+}R_d$ 双参数约定——本文 §4.1 与之一致，保证课题间可比。

---

## 附录 B：伪代码

### B.1 主流程 `identify(f, z, edges)`

```python
def identify(f, z, edges, config):
    red = reduce_graph(edges)              # T2: 精确减支（值无关）
    model = NodalModel.from_edges(red)     # 压印索引、参数布局
    w0, z0 = geomean(w), geomean(|z|)      # D1: 双重归一化
    s_t, z_t = 1j*w/w0, z/z0
    lb, ub = physical_bounds / scales      # 对数箱

    starts = [unit] + LHS(full) + LHS(center) + resonance_seeds + topo_seeds
    best = run_coarse(starts)              # A 段
    best = min(best, run_coarse(perturb(best)))        # B 段
    best = min(best, run_coarse(pair_resonance(best))) # B2 段
    while wrmse(best) > 0.03 and rounds_left:          # C 段
        best = min(best, run_coarse(LHS(...) + pair_resonance(best)))
    if wrmse(best) > 0.03:                             # D 段
        best = min(best, homotopy_rescue(best), homotopy_rescue(unit))
    while wrmse(best) > 0.03 and rounds_left:          # E 段（决策 D4a）
        best = min(best, run_coarse(LHS + centerLHS
                    + multiscale_perturb(best) + pair_resonance(best)))
    best = polish(top3)                                # 精修

    E = elasticity(best)                # T4: weak 标记
    rank, cond = svd(weighted_jacobian(best))  # T3: 秩诊断
    return FitResult(groups, edge_reports, metrics, diagnostics)
```

### B.2 减支 `reduce_graph`

```python
def reduce_graph(edges):
    work = [WEdge(u, v, kind, leaf(i, kind)) for i, e in enumerate(edges)]
    while changed:
        drop self loops                              # F1a
        check port 0-1 connected else raise          # F1b
        keep only port-connected component           # F1b
        iteratively drop non-port degree-1 branches  # F1c
        merge same-kind parallel R/C groups          # F2
        merge one degree-2 same-kind / {R,L} series  # F3/F4
    return ReductionResult(work, dropped)
```

### B.3 战役 `run_campaign.py`

```python
for seed in seeds:                       # 多进程池
    dut = random_case(rng(seed))         # Prüfer 树 + 重边 + 对数均匀值
    f, z = measure(dut, sigma=0.005)     # Try1 协议
    r = identify(f, z, dut.edges)
    # 对照真值：曲线（下限化最大相对误差）、分层参数误差
    # （解处满秩? 真值处可见? 同位同型置换对齐）、秩/条件数、耗时
aggregate(rows) -> JSON + markdown 摘要
```
