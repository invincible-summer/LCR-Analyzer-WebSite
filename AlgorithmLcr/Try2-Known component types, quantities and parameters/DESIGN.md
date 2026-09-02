# 已知元件的单端口网络拓扑识别算法 — 设计文档（Try2）

版本 v1.0 · 2026-09-03 · 状态：**P1 已实现并验证通过**（50 测试全绿；demo 13/13 恢复：10 命名 DUT + 3 随机 DUT）

> 目标读者：审查者 + 后续维护者。本文档与 `netgraph_id/` 代码逐项对应，记录每一次设计
> 决策（**决策 Dn**）、数学推理与合理性证明；改算法先改本文档。
> 情景背景与 Try1（完全未知的单端口拟合）的关系见 §1.3 与
> `../Try1-Completely unknown single port fitting/DESIGN.md`。

---

## 0. 摘要

**输入**：DUT 的元件多重集完全已知——$n_R$ 个理想电阻、$n_C$ 个理想电容、$n_L$ 个
电感（每个电感 = 理想电感 $L$ + 串联理想直流电阻 $R_{dc}$，双参数，$R_{dc}\ge 0$
可为 0），以及离散频点复阻抗测量 $\{(f_k,\hat z_k)\}_{k=1}^{M}$（含噪声）。

**输出**：按拟合误差排序的**拓扑等价类列表**（每类 = 一个接线方案代表元 + 电学
等价成员 + 拟合优度）。

**核心论点（后文逐一证明）**：

1. 元件值已知 ⟹ 候选拓扑**无需参数拟合**、$Z(f)$ 可精确求值 ⟹ 逐候选成本从
   Try1 的"多起点迭代优化"降为一次批量节点分析 ⟹ **全多重图枚举变得可行**——
   包括 Try1 原理上不可覆盖的桥式（非串并联）拓扑与重边（并联）结构（§2.2）。
2. 搜索空间不是"所有图"：**R0 死区定理**（§2.5）证明任何通过割点悬挂、不含端口
   的子图净电流恒为零，可安全排除；加上图同构规范化，E=6 的候选数从朴素的
   $9.8\times10^{7}$ 压缩到 $2.8\times10^{4}$（**减支 ≥ 3.5 个数量级**，§4.4）。
3. 拓扑仍**不可唯一辨识**（§2.3）：图自同构、Whitney 2-同构、数值巧合三类电气
   等价使"唯一正确答案"在数学上不存在 ⟹ 输出等价类（承袭 Try1 T2 的适定性立场）。

**实测能力边界**（WSL2、conda `lcr`、Python 3.11 + numpy，单核）：E ≤ 6 秒级、
E = 7 约 37 s；E ≥ 8 为 P2 扩展（§10.2）。

---

## 1. 问题形式化

### 1.1 数据模型

$$
\hat z_k = Z(j\omega_k) + \varepsilon_k,\qquad \omega_k = 2\pi f_k,\quad k=1..M,
$$

$\varepsilon_k$ 为复高斯、实虚部独立、$\sigma_k \approx \sigma_0|\hat z_k|$
（相对噪声模型，同 Try1 A3；实测 LCR 场景 $\sigma_0 \approx 0.5\%$）。
$Z(s)$ 为被测单口的真实驱动点阻抗。

### 1.2 已知量与未知量

| | Try1 | **Try2（本文）** |
|---|---|---|
| 元件类型 | 未知（R/L/C） | **已知** |
| 元件数量 | 未知（≤ N_max） | **已知** $E=n_R+n_C+n_L$ |
| 元件参数 | 未知（需拟合） | **已知**（电感含 DCR） |
| 拓扑 | 未知（限串并联树） | **未知（任意图，含重边）** |
| 每候选成本 | 多起点迭代最小二乘 | 一次批量 MNA 求值 |
| 电感模型 | 理想 L（寄生需显式 R 叶） | **复合边** $R_{dc}+sL$ |

网络建模为**连通多重无向图** $G$：节点 = 支路连接点（编号 0、1 为两端口，
$2..V{-}1$ 为内部节点），边 = 元件（每条边是独立对象 $(u,v,\text{id})$，
绝不用节点对作唯一键——同一节点对的多条边 = **并联**，即重边）；自环（元件两端
接同一节点）电气无效（外部电流恒为零），不枚举。

### 1.3 假设

- **A0（对象）**：集中参数、线性、时不变、无源单端口；元件取值正实（$R_{dc}\ge0$）。
  无互感、受控源、分布参数。与 Try1 A0 一致。
- **A1（规模）**：$E \le 7$（纯 Python；§10 论证与实测）。
- **A2（活跃性）**：每个元件都处于某种电流路径上（形式化即 R0：无死区子图，
  §2.5）。悬空元件电气不可观测，其拓扑原则上不可辨识，默认排除。
- **A3（采样）**：频点对数等距覆盖 $10\,\text{Hz}\sim10\,\text{MHz}$，$M\ge 20$；
  覆盖主要特征频率 ±2 个十倍频程。
- **A4（可激励性）**：至少含一个储能元件（L 或 C）。全电阻网络的 $Z(f)$ 与频率
  无关，数据只含 1 个复数信息量（有效电阻），拓扑不可辨识（§2.4，实测验证）。
- **A5（活跃参数）**：元件值需使元件在频带内"起作用"（转折/谐振频率不在带外
  三个数量级以外）；带外惰性元件产生统计并列类（§6.3 实测案例）。

### 1.4 目标

沿用 Try1 的加权复残差（§5.1，公式逐式一致）：

$$
\chi^2(G) = \sum_{k=1}^{M} w_k^2\,\big|\hat z_k - Z_G(j\omega_k)\big|^2,
\qquad w_k = 1/|\hat z_k|,
$$

在候选多重图集合 $\mathcal{G}_E$（§4 枚举）上最小化，输出排序等价类。

**决策 D0（免拟合穷举架构）**：Try1 因每拓扑需参数拟合而退守串并联树；Try2 元件
值已知，评估是纯前向计算，故采用**穷举 + 减支漏斗**：完备性由构造保证（§4.4
定理），而非由"覆盖常见情形"的经验保证（Try1 §2.3 的边界声明）。代价是候选数
随 $E$ 阶乘增长（§10.2），换来的是桥式/重边/深度嵌套的完整覆盖。

---

## 2. 数学基础

### 2.1 T1 — 正实性（继承）

任意无源 RLC 单口的 $Z(s)$ 必为正实有理函数（Brune 1931；证明见 Try1 §2.1）。
对 Try2 的直接推论：每个候选的 $Z_G(s)$ 的 McMillan 阶 $\le n_L+n_C$（储能元件
总数，对全部候选相同），极点均在闭左半平面——这是 §5.4 无源性数值检验的依据。

### 2.2 T2 — 免拟合性（Try2 的根本优势）

Try1 中候选评价 = 在连续参数空间做多起点非线性最小二乘（单拓扑 $10^2\sim10^3$
次 $Z$ 求值）；Try2 中候选评价 = 一次确定性前向计算：

$$
Z_G(f) = (Y_{\text{red}}(j\omega)^{-1})_{00},\qquad O(M\,V^3)\ \text{flops},
$$

且批量 LAPACK 可同时评估数千候选（§5.1）。搜索空间的连续自由度（Try1 的
$2n+2$ 维）**完全消失**，只剩离散的图结构选择。这是"元件值已知"这一先验的
最大化利用。

### 2.3 T3 — 拓扑不可唯一性（三类电气等价）

即使数据无噪、频带无限，以下候选的 $Z(s)$ **完全相同**，任何算法不可区分：

1. **图自同构**：重标签下的同一接线（含端口两端互换——驱动点阻抗对称）。
   ⟹ 枚举层以规范形消除（§4.1），每个同构类恰生成一次。
2. **Whitney 2-同构** [1]：两图有相同的生成树集合（边子集意义下）⟺ 2-同构。
   由网络函数的**拓扑公式**（$Z$ 的分子/分母 = 生成树/2-树导纳积之和，
   Kirchhoff [2]、Chen [3]），2-同构图对**任意**元件值有相同 $Z$。小图中
   2-同构但不同构的对虽罕见（需要 Whitney 翻转结构），但存在即不可分辨。
3. **数值巧合**：特定元件值下不同构的网络重合（如满足电阻关系的 Y-Δ 变换；
   测度零但工程上可遇到）。

因此正确输出为**等价类**：类内成员 $Z$ 在扩展频带上最大相对偏差
$< \max(10^{-3},\,3\hat\sigma_{\text{rel}})$（Try1 §5.5 判据的沿用），类间按
$\chi^2$ 排序。**类内二级判据**（工程简约性，决策 D6）：内部节点少 → 串并联
优先（Valdes–Tarjan–Puech 线性识别 [4]）→ 规范串字典序。

### 2.4 T4 — 直流简并（实测验证的边界）

全电阻网络：$Z_G(f) = Z_G$ 为常数 ⟹ 数据信息量 = 1 个复数（有效电阻），
仅能辨识 $Z_{\text{eff}}$，不能辨识拓扑。实测（开发过程）：全电阻惠斯通电桥
DUT 的 top-1 等价类包含真值但代表元是任意等阻值接线——**算法行为正确**，
问题本身不适定。⟹ 假设 A4：至少一个储能元件。

### 2.5 T5 — 死区定理与 R0 减支规则

**定义**：$P$ 为 $G - c$（删去节点 $c$）的一个连通分量且不含端口 0、1，称
$P$（连同其全部边）为**死区**。

**定理（R0）**：删除任何死区不改变网络的 $Z(s)$。

**证明**：设 $P$ 的节点集为 $\mathcal{P}$，其与外部的全部连接边均过 $c$。
对解出的网络状态，对 $x\in\mathcal{P}$ 逐点写 KCL 并求和：内部边电流成对出现
且反号抵消，剩下 $\sum_{e\ni(c,x),\,x\in\mathcal{P}} i_e = 0$，即**流入 $P$
的净电流恒为零**（对任意 $V_c$ 成立）。把 $P$ 换成开路，电路方程不变，
$Z$ 不变。∎

**推论（单遍判定）**：死区存在 ⟺ 存在 $c$ 使 $G-c$ 有不含端口的连通分量。
（若死区 $P$ 真包含于某分量 $Q\subseteq G-c$，则 $P$ 在 $Q$ 内另有连接，
与 $P$ 只经 $c$ 连外矛盾；故 $P$ 必为完整分量，单遍扫描充分。）

该规则统一并推广了直觉性规则：悬空支路（度 1 内部节点）、经同一节点并联悬挂的
支路对、悬挂三角形等，全部是死区特例。**实测剪枝量**：E=6 候选数从朴素
$9.8\times10^7$ 降至 $2.8\times10^4$（含 R0 + 同构去重 + 轨道坍缩，§4.4）。

---

## 3. 图模型与数据结构

### 3.1 双层表示（决策 D1）

- **结构层（多重邻接矩阵）**：$M[i][j] = m_{ij}$（对角为 0，$\sum m_{ij}=E$），
  按规范槽位序 $[(0,1),(0,2),\dots,(V{-}2,V{-}1)]$ 存为元组。枚举、规范化、
  去重都在这层进行——"枚举结构"= 枚举满足约束的整数对称矩阵；$V\le E{+}1\le 8$，
  矩阵极小，同构置换只是行列重排。
- **指派层（边表）**：每条边是独立对象 $(u, v, \text{id}, \text{comp})$，
  重边信息（$m_{ij}\ge 2$ 时每条边的类型与数值）完整保留，可无损还原任意
  并联拓扑。

### 3.2 求值层与统一并联公式的衔接（决策 D2）

MNA 印章：边 $(u,v)$ 导纳 $y$ 贡献 $Y_{uu}{+}{=}y,\ Y_{vv}{+}{=}y,\ Y_{uv}{-}{=}y,\
Y_{vu}{-}{=}y$。**同一节点对的重边导纳直接相加进同一矩阵元素**——这正是
"重边 = 并联"的数学表达。$V=2$（单槽）时严格退化为闭式总公式：

$$
Z(f) = \frac{1}{\displaystyle\sum_i \frac{1}{R_i} + j2\pi f\sum_i C_i
+ \sum_i \frac{1}{R_{Li} + j2\pi f\,L_i}}.
$$

---

## 4. 枚举算法（两阶段，无同构完备）

### 4.1 规范形与代表元唯一性

**重标签群**：$G_{\text{lab}} = \{\text{端口互换}\}\times \mathrm{Sym}(2..V{-}1)$，
$|G_{\text{lab}}| = 2\,(V{-}2)!$（$V\le8$ 时 $\le 1440$）。端口 0/1 作为**集合**
被区分（端口对），但互换不改变 $Z$，故两种标签视为同一网络。

**规范形**：$\text{canon}(m) = \min_{p\in G_{\text{lab}}} p\cdot m$（槽位多重
向量在置换下的像取字典序最小者）。

**定理（代表元唯一）**：每个（均匀边）同构类在规范形集合中有唯一代表元；
两个多重图同构（保端口集合）⟺ 规范形相等。*证明*：规范化映射是到轨道代表元
的常值映射；$\min$ 在群作用下不变。∎（与 Try1 §4.1 R3 的字典序查重同构思想，
但作用群从树自同构推广为节点置换群。）

**自同构群**：$\mathrm{Aut}(m) = \{p\in G_{\text{lab}}: p\cdot m = m\}$，
供指派层轨道去重（§4.3）。

### 4.2 阶段一：结构层枚举

对 $V = 2..E{+}1$：枚举 $S(V)=V(V{-}1)/2$ 个槽位上 $E$ 条（均匀）边的多重组合
$\binom{S(V)+E-1}{E}$（`combinations_with_replacement`），依次过滤：

1. **连通性**（并查集，$O(E\,\alpha(V))$）：全部 $V$ 个节点连通；
2. **R0**（§2.5，$O(V\cdot(V{+}E))$）：无死区；
3. **规范化 + 去重**（幸存者才做，$O(2(V{-}2)!\cdot S)$）。

```python
for V in range(2, E + 2):
    for combo in combinations_with_replacement(range(S(V)), E):
        mult = to_mult(combo)
        if not is_connected(V, mult): continue      # 廉价，先做
        if has_dead_part(V, mult):   continue       # R0
        cm = canonical_mult(V, mult)                # 昂贵，后做
        seen.add(cm)                                # 每同构类恰一次
```

### 4.3 阶段二：指派层枚举

对每个规范结构，将元件多重集指派到边实例（实例序 = 槽位规范序）：

- 元件按 $(\text{kind}, \text{value}, \text{dcr})$ 规范排序；**同键元件不可分辨**；
- 遍历 $E!$ 个排列，以**序列化键**去重：每槽位内元件键排序（并联阶无关 ⟹
  重边自动坍缩），再在 $|\mathrm{Aut}|>1$ 时对槽位键沿自同构置换取最小
  （`graph.permute_slot_keys`）；
- 每条"接线轨道"恰产出一个候选。正确性：两指派等价 ⟺ 存在 $p$ 映射之一
  到另一且保结构 ⟺ 序列化键相等（空槽编码保证占用模式一致 ⟹ $p\in\mathrm{Aut}$）。

### 4.4 完备性定理与实测计数

**定理（无重无漏）**：每种"用尽全部 $E$ 个元件、连通、无死区、无自环"的接线
方式，在（结构规范形 × 指派轨道代表元）空间中恰出现一次。
*证明*：任意合法接线 → 删元件标签得均匀结构 → 其规范形恰在阶段一产出
（连通/死区在重标签下不变；其多重组合必被遍历）；指派轨道由 §4.3 键唯一代表。∎

**实测计数表**（`python demo.py --stats`，可分辨元件）：

| $E$ | 结构数（分 V） | 候选数 $N_{\text{cand}}$ | 朴素槽位指派 $\sum_V S(V)^E$ | 减支量 |
|---|---|---|---|---|
| 2 | 2 | 2 | $1.0\times10^{1}$ | 5× |
| 3 | 4 | 10 | $2.4\times10^{2}$ | 24× |
| 4 | 11 | 98 | $1.1\times10^{4}$ | 117× |
| 5 | 31 | 1 426 | $8.7\times10^{5}$ | 608× |
| 6 | 104 | 27 542 | $9.8\times10^{7}$ | **3 560×** |
| 7 | 369 | 669 670 | $1.6\times10^{10}$ | 23 000× |

（分 V 明细，E=7：$\{V{=}2{:}1,\ 3{:}12,\ 4{:}77,\ 5{:}153,\ 6{:}105,\ 7{:}20,\ 8{:}1\}$；
E=6：$\{2{:}1,\,3{:}9,\,4{:}37,\,5{:}42,\,6{:}14,\,7{:}1\}$。）
E=6 案例核对：104 结构 × 平均指派 $E!/|\mathrm{Aut}| \approx 265$ ≈ 2.75e4 ✓。
同值元件按 $\prod_g m_g!$ 进一步坍缩（如 E=3 双 10kΩ + C：10 → 7 候选，实测）。

### 4.5 复杂度

- **阶段一**：$O\!\big(\sum_{V=2}^{E+1}\binom{S(V)+E-1}{E}\cdot E\alpha\big)$
  遍历 + 幸存者规范化 $O\big(2(V{-}2)!\,S\big)$。主导项是组合遍历
  （E=7 实测 19.9 s，其中 V=7 的 $\binom{27}{7}=888{,}030$ 与 V=8 的
  $\binom{34}{7}=5.4\times10^6$ 占大头）。
- **阶段二**：$O\big(\sum_{\text{结构}} E!\cdot(|\mathrm{Aut}|\,S + E\log E)/\prod_g m_g!\big)$；
  $|\mathrm{Aut}|$ 中位数 = 1–2，实测 E=7 指派层 6.3 s。
- **空间**：结构库 $O(\#\text{结构}\cdot S)$（E=7 仅 369×28），候选流式产出。

---

## 5. 阻抗求值

### 5.1 批量化节点分析（`nodal.StructureStamps`）

对固定（结构, 元件集）：边实例按槽位连续分组 ⟹ 槽位导纳 = `np.add.reduceat`
一次聚合；对角/非对角印章目标预计算为索引数组；每一频点构造
$(N, V{-}1, V{-}1)$ 复数矩阵栈，`np.linalg.solve` 批量求解，
$Z = (Y_{\text{red}}^{-1})_{00}$（注入单位电流于端口节点 1、接地节点 0）。
总成本 $O(N\,M\,V^3)$（批量 LAPACK，E=6 全库 27 542 候选 × 30 频点 ≈ 1 s 实测）。

### 5.2 奇异性处理

精确奇异（如纯 LC 并联在精确谐振频点）或病态时 `LinAlgError` 回退到逐候选
求解，仍奇异者置 $Z=\infty$ ⟹ $\chi^2=\infty$ 自然淘汰。测量频点取对数栅格，
撞上精确奇异点的概率为测度零。

### 5.3 直流/高频渐近不变量（`nodal.asymptote_impedance`）

- $Z(0)$：理想电感（$R_{dc}=0$）边 = 短路（union-find 合并端点），电容边开路，
  电阻边取 $R$、有损电感边取 $R_{dc}$，得电阻网络的有效电阻；
- $Z(\infty)$：电容边短路合并，电感边（含 DCR，$\omega\to\infty$ 时 $|Z_L|\to\infty$）
  开路，只剩电阻边。
二者为纯图 + 线性方程计算，用作报告标注与单元测试锚点（`test_nodal.py`）；
在线剪枝由 §7 的探针漏斗承担（有限频点精确求值严格强于渐近比较，且可批量化）。

### 5.4 交叉验证

- V=2 退化为 §3.2 闭式公式（rtol 1e-12）；
- 串并联网络与 **Try1** `rlc_id.circuits.evaluate_f` 逐频点一致（rtol 1e-10，
  `test_nodal.py::TestTry1CrossValidation`，含 (R+L)∥C 寄生模型 = Try1 双叶建模）；
- 平衡惠斯通电桥手算值（300∥300 = 150 Ω）；
- 无源性：随机网络 $\operatorname{Re}Z \ge -\epsilon$（T1 推论）。

---

## 6. 误差度量与模型选择

### 6.1 公式（与 Try1 §5.1/§8.1 逐式一致）

$$
r_k = w_k(\hat z_k - Z_G(j\omega_k)),\quad \mathbf{r} = [\operatorname{Re}r_1,
\operatorname{Im}r_1,\dots],\quad \chi^2 = \|\mathbf r\|^2,
$$
$$
\mathrm{wRMSE} = \sqrt{\tfrac1M\textstyle\sum_k\big|(\hat z_k - Z_G)/\hat z_k\big|^2},
\qquad e_{\max} = \max_k\big|(\hat z_k - Z_G)/\hat z_k\big|.
$$

（`metric.py`；`tests/test_metric.py` 与 Try1 `fit_engine_a.py` 数值对拍锁定。）

### 6.2 AICc 在 Try2 中退化为 RSS 排序

全部候选共用同一元件集 ⟹ 参数数 $p = n_R + n_C + 2n_L$ 与 AICc 的 $K=p+1$
**恒定** ⟹ AICc 是 RSS 的严格单调变换 ⟹ 排序等价。报告仍打印 AICc 值以延续
Try1 界面（决策 D3）。这一退化本身就是"元件值已知"先验的另一个体现：模型
复杂度惩罚无事可罚。

### 6.3 多重比较效应（实测发现，决策 D4）

$N_{\text{cand}}$ 很大时，即使真值在候选中，best-of-$N$ 的噪声实现也可能使
**近简并**候选的 $\chi^2$ 略低于真值（$\chi^2$ 的最小次序统计量随 $N$ 下移）。
实测（E=7、$\sigma_0=0.5\%$、刻意取带外惰性元件值）：67 万候选中 top-8 类
$\chi^2$ 全部贴合噪声底（wRMSE $\approx 6.05\times10^{-3}$，低于噪声期望
$7.1\times10^{-3}$），真值混在其中；换带内活跃元件值后真值以 0.05% 精度稳居
rank-1 类。**处置**：(i) 报告 top-k 等价类而非唯一样本；(ii) 假设 A5（元件值
需带内活跃）；(iii) 需要更高置信度时增加频点数 $M$（$\chi^2$ 噪声底随 $M$ 收窄）。
这是穷举式方法的固有统计代价，与算法正确性无关。

---

## 7. 减支漏斗（6 级）

| 级 | 机制 | 依据 | 实测效果 |
|---|---|---|---|
| F1 | R0 死区排除 | §2.5 定理 | E=6：$9.8\times10^7\to$ 数千结构候选 |
| F2 | 规范形同构去重 | §4.1 定理 | 消除全部重标签重复 |
| F3 | 同值元件轨道坍缩 | §4.3 | 双同值 R 案例 10→7 |
| F4 | 探针频率漏斗 | 3 个频点（两端+中部）粗评，保留 $\chi^2_{\text{probe}}\le$ best$\times10^6$ | E=6：27 542→10 803（比率宽松宁可少剪；真值必留——错误接线在探针点的相对误差 $\gg\sqrt{10^6}$，`test_funnel_keeps_truth` 锁定） |
| F5 | 全频点 $\chi^2$ 精评排序 | §6.1 | 每候选 $O(MV^3)$ |
| F6 | 等价类聚类 + 二级判据 | §2.3；网格 = 频带外扩 10×、200 对数点，容差 $\max(10^{-3},3\hat\sigma)$ | 输出压缩为代表类列表 |

---

## 8. 验证方案与实测结果

### 8.1 测试矩阵（`tests/`，50 项全绿）

| 文件 | 覆盖 |
|---|---|
| `test_graph.py` | 规范形 = 暴力最小、群闭包、aut 修复性、R0 正反例（含悬挂三角形）、VTP 串并联识别正反例 |
| `test_enumerate.py` | 计数锁定（E=1..4 手算：1/2/4/11）；**E≤4 与全槽位指派暴力枚举精确一致**（完备性 + 无重的机器验证）；同值元件、单槽并联坍缩、方环 aut=4 去重 |
| `test_nodal.py` | V=2 闭式、串联链、平衡电桥手算值、批量=单点、无源性、渐近不变量（含 $R_{dc}=0$ 短接）、**Try1 交叉验证**（3 网络，rtol 1e-10） |
| `test_metric.py` | 残差交错布局、向量化=循环、AICc 恒 K 排序、与 Try1 数值对拍 |
| `test_end_to_end.py` | 10 命名 DUT + 随机 DUT（E=2..6）top-1 类=真值；桥式 SP 标注；无噪精确恢复；2% 噪声鲁棒；漏斗保真 |

### 8.2 命名 DUT 套件（`synthetic.make_duts`）

$10\,\text{Hz}\sim10\,\text{MHz}$、30 对数点、0.5% 相对复高斯噪声：
串联 RL（含 DCR 复合边）、并联 RC、三重并联槽路（重边）、$(R_s{+}L)\|C_p$
电感寄生、$R{+}L{+}C$ 电容寄生、**动态惠斯通电桥**（非串并联，Try1 不可达）、
$C+(L_1\|L_2)$ 双电感重边、RC 阶梯、同值双电阻、E=6 三类混合桥（非 SP）。

**实测：10/10 top-1 类命中**；E=6 混合桥 wRMSE=6.4e-3（≈噪声底），
错误类最近者 wRMSE=1.2e-2（差距 2×）。

### 8.3 随机 DUT（Prüfer 均匀标记树 + 随机补边，V 均匀采样）

E=4 随机 3/3 命中（含 V=4 方环对角网络）。

---

## 9. 软件架构

```
Try2-Known component types, quantities and parameters/
├── DESIGN.md                  # 本文档
├── requirements.txt           # numpy / scipy / pytest（同 Try1）
├── demo.py                    # 命名+随机 DUT 演示；--stats 输出计数/耗时表
├── netgraph_id/
│   ├── __init__.py            # identify(components, f, z, config) -> IdentifyResult
│   ├── components.py          # Component(R/C/L+dcr) 多重集、边导纳、工程格式化
│   ├── graph.py               # 槽位/多重邻接、连通、R0、规范形、Aut、VTP 串并联识别
│   ├── enumerate.py           # 阶段一结构枚举（lru_cache）+ 阶段二指派生成
│   ├── nodal.py               # 批量 MNA、单网络 Z、Z(0)/Z(∞) 渐近不变量
│   ├── metric.py              # Try1 一致误差度量（§6.1）
│   ├── filters.py             # 探针频率漏斗（F4）
│   ├── selector.py            # 候选评估、等价类聚类、二级排序
│   ├── synthetic.py           # 命名 DUT、Prüfer 随机网络、噪声测量
│   └── report.py              # ASCII 排序表（V/SP/wRMSE/maxRel/RSS/类大小）
└── tests/                     # §8.1 的 5 个测试文件
```

主流程（`identify`）：`enumerate_structures(E)`（缓存）→ 流式指派 → 探针漏斗
→ 全频点精评 → 排序聚类 → `IdentifyResult{classes, n_candidates, timings}`。

---

## 10. 性能、能力边界与扩展路线

### 10.1 实测性能表（本机 WSL2，conda lcr，Python 3.11.13 / numpy 2.x）

| $E$ | 结构 | 候选 | 枚举+指派 | 端到端 identify（30 频点） |
|---|---|---|---|---|
| 2 | 2 | 2 | <0.01 s | ~0.01 s |
| 3 | 4 | 10 | <0.01 s | ~0.03 s |
| 4 | 11 | 98 | <0.01 s | ~0.2 s |
| 5 | 31 | 1 426 | 0.04 s | ~0.3 s |
| 6 | 104 | 27 542 | 0.9 s | **1.4 s** |
| 7 | 369 | 669 670 | 26 s | **37 s** |

### 10.2 外推与边界（诚实声明）

E=8 外推：结构数 ~1.5–2k、候选数 $\sim 8!\times\text{结构}/\overline{|\mathrm{Aut}|}
\approx 2\times10^7$、阶段一组合遍历 $\binom{45}{8}=2.2\times10^8$ ——
纯 Python 估计 1–2 小时量级。**P2 路线**：
(i) 阶段一改 McKay 规范构造路径（isomorph-free exhaustive generation [5]），
免组合垃圾遍历；(ii) nauty/Traces [6] 规范标签（有 Python 绑定）替代
$(V{-}2)!\times2$ 暴力最小化；(iii) 参照 Try1 `cppversion/` 先例做 C++ 移植
（批量 MNA 与指派层都是数值密集型，预期 10–50×）。
更高效的**数学剪枝**亦有空间：由数据有理拟合（Try1 引擎 B）得极点/零点，
候选按极点签名预筛（元件值已知 ⟹ 每个候选的极点可由广义特征值问题精确算出）。

### 10.3 理想测量条件建议（"可测量范围"）

- 元件数 $E\le7$（纯 Python P1）；频点 $M\ge20$ 对数等距；
- 频带覆盖所有转折/谐振特征 ±2 十倍频程（A3/A5）；
- 相对噪声 $\sigma_0 \le 1\%$（0.5% 实测下 E≤6 全部 top-1 命中）；
- 至少一个储能元件（A4）；元件值跨度建议 ≤ 4 个数量级且带内活跃；
- 同值元件允许（轨道坍缩自动处理），但会减少可区分候选数（信息量下降）。

---

## 附录 A：参考文献

1. **Whitney, H. (1933).** "2-isomorphic graphs." *American Journal of
   Mathematics*, 55(1), 245–254. doi:10.2307/2381127.
   —— 2-同构 ⟺ 生成树集合相同；§2.3 不可唯一性 (b) 的理论依据。
2. **Kirchhoff, G. (1847).** "Über die Auflösung der Gleichungen, auf welche
   man bei der Untersuchung der linearen Vertheilung galvanischer Ströme
   geführt wird." *Annalen der Physik*, 148(12), 497–508.
   doi:10.1002/andp.18471481202.
   —— 生成树公式（拓扑公式）起源；网络函数是生成树导纳积之和 ⟹ 2-同构不变。
3. **Chen, W.-K. (1976).** *Applied Graph Theory: Graphs and Electrical
   Networks*, 2nd ed., North-Holland, Amsterdam.
   —— 拓扑公式（树/2-树和）的系统阐述；§2.3、§10.2 极点签名剪枝的理论背景。
4. **Valdes, J., Tarjan, R. E., & Puech, E. L. (1982).** "The recognition of
   series-parallel digraphs." *SIAM Journal on Computing*, 11(2), 298–313.
   doi:10.1137/0211023.
   —— 两端串并联图的线性识别（减约合流性）；§2.3 二级判据与报告 SP 标注。
5. **McKay, B. D. (1998).** "Isomorph-free exhaustive generation."
   *Journal of Algorithms*, 26(2), 306–324. doi:10.1006/jagm.1997.0898.
   —— 规范构造路径（无同构完备生成的标准方法）；§10.2 P2 路线 (i)。
6. **McKay, B. D., & Piperno, A. (2014).** "Practical graph isomorphism,
   benchmarking and the toolkit nauty/Traces." *Journal of Symbolic
   Computation*, 60, 94–112. doi:10.1016/j.jsc.2013.09.003.
   —— 实用规范标签工具；§10.2 P2 路线 (ii)。
7. **Brune, O. (1931).** "Synthesis of a finite two-terminal network whose
   driving-point impedance is a prescribed function of frequency."
   *Journal of Mathematics and Physics*, 10(1–4), 191–236.
   doi:10.1002/sapm1931101191.
   —— 正实函数定理；§2.1（经 Try1 §2.1 引入）。
8. **Hurvich, C. M., & Tsai, C.-L. (1989).** "Regression and time series
   model selection in small samples." *Biometrika*, 76(2), 297–307.
   doi:10.1093/biomet/76.2.297.
   —— AICc 公式来源（§6.2，Try2 中退化为常数偏移）。
9. **相关工作对照（EIS 自动模型发现）**：
   (a) Chen, Y., Jiang, J., et al. (2025). "Auto-EIS: Automated discovery of
   equivalent circuit models from impedance spectroscopy via deep
   reinforcement learning."（Deep-RL 路线）；
   (b) "AutoEIS: automated Bayesian model selection and analysis for
   electrochemical impedance spectroscopy." arXiv:2305.04841（贝叶斯模型
   选择路线，JOSS 软件 DOI:10.21105/joss.06256）。
   —— 二者均为"参数未知 + 拟合/搜索"设定；Try2 的"参数已知 ⟹ 免拟合穷举 +
   完备性保证"是正交设定：其搜索的拓扑空间恰是本枚举空间的子集（串并联树）。
10. **Try1 设计文档**：`../Try1-Completely unknown single port fitting/DESIGN.md`
    §5.1/§8.1（误差度量规范，本文 §6.1 逐式沿用）、§2.1–2.3（正实性、
    可辨识性、综合定理）、Foster/Cauer/Bott–Duffin 及 SK 迭代参考文献。

## 附录 B：主流程伪代码

```python
def identify(components, f, z, config):
    w = 1/|z|; s = 2j*pi*f
    structures = enumerate_structures(E)              # 阶段一（缓存）
    state = FunnelState(...)
    for st in structures:                             # 阶段二（流式）
        stamps = StructureStamps(st, components)
        for batch in chunks(iter_assignments(st, components)):
            z_probe = stamps.z_full(batch, s[probe])  # F4: 3 频点
            state.update(batch, weighted_rss(z_probe, z_probe_meas, w_probe))
    survivors = state.final_keep()                    # chi2 <= best*1e6
    candidates = evaluate_candidates(survivors, ...)  # F5: 全频点
    classes = rank_and_cluster(candidates, ...)       # F6: 等价类+二级判据
    return IdentifyResult(classes, ...)
```
