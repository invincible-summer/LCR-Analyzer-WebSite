# LCR 未知单端口网络识别算法 — 设计文档

版本 v1.0 · 2026-09-02 · 状态：**P1 已实现并验证通过**（56 测试全绿，12 合成 DUT 全恢复）

> 目标读者：审查者 + 后续维护者。本文档记录每一次设计决策（标记为 **决策 Dn**）、
> 其数学推理与合理性证明。所有公式与 `rlc_id/` 代码逐项对应；改算法先改本文档。

---

## 0. 摘要

输入：不同频率正弦激励下测得的复阻抗离散点 $\{(f_k, \hat z_k)\}_{k=1}^{M}$。
输出：排序的候选等效电路列表（拓扑 + 元件值 + 拟合优度）。

核心结论（后文逐一证明）：

1. 任意无源 RLC 单端口的阻抗 $Z(s)$ 必为**正实有理函数**（§2.1），因此搜索空间
   不是"所有图"（超指数），而是"正实有理函数空间"（连续自由度 $2n+2$ + 整数阶 $n$）。
2. $Z(s)$ 可辨识，但**拓扑不可辨识**：同一 $Z(s)$ 有无穷多等价实现（§2.2）。因此
   算法的正确输出是**等价类 + 二级判据排序**，而非唯一"最优电路"。
3. 经典综合定理（Foster / Cauer / Bott–Duffin，§2.3）保证：枚举**规范串并联树**
   （§4）+ 极点分组实现族（§6.4）即可覆盖实践中所有常见情形，无需枚举任意图。

算法为**双引擎**架构：

- **引擎 A（topology-first）**：规范枚举 $n \le N_{\max}$ 个元件的全部串并联拓扑，
  对每个拓扑做复数域加权最小二乘参数拟合，按 AICc 排序（§4、§5）。
- **引擎 B（function-first）**：先把 $\hat z_k$ 拟合成有理函数（SK 迭代 / vector
  fitting），由极点结构与留数经 Foster / Cauer 综合**闭式**构造候选实现，并把
  极点数/极点类型回传给引擎 A 做剪枝（§6）。
- **选择器**：AICc 主排序 + 等价类合并 + 二级判据（§5.5、§7）。

---

## 1. 问题形式化

### 1.1 数据模型

通过正弦拟合 / FFT 从 $u(t), i(t)$ 得到每个激励频率 $f_k$ 处的复阻抗估计

$$
\hat z_k = Z(j\omega_k) + \varepsilon_k, \qquad \omega_k = 2\pi f_k,\quad k = 1..M,
$$

其中 $Z(s) = V(s)/I(s)$ 为被测件（DUT）的真实阻抗。

### 1.2 假设

- **A0（对象）**：DUT 为集中参数、线性、时不变、**无源**单端口；元件取值于
  $\{R, L, C\}$，值为正实数；拓扑未知、元件数未知。不含互感、受控源、分布参数。
- **A1（规模）**：实际可辨识的元件数 $\le 6$（引擎 A 默认 $N_{\max}=4$，可配）。
  依据：频带有限 + 噪声存在时，阶数 $\ge 7$ 的有理模型不可辨识（§2.2 限带讨论）。
- **A2（采样）**：频点对数等距，$M \ge 4n$（$n$ 为阶数）；频带覆盖主要特征
  （谐振点、转折频率）±2 个十倍频程。
- **A3（噪声）**：$\varepsilon_k$ 为复高斯，实虚部独立，$\sigma_k$ 已知时用之；
  未知时取相对模型 $\sigma_k \approx \sigma_0 |\hat z_k|$（实测中相对误差主导）。
- **A4（频响）**：激励幅值处于 DUT 线性区。

### 1.3 目标

最小化加权复残差并惩罚模型复杂度：

$$
\chi^2(\theta; \mathcal{T}) = \sum_{k=1}^{M} w_k^2 \big|\hat z_k - Z_{\mathcal{T}}(j\omega_k; \theta)\big|^2,
\qquad
\text{在候选拓扑 } \mathcal{T} \text{ 间用 AICc 排序（§5.5）。}
$$

**决策 D0（双引擎架构）**：引擎 A 在元件数少时完备且鲁棒（直接拟合 $Z(f)$，
无需先做有理逼近，不引入额外近似误差）；引擎 B 给出阶数与极点结构（用于剪枝），
并在元件数多、枚举爆炸时仍可扩展。二者互为交叉验证。

---

## 2. 数学基础：四个决定算法形态的事实

### 2.1 T1 — 阻抗必为正实有理函数

**定理（Brune 1931）**：由有限个正实 $R, L, C$ 任意连接构成的单端口，其阻抗

$$
Z(s) = \frac{N(s)}{D(s)}
$$

是**正实（positive-real, PR）有理函数**：(i) 实系数；(ii) $\operatorname{Re} s > 0 \Rightarrow \operatorname{Re} Z(s) \ge 0$。
McMillan 阶数 $n \le n_L + n_C$（无纯电感割集 / 纯电容回路时取等号）。

**证明思路（能量论证）**：取 $v(t) = \operatorname{Re}[V e^{st}]$，$\operatorname{Re} s > 0$，
则无源性要求网络在任意时刻吸收的能量非负；由 Tellegen 定理可得
$\operatorname{Re}[Z(s)]\,|V|^2 \ge 0$。标准结果，证明见 Brune (1931) 或任何网络综合教材。

**推论（极点的限制）**：

- 极点全部在闭左半平面（无源性 $\Rightarrow$ 稳定）；
- $j\omega$ 轴上的极点必为单阶且留数为正实数（LC 型极点）；
- $Z(\infty)$ 只能是 $0$、正常数或 $s$ 的线性增长（对应串联 C、串联 R、串联 L 端接）；
  $Z(0)$ 对偶。

**意义**：识别问题 = 在 PR 有理函数族中做系统辨识。搜索空间的连续自由度只有
$2n+2$（分子分母系数差一个尺度），外加一个整数阶 $n$。这就是把"图枚举"
降为"阶数选择 + 有理拟合 + 实现枚举"的理论根据。

### 2.2 T2 — 可辨识性与等价性

**(a) $Z(s)$ 可辨识**：阶数 $\le n$ 的有理函数由 $2n+2$ 个实自由度决定；$M$ 个
频点提供 $2M$ 个实方程。无噪且 $2M \ge 2n+2$ 时 $Z(s)$ 由有理插值唯一确定。
有噪时为统计估计问题（§5）。

**(b) 限带修正**：极点远离频带时不可辨识。一阶示例：$|p| \gg \omega_{\max}$ 时

$$
\frac{r}{s - p} \approx -\frac{r}{p}\Big(1 + \frac{s}{p}\Big),
$$

即一个带外实极点在带内的贡献与"串联电阻 $-r/p$ + 串联电感 $-r/p^2$"**不可区分**。
**结论：我们只能识别"带内等价模型"；频带设计（A2）决定可辨识阶数上限。**

**(c) 拓扑不可辨识**：同一 $Z(s)$ 有无穷多实现。例：

- 平凡合并：$R_1 + R_2$ 串联 $\equiv$ 单个 $R_1+R_2$（启发枚举规则 R2，§4.1）；
- Foster I 型与 Foster II 型实现同一函数（§6.2，经典对偶定理）；
- Y-Δ 变换及更一般的等价网络。

**推论（问题适定化）**：*不存在*能"用拟合优度区分等价拓扑"的算法——它们对
任意频率的响应完全相同。因此本算法的输出定义为：**按拟合优度排序的等价类代表
列表 + 等价类内部按二级判据排序**（§5.5）。这是整个设计的适定性前提。

### 2.3 T3 — 经典综合定理 ⇒ 枚举完备性

- **Foster I (1924)**：对 $Z(s)$ 做部分分式展开，每一项对应一个**串联**子节
  （$R$、$L$、$C$、$R\|C$、$R\|L\|C$ 槽路），闭式映射见 §6.2。
- **Foster II**：对偶，对 $Y(s) = 1/Z(s)$ 展开，每项对应一个**并联**支路
  （$R$、$L$、$C$、$R\!-\!L$ 串联支路、$R\!-\!L\!-\!C$ 串联支路）。
- **Cauer I/II (1926)**：连分式展开 $\Rightarrow$ 梯形网络。梯形 ⊂ 串并联。
- **Bott–Duffin (1949)**：任意 PR 函数都存在无互感 RLC 实现（可能要桥式与冗余元件）。

**已知边界（诚实声明）**：部分二阶 PR 函数（biquadratic）不存在元件数 $\le 3$ 的
串并联实现，必须引入桥式（Bott–Duffin 原始构造即含桥）。在本项目面向"实际元件
+ 夹具寄生"的表征场景中，测量数据对应的模型阶数低、且绝大多数可由串并联实现；
桥式枚举列为 P3 扩展（§10），引擎 B 检测到无法串并联实现的二阶节时会显式标注，
不会静默给出错误结果（决策 D8，§6.2）。

**推论（剪枝总纲）**：§4 的规范串并联树枚举 + §6.4 的极点分组实现族，覆盖了
实践中全部常见情形；对任意图（节点数 $m$）的朴素枚举数量为
$2^{\binom{m}{2}} \cdot 3^{|E|} \cdot (\text{端口选择})$，$m=6$ 时即超 $10^{10}$，
既无必要（T3）也不可行（组合爆炸）。**这是我们拒绝朴素图枚举的严格理由。**

### 2.4 T4 — 对偶性

串联 $\leftrightarrow$ 并联、$Z \leftrightarrow Y$、$L \leftrightarrow C$、
$R \leftrightarrow 1/R$（电导）。所有结论与代码只需实现一半，另一半由对偶生成；
枚举量天然减半。

---

## 3. 起点：纯串联与纯并联（直觉的形式化）

**纯串联**：$Z(s) = \sum_i Z_i(s)$。**纯并联**：$Y(s) = \sum_i Y_i(s)$。

**关键观察**：PR 函数的极点留数（$j\omega$ 轴上）为正，故串联求和时**各支路极点
不会相消**；而部分分式分解唯一。因此：

> **纯串联网络的识别 ≡ $Z(s)$ 的部分分式分解 —— 闭式解，无需搜索。**
> 对偶地，纯并联 ≡ $Y(s)$ 的部分分式分解。

这是引擎 B 的特例，也是"函数优先"方法高效的第一证据：串/并联结构在部分分式
层面是**可分离**的，搜索只花在"哪些项归为一组串联、哪些项先取倒数再分组"
（即 Foster I / II 的混合，§6.4）。

**两元件全表（6 种）**：串联 RL、RC、LC 与对偶的并联 RL、RC、LC。其 Nyquist /
Bode 特征（半圆、垂直 / 水平渐近线、谐振峰）是快速粗分类与初值构造的依据
（§5.4 渐近启发式）。

**三元件**：按 §4.1 规则枚举恰有 **20** 种规范型（推导见 §4.2，代码验证）。
典型代表：$(R+L)\|C$（电感寄生模型）、$R + (L\|C)$、$R + (R\|C)$、
$R\|L\|C$（谐振槽路）等。

---

## 4. 引擎 A：串并联树的规范枚举（topology-first）

### 4.1 拓扑表示与规范化规则

**表示**：树。内部节点 $\in \{\mathrm{SER}, \mathrm{PAR}\}$；叶子 $= (\mathrm{type}, \mathrm{value})$，
$\mathrm{type} \in \{R, L, C\}$，$\mathrm{value} > 0$。顶层节点可为 SER 或 PAR（单叶子即 $n=1$）。
用户的图模型中"边 = 元件、节点 = 连接点"：串并联树正是一类可递归分解的图；
任意图（含桥式）的扩展接口预留在 `circuits.py`（P3），本期不枚举（§2.3 边界声明）。

**规范化规则（消除电气等价冗余，枚举"无重复无遗漏"的关键）**：

- **R1（层级交替）**：父子节点类型必须交替。`SER(SER(a,b),c)` 与 `SER(a,b,c)`
  阻抗全同，后者为代表元。
- **R2（同类合并）**：同一节点下同类型**叶子**至多 1 个。串联 $R_1+R_2 \equiv R$，
  $L_1+L_2 \equiv L$，$C_1C_2/(C_1+C_2) \equiv C$；并联对偶同理。
  ⟹ 每个节点下叶子数 $\le 3$ 且类型互异。注意规则只约束叶子：
  一个 SER 节点可以既有叶子 $R$ 又有含 $R$ 的 PAR 子树（二者电气不等价）。
- **R3（规范序）**：子树按其序列化字符串排序后存储（串联/并联满足交换律），
  两个树同构当且仅当规范串相等 ⟹ 去重为字典序查重，$O(1)$。

**定理（枚举的正确性）**：任何串并联 RLC 单端口的阻抗函数，在满足 R1–R3 的树
集合中存在**唯一**代表元。

**证明**：给构造性规范化映射 $\nu$：反复 (i) 展开同类型嵌套节点（R1，保持 $Z$ 不变，
因串联/并联结合律）；(ii) 合并同节点同类叶子（R2，保持 $Z$ 不变，由上列恒等式）；
(iii) 子树按规范串排序（R3，保持 $Z$ 不变，交换律）。每步保持阻抗不变，且规则
合流（confluent）且终止（叶子数有限、每步严格减少嵌套或叶子计数）。故任意树
映射到唯一规范形；反之每个规范树是合法的串并联网络。∎

**意义**：枚举空间 = 规范树集合，不含任何"换皮重复"。这正是把用户"枚举图"
直觉落地的严格版本：枚举的是**电气等价类的代表元**。

### 4.2 生成算法与计数

递归生成：对叶子数预算 $n$，枚举顶层节点类型 $\times$ 子树叶子数拆分（整数划分，
要求子节点类型交替、叶子类型互异、规范序）。伪代码见附录 B.1。

计数（$n$ = 元件数，`rlc_id.library.stats()` 实测，`tests/test_library.py`
断言锁定）：

| $n$ | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| 规范拓扑数（内部深度 ≤ 2，默认库） | 3 | 6 | 20 | 36 | 54 | 78 |
| 规范拓扑数（内部深度 ≤ 3，供参考） | 3 | 6 | 20 | 90 | — | — |

> **实现注记（深度上限）**：默认枚举库限制非根内部节点的嵌套深度 ≤ 2
> （`library.DEFAULT_MAX_IDEPTH = 2`），即只允许"一层串/并联套一层"。这覆盖了
> §8.2 全部 12 个 DUT 与 Foster I/II 的全部输出形式，并恰好复现手算锁定的
> 3/6/20/36。放开深度到 3 后 $n=4$ 为 90 种（多出 $S(R, P(R, S(R,C)))$ 类深嵌套）。
> 深度参数可调（`library.get_library(max_n, max_idepth)`），属精度/耗时权衡。

手算验证 $n=3$：顶层 SER 的叶子数拆分为 $\{1,1,1\}$（三叶类型互异：仅
$\{R,L,C\}$，1 种）与 $\{1,2\}$（单叶 3 选 1 × PAR 双子组 2 选异型
$\binom{3}{2}=3$，共 9 种）；PAR 顶层对偶同数；合计 $(1+9)\times 2 = 20$。✓
（$n=2$：SER 二叶异型 $\binom{3}{2}=3$，PAR 同，共 6 ✓）

**对照**：朴素图枚举（$\le n+2$ 节点的无标号图 × 边类型指派）上界约
$2^{\binom{n+2}{2}}\cdot 3^{n}$，$n=4$ 时已 $>10^{9}$；规范树枚举仅 36。
**减支收益：$\ge 7$ 个数量级（剪枝 1）。**

### 4.3 参数化与阻抗求值

- 参数向量 $\theta_i = \log_{10}(\mathrm{value}_i)$：正性自动满足；各参数量纲差异
  （$\Omega$ / H / F 跨 10 余个数量级）被拉平，数值条件大幅改善。
- 搜索域（可配，面向 ESP32 实测场景）：$R \in [10^{-3}, 10^{7}]\,\Omega$，
  $L \in [10^{-10}, 10^{1}]\,$H，$C \in [10^{-13}, 10^{-3}]\,$F。
- 求值递归：$\mathrm{SER}: Z = \sum Z_c$；$\mathrm{PAR}: Z = 1/\sum(1/Z_c)$；
  叶：$R$、$sL$、$1/(sC)$，$s = j\omega$。numpy 向量化（所有频点同时），
  单次求值 $O(nM)$，$n, M$ 均小，枚举全库成本可忽略（实测 §10）。

---

## 5. 拟合与拟合优度（引擎 A 的数值核心）

### 5.1 残差与权重

$$
r_k(\theta) = w_k\,\big(\hat z_k - Z(j\omega_k; \theta)\big),\qquad
\mathbf{r} = [\operatorname{Re} r_1, \operatorname{Im} r_1, \dots]^\top \in \mathbb{R}^{2M}.
$$

权重 $w_k = 1/\sigma_k$（已知噪声）或 $w_k = 1/|\hat z_k|$（默认，相对误差模型 A3）。

**决策 D4（复数域直接拟合，不拆幅值/相位）**：相位在 $|z| \to 0$ 处病态且有
$2\pi$ 卷绕；幅值-相位拆分在过零附近梯度爆炸。复残差处处良态，且与 A3 的
高斯模型严格一致（复高斯 = 实虚部联合高斯）。报告中另输出幅值/相位误差供人读。

### 5.2 Jacobian：前向自动微分（复步长法的修正）

**设计修正（P1 实现期发现）**：最初采用复步长法（complex-step）
$\partial Z/\partial\theta_i = \operatorname{Im} Z(\theta + i h e_i)/h$，
它要求函数在实参数处取**实值**（$\operatorname{Im} Z(\theta)=0$）。但本问题中
$Z(j\omega)$ 本身就是复数，$\operatorname{Im} Z(\theta + i h e_i)$ 被
$\operatorname{Im} Z(\theta)$ 的 $O(1)$ 部分淹没，除以 $h=10^{-20}$ 后完全失真
——实测残差雅可比各行全零、优化器停滞。因此改用**前向模式自动微分**：
沿同一棵树递归同时传播 $Z$ 与 $\partial Z/\partial\theta_i$，规则为

$$
\begin{aligned}
&\text{叶：} R \to \partial_v = 1,\quad
  sL \to \partial_v = s,\quad
  1/(sC) \to \partial_v = -\frac{1}{s v^2};\\[2pt]
&\text{SER：} \partial Z = \textstyle\sum \partial Z_c;\\[2pt]
&\text{PAR：} Z = \Big(\textstyle\sum Z_c^{-1}\Big)^{-1}
  \;\Rightarrow\; \partial Z = Z^2 \sum \frac{\partial Z_c}{Z_c^2};
\end{aligned}
$$

再乘上 $\mathrm{d}v/\mathrm{d}\theta = \ln(10)\cdot v$（$\log_{10}$ 参数化）。
每次求值同时得到精确到机器精度的雅可比，成本仅为 1 次树求值（比复步长的
$p$ 次更省）。代码：`circuits.evaluate_jac`；正确性测试：
`tests/test_circuits.py::test_evaluate_jac_matches_fd`（与中心差分一致到
$10^{-9}$ 相对精度）。


**决策 D5（求解器）**：`scipy.optimize.least_squares(method='trf', bounds)`，
损失默认 `linear`；实测有粗差时切 `soft_l1`（开关保留）。多起点策略：
LHS（拉丁超立方）$n_{\text{start}}$ 个 + 1 个渐近启发式起点（§5.4），两阶段：
粗筛（全部拓扑 × 少起点）→ 精修（前 20% × 全起点）。

### 5.3（并入 5.2）

### 5.4 渐近启发式初值与结构预判

低频端 / 高频端的 $|Z|$ 斜率（0 或 $\pm 20$ dB/dec）与相位（$\to 0 / \pm 90°$）
决定端接元件类型与量级，例如：

| 观测 | 结论 |
|---|---|
| $\omega \to 0$: $|Z| \to$ 常数 | 无串联 C、无并联 L 直流通路问题，首端接含 R |
| $\omega \to 0$: $|Z| \propto 1/\omega$ | 存在串联 C（或阻断直流的结构） |
| $\omega \to \infty$: $|Z| \propto \omega$ | 存在串联 L 端接 |
| 中频谐振峰 | 存在 LC 对，$\sqrt{LC} \approx 1/\omega_0$ |

用途二合一：(i) 构造拟合初值；(ii) **剪枝 3**——端接结构与观测矛盾的拓扑直接跳过。

### 5.5 拟合优度与模型选择

设 $n_{\text{obs}} = 2M$（实虚部堆叠），$K = p + 1$（$p$ 个元件参数 + 1 个方差参数）：

$$
\mathrm{AICc} = n_{\text{obs}} \ln\frac{\mathrm{RSS}}{n_{\text{obs}}} + 2K
+ \frac{2K(K+1)}{n_{\text{obs}} - K - 1},
\qquad \mathrm{RSS} = \|\mathbf r\|^2 .
$$

推导：高斯噪声下 ML 对数似然 $\Rightarrow$ AIC（Akaike 1974）；小样本二阶修正
（Hurvich–Tsai 1989）。$n_{\text{obs}} \gg K$ 时退化为 AIC。

**决策 D6（选择协议）**：

1. 主排序：AICc 升序。
2. $\Delta\mathrm{AICc} < 2$ 视为统计不可区分，全部报告。
3. **等价类合并**：两个拟合结果在加密校验网格（频带外扩 10 倍、200 点）上
   $\max_k |Z_1 - Z_2|/|Z_1| < 10^{-3}$ 则判等价（T2 的数值实现），合并为一行，
   类内按二级判据排序：元件数少者优先 → 元件值物理合理性 → FIM 条件数
   （参数可辨识性，P3 输出不确定度时启用）。
4. 报告 top-$k$：拓扑、参数、加权 RMSE、最大相对误差、AICc、等价标注。

---

## 6. 引擎 B：有理拟合 + 经典综合（function-first）

### 6.1 有理拟合（Sanathanan–Koerner 迭代 = 简化 vector fitting）

模型

$$
Z(s) \approx \frac{N(s)}{D(s)},\quad
N = \sum_{m=0}^{n} a_m \varphi_m(s),\quad
D = \sum_{m=0}^{n} b_m \varphi_m(s),\; b_n = 1,\quad
\varphi_m(s) = (s/\omega_0)^m,
$$

频率归一化 $\omega_0 = \exp(\mathrm{mean}\,\ln \omega_k)$（条件数，决策 D7a）。

**SK 迭代**（Sanathanan–Koerner 1963；Gustavsen–Semlyen 1999 的 vector fitting
即其极点重定位变体）：给定 $D^{(0)} \equiv 1$，反复解线性最小二乘

$$
\min_{a,b}\; \sum_k w_k^2\,
\frac{\big|N(s_k) - \hat z_k D(s_k)\big|^2}{|D^{(\mathrm{prev})}(s_k)|^2},
$$

实系数（实虚堆叠）；极点 $=$ roots$(D)$；不稳定极点翻折
$\operatorname{Re} p \to -\operatorname{Re} p$（保幅值近似，PR 约束的数值投影）；
10–20 次收敛。随后极点固定、线性重估留数：

$$
Z(s) = e\,s + d + \sum_i \frac{\rho_i}{s - p_i}
$$

（$\{e, d, \rho_i\}$ 线性，加权 LS 一步得）。

**阶数选择**：$n = 1..N_{\max}$ 扫描，按 AICc 选阶；剔除"近相消极点-零点对"
（留数 $|\rho_i| \ll$ 尺度的极点，对应不可见模态）。

**剪枝 2（极点可行性回传引擎 A）**：极点数 $n_p$ ⟹ 候选拓扑的储能元件数下限
（每个实极点至少 1 个、每对共轭极点至少 2 个储能元件）；极点全在负实轴 ⟹
RC-only 或 RL-only 子库（T1 推论），直接砍掉含 L（或含 C）的拓扑。

### 6.2 Foster 实现（部分分式 → 电路，闭式映射）

**Foster I（$Z$ 展开，全部串联）**：

| 部分分式项 | 条件 | 节 | 元件公式 |
|---|---|---|---|
| $e\,s$ | $e>0$ | 串联 $L$ | $L = e$ |
| $k_0/s$ | $k_0>0$ | 串联 $C$ | $C = 1/k_0$ |
| $d$ | $d \ge 0$ | 串联 $R$ | $R = d$ |
| $\rho/(s+a)$，$a>0$ | $\rho>0$ | 串联 $R\|C$ | $C = 1/\rho,\; R = \rho/a$ |
| 共轭对 $\dfrac{2(\rho_r s + c)}{(s+\alpha)^2+\beta^2}$，$c \equiv \rho_r\alpha - \rho_i\beta$ | $c = 0$ 且 $\rho_r>0$ | 串联 $R\|L\|C$ 槽路 | $C = \frac{1}{2\rho_r},\; L = \frac{2\rho_r}{\alpha^2+\beta^2},\; R = \frac{\rho_r}{\alpha}$ |

推导示例（$R\|C$ 行）：$Z_{R\|C} = \dfrac{R}{1+sRC} = \dfrac{1/C}{s + 1/(RC)}$，
对照 $\rho/(s+a)$ 得 $C = 1/\rho$，$R = \rho/a$。✓ 其余行同法（纯代数对照）。

**复极点的重要警示**：共轭对项的分子一般含常数项 $c = \rho_r\alpha - \rho_i\beta$，
而 $R\|L\|C$ 槽路的分子正比于 $s$（无常数项）。$c \neq 0$ 时简单 Foster I 失败——
这正是 Bott–Duffin 定理由来。**决策 D8**：检测到 $c \neq 0$ 时不强行综合，
显式标注"需 Bott–Duffin / 桥式"，交由引擎 A 的数值拟合兜底（实际数据中该情形
罕见，且引擎 A 在 $n \le 4$ 完备覆盖）。宁缺毋滥：绝不输出元件为负的"伪实现"。

**Foster II（$Y = 1/Z$ 展开，全部并联，对偶表）**：

| $Y$ 的项 | 条件 | 并联支路 | 元件公式 |
|---|---|---|---|
| $e' s$ | $e'>0$ | $C$ | $C = e'$ |
| $k_0'/s$ | $k_0'>0$ | $L$ | $L = 1/k_0'$ |
| $d'$ | $d'>0$ | $R$ | $R = 1/d'$ |
| $\rho'/(s+a')$ | $\rho'>0$ | $R\!-\!L$ 串联支路 | $L = 1/\rho',\; R = a'/\rho'$ |
| 共轭对，$c' = 0$ | $\rho'_r>0$ | $R\!-\!L\!-\!C$ 串联支路 | $L = \frac{1}{2\rho'_r},\; C = \frac{2\rho'_r}{\alpha'^2+\beta'^2},\; R = \frac{\alpha'}{\rho'_r}$ |

### 6.3 Cauer 连分式 → 梯形网络

在 $s \to \infty$（Cauer I）或 $s \to 0$（Cauer II）对 $Z = N/D$ 做辗转相除：

$$
Z = z_1 + \cfrac{1}{y_2 + \cfrac{1}{z_3 + \cfrac{1}{\ddots}}}
$$

每步多项式除法取商：$\alpha s \Rightarrow$ 串联 $L$（或并联 $C$，视奇偶层）；
常数 $\beta \Rightarrow$ 串联 $R$（或并联 $R$）。梯形 ⊂ 串并联 ⟹ 引擎 A 枚举空间
自动包含 Cauer 型；此处独立展开作为**交叉验证**（两条路径应给出等价 $Z(s)$）。
商出现负值 ⟹ 该方向不可实现，换对偶方向或放弃（与 D8 同一原则）。

### 6.4 等价实现族枚举（"枚举图结构"的数学完备版）

固定有理拟合结果后，部分分式项集 $\{T_1, \dots, T_q\}$ 满足
$Z = \sum T_i$。把所有项做**二分划分** $\{T\} = S \sqcup B$：

- 若 $Z_S = \sum_{T_i \in S} T_i$ 与 $Z_B$ 各自 PR（密网格检验
  $\operatorname{Re} Z(j\omega) \ge -\epsilon$，PR 的实用判据），则
  $\mathrm{SER}(\text{实现}(Z_S), \text{实现}(Z_B))$ 是合法实现，且可递归细分；
- 对偶地，对 $Y = 1/Z$ 的项集做划分得并联分组。

枚举量 $\sim$ Bell$(q)$（$q$ = 项数，通常 $\le 5$），且 PR 检验大幅缩减合法划分。
**关键性质：族内所有实现的 $Z(s)$ 完全相同 ⟹ 拟合优度完全相同**（T2 的具体化）；
族内排序只能依赖二级判据（元件数、物理合理性、灵敏度）。这把"枚举图结构"
放进了数学完备的轨道：我们枚举的不是任意图，而是**同一传递函数的全部
可分离实现**，它们才是彼此不可区分的真正候选集。

---

## 7. 减支策略汇总（6 级过滤器）

按执行次序形成的漏斗（全部有定理/数学性质保证，不丢解）：

| 级别 | 减支机制 | 依据 | 过滤量（估算） |
|---|---|---|---|
| F1 | 规范串并联树枚举（R1–R3） | §4.1 定理（电气等价类代表元唯一） | $\ge 10^7$（图 $\to$ 树） |
| F2 | 渐近与 Bode 斜率过滤 | §5.4（端接类型与渐近行为矛盾） | $\sim 50\%$ |
| F3 | 极点结构与符号剪枝 | 引擎 B（实极点/共轭对数 $\Rightarrow$ 储能元件下限） | $\sim 60\%$ |
| F4 | 两阶段优化（粗筛少起点 $\to$ 精修） | §5.2（明显不合拓扑在粗筛即被甩开） | 计算量省 $70\%$ |
| F5 | 元件值出界淘汰 | 物理先验（超界视为退化为开路/短路，即退化为低阶拓扑） | 优化中自然发生 |
| F6 | AICc + 等价类合并 | §5.5（统计惩罚 + 密网格等价判定） | 压缩输出为代表集 |

---

## 8. 验证方案（双引擎交叉 + 闭环自测）

### 8.1 验证指标

设拟合阻抗为 $Z_{\text{fit}}(f_k)$，真实（或测得）为 $\hat z_k$：

- **加权 RMSE**：$\mathrm{wRMSE} = \sqrt{\frac{1}{M}\sum_{k=1}^M \big|(\hat z_k - Z_{\text{fit}})/\hat z_k\big|^2}$；
- **最大相对误差**：$e_{\max} = \max_k |(\hat z_k - Z_{\text{fit}})/\hat z_k|$；
- **元件值相对误差**（合成数据）：$e_{\text{param}} = \max_i |\theta_i^{\text{fit}} - \theta_i^{\text{true}}| / \theta_i^{\text{true}}$；
- **拓扑准确率**（合成数据）：真值拓扑是否排进 top-1（或在 top-1 的等价类中）。

### 8.2 闭环自测套件（8 类典型 DUT，覆盖低中高复杂度）

1. **单元件**：$R=100\,\Omega$、$L=1\,\mathrm{mH}$、$C=10\,\mathrm{nF}$；
2. **串联二元件**：$R=50\,\Omega + L=1\,\mathrm{mH}$、$R=1\,\mathrm{k}\Omega + C=100\,\mathrm{pF}$；
3. **并联二元件**：$R=1\,\mathrm{k}\Omega \| C=10\,\mathrm{nF}$、$R=50\,\Omega \| L=100\,\mu\mathrm{H}$；
4. **电感寄生模型（3 元件）**：$(R_s=1\,\Omega + L=10\,\mu\mathrm{H}) \| C_p=50\,\mathrm{pF}$（自谐振 $\approx 7.1\,$MHz，在带内）；
5. **电容寄生模型（3 元件）**：$R_{\mathrm{esr}}=0.05\,\Omega + L_{\mathrm{esl}}=2\,\mathrm{nH} + C=10\,\mu\mathrm{F}$；
6. **串并联混联（3 元件）**：$R_1 + (R_2 \| C)$（一阶弛豫介质模型）；
7. **二阶槽路（3 元件）**：$R \| L \| C$；
8. **四元件双峰**：$R_1 + (L_1 \| C_1) + (L_2 \| C_2)$。

测试条件：合成数据生成，加 $0.5\%$ 相对复高斯噪声，频率 $10\,\mathrm{Hz} \sim 10\,\mathrm{MHz}$，
30 点对数等距。断言：

- top-1 拓扑正确（或在等价类中）；
- 各元件值相对误差 $< 2\%$（无噪时 $< 10^{-4}$）；
- 引擎 A 与引擎 B 对二阶系统输出同构/等价解（交叉验证）。

---

## 9. 软件架构与模块设计

```
AlgorithmLcr/
├── DESIGN.md                 # 本文档
├── rlc_id/
│   ├── __init__.py           # 对外接口：identify(f, z, weights=None, config=None)
│   ├── circuits.py           # 串并联树数据结构、Z(s) 向量化求值、参数化、规范化 (R1-R3)
│   ├── library.py            # 规范串并联拓扑枚举器、预生成拓扑库、计数统计
│   ├── fit_engine_a.py       # 引擎 A：加权复残差、前向 AD 精确 Jacobian、多起点 TRF、AICc
│   ├── fit_engine_b.py       # 引擎 B：SK 迭代有理拟合、极点-留数分解、Foster I/II 综合
│   ├── pruning.py            # 渐近斜率估计、端接预判、极点结构与储能元件数剪枝
│   ├── selector.py           # 等价类判定、AICc 主排序、二级判据、Top-K 包装
│   ├── synthetic.py          # 合成 DUT 阻抗与波形生成器（带可控噪声）
│   ├── report.py             # 拟合报告生成（ASCII 排序候选表）
│   ├── adjacency.py          # 统一输出：候选电路 → 上三角邻接矩阵 + vector<Edge>
│   │                         # （规范见 ../../OUTPUT_FORMAT.md §5.1，独立实现）
│   └── iofmt.py              # 统一输入：测量数据 n / f Rz Iz 文本加载
│                             # （规范见 ../../INPUT_FORMAT.md §1/§2.1，独立实现）
├── tests/
│   ├── test_circuits.py      # 规范化唯一性、Z(s) 向量化正确性
│   ├── test_library.py       # 拓扑枚举计数断言 (n=1..4 逐项锁定)
│   ├── test_engine_a.py      # 单拓扑参数拟合精度 (无噪/有噪)
│   ├── test_engine_b.py      # SK 迭代有理拟合 + Foster 综合闭式恢复
│   ├── test_pruning.py       # 渐近斜率与极点剪枝不漏解
│   ├── test_adjacency.py     # 邻接矩阵形状/确定性/连通性 + 节点分析 Z 交叉验证
│   ├── test_iofmt.py         # 测量文本 round-trip 逐位一致 + 校验错误用例
│   └── test_end_to_end.py    # 8 类典型 DUT 的端到端识别（§8.2）
└── demo.py                   # 命令行可执行示例：从合成测量到拓扑+参数输出
```

---

## 10. 实现路线与当前进度

- **P1（已完成，2026-09-02 验证通过）**：
  - [x] 形式化与数学证明写入 `DESIGN.md`（§1–§8）
  - [x] 串并联树表示 + 规范化 R1–R3（`circuits.py`）
  - [x] 拓扑枚举器（默认 $n \le 4$、深度 ≤ 2，`library.py`）+ 计数测试锁定
  - [x] 引擎 A：复数域加权拟合 + **前向 AD 精确 Jacobian** + 多起点
        （`fit_engine_a.py`；复步长 → AD 的设计修正见 §5.2）
  - [x] 引擎 B：SK 有理拟合 + 极点留数 + Foster I/II 综合（`fit_engine_b.py`）
  - [x] 剪枝 F1–F5 + 选择器与等价类判定（`pruning.py`, `selector.py`）
  - [x] 8 类 DUT 的端到端测试与 demo（`tests/`, `demo.py`）
- **P2（后续）**：Cauer 综合完整梯形链、Bode 图/Nyquist 图导出、与 ESP32 契约集成
- **P3（扩展）**：Bott–Duffin 完整综合器（支持任意 PR 双二次函数）、FIM 参数置信区间。

### 10.1 P1 实测验证结果

运行环境：conda env `lcr`（Python 3.11，numpy 2.4.6，scipy 1.17.1），WSL2。

- `python -m pytest tests/ -q`：**56 passed**（约 60 s）。
- `python demo.py`（0.5% 噪声）：**12/12 恢复**——11 个精确拓扑命中（参数最大
  相对误差 ≤ 9.8e-3，多数 < 2e-3），1 个（dut6_relaxation）落入电气等价类
  （T2 预期行为，等效实现在扩展频带上与真值最大相对偏差 < 2%）。
- `python demo.py --noiseless`：**12/12 精确命中**，参数误差 ≤ 1.4e-14
  （机器精度）；wRMSE ≤ 1e-14。
- 耗时：全部 12 个 DUT（含枚举 + 双引擎 + 选择器）合计约 25 s，单 DUT ≤ 3.3 s。

关键实现要点（与初稿的偏差说明）：

1. **§5.2 复步长 → 前向 AD**：见该节"设计修正"。
2. **D10（引擎 B 阶数选择）**：最终采用**差异度原则**——以最佳拟合的
   每自由度 RSS 估计噪声底，选择不超过其 $\chi^2$ 3σ 波动上界的**最低阶**
   模型；纯 AICc 在 30 点 × 0.5% 噪声下会偶发追高阶（噪声拟合）。
3. **F3 保守化**（`conservative_energy_bound`）：极点界取所有
   ΔAICc ≤ 10 的备选有理模型中的**最小**储能元件数，保证 F3 只可能漏剪、
   绝不误剪真实拓扑。
4. **选择器噪声一致简约**：`selector.rank_and_cluster_equivalent` 先用
   差异度原则圈定"噪声一致"候选集，再在集合内取**最少参数**者为代表；
   等价类容差自适应为 $\max(10^{-3}, 3\hat\sigma_{\rm rel})$，避免把
   同一物理电路的两次独立噪声拟合误判为两个类（dut6 的教训）。
5. **谐振起点启发式**：|Z| 内部极值点检测 + 两端渐近线交点反推
   $\omega_0 = 1/\sqrt{LC}$；并联谐振时 $R$ 取峰值 `r_peak` 而非频段中位数
   （dut4/dut7 收敛性的关键）。

---

## 附录 A：参考文献

### A.1 网络综合理论（正实函数、Foster/Cauer/Bott–Duffin）

1. **Brune, O. (1931).** "Synthesis of a finite two-terminal network whose
   driving-point impedance is a prescribed function of frequency."
   *Journal of Mathematics and Physics*, 10(1–4), 191–236.
   doi:10.1002/sapm1931101191.
   —— 正实（positive-real）函数概念的奠基之作；证明"RLC 单端口 ⟺ PR 有理函数"
   的充分性方向，是本文 §2.1 T1 的原始出处。

2. **Foster, R. M. (1924).** "A reactance theorem."
   *Bell System Technical Journal*, 3(2), 259–267.
   doi:10.1002/j.1538-7305.1924.tb01358.x.
   —— Foster I/II 部分分式综合，本文 §2.3、§6.2 映射表的来源。

3. **Cauer, W. (1926).** "Die Verwirklichung der Wechselstromwiderstände
   vorgeschriebener Frequenzabhängigkeit."
   *Archiv für Elektrotechnik*, 17(4), 355–388.
   doi:10.1007/BF01656400.
   —— 连分式（梯形）综合，§6.3 的来源。

4. **Bott, R., & Duffin, R. J. (1949).** "Impedance synthesis without use of
   transformers." *Journal of Applied Physics*, 20(8), 816.
   doi:10.1063/1.1698532.
   —— 证明任意 PR 函数都存在无互感 RLC 实现（可能需桥式）；
   本文 §2.3 的边界声明与 P3 扩展的依据。

5. **Guillemin, E. A. (1957).** *Synthesis of Passive Networks.*
   John Wiley & Sons, New York.
   —— 网络综合经典教材；Foster/Cauer 映射表推导细节与二阶节条件的参考。

### A.2 有理拟合 / 系统辨识

6. **Sanathanan, C. K., & Koerner, J. (1963).** "Transfer function synthesis
   as a ratio of two complex polynomials."
   *IEEE Transactions on Automatic Control*, 8(1), 56–70.
   doi:10.1109/TAC.1963.1105517.
   —— SK 迭代原文，本文 §6.1 引擎 B 的有理拟合核心。

7. **Gustavsen, B., & Semlyen, A. (1999).** "Rational approximation of
   frequency domain responses by vector fitting."
   *IEEE Transactions on Power Delivery*, 14(3), 1052–1061.
   doi:10.1109/61.772353.
   —— 矢量拟合（VF）标准文献；本文实现采用其"极点重定位"变体
   （`fit_engine_b._vf_step`），即在极点基下的 SK 迭代。

8. **Levi, E. C. (1959).** "Complex-curve fitting."
   *IRE Transactions on Automatic Control*, 4(1), 37–43.
   doi:10.1109/TAC.1959.6429401.
   —— Levi 方法（SK 迭代的第一步特例），复数域曲线拟合的奠基工作。

### A.3 模型选择与统计判据

9. **Akaike, H. (1974).** "A new look at the statistical model
   identification." *IEEE Transactions on Automatic Control*, 19(6), 716–723.
   doi:10.1109/TAC.1974.1100705.
   —— AIC 原文，§5.5 的基线。

10. **Hurvich, C. M., & Tsai, C.-L. (1989).** "Regression and time series
    model selection in small samples." *Biometrika*, 76(2), 297–307.
    doi:10.1093/biomet/76.2.297.
    —— AICc 小样本二阶修正，本文实际使用的判据（§5.5 公式）。

11. **Burnham, K. P., & Anderson, D. R. (2002).** *Model Selection and
    Multimodel Inference: A Practical Information-Theoretic Approach*,
    2nd ed., Springer, New York.  doi:10.1007/b97636.
    —— ΔAICc < 2 视为"统计不可区分"的经验阈值来源（§5.5 决策 D6.2）。

12. **Hansen, P. C. (1992).** "Analysis of discrete ill-posed problems by
    means of the L-curve." *SIAM Review*, 34(4), 561–580.
    doi:10.1137/1034115.
    —— 差异度原则（discrepancy principle）的系统阐述；本文 D10 与
    选择器噪声一致简约的理论依据。

### A.4 数值微分与优化

13. **Squire, W., & Trapp, G. (1998).** "Using complex variables to estimate
    derivatives of real functions." *SIAM Review*, 40(1), 110–112.
    doi:10.1137/S003614459631241X.
    —— 复步长法原文。**注意**：本文 §5.2 记录了为何它对本问题失效
    （要求实值基函数，而 $Z(j\omega)$ 为复值），以及改用前向 AD 的修正。

14. **Nocedal, J., & Wright, S. J. (2006).** *Numerical Optimization*,
    2nd ed., Springer, New York.  doi:10.1007/978-0-387-40065-5.
    —— TRF（信赖域反射）算法与 box 约束最小二乘的参考；
    `scipy.optimize.least_squares(method='trf')` 的理论背景。

15. **McKay, M. D., Beckman, R. J., & Conover, W. J. (1979).** "A comparison
    of three methods for selecting values of input variables in the analysis
    of output from a computer code." *Technometrics*, 21(2), 239–245.
    doi:10.1080/00401706.1979.10489755.
    —— 拉丁超立方采样（LHS）原文，引擎 A 多起点策略的采样方法。


---

## 附录 B：算法伪代码

### B.1 规范树递归生成

```python
def generate_trees(n: int, parent_type: NodeType) -> List[Tree]:
    """生成元件数恰为 n、父节点为 parent_type 的全部规范子树列表。"""
    if n == 1:
        # 叶子：R, L, C 各一
        return [Leaf('R'), Leaf('L'), Leaf('C')]

    my_type = PAR if parent_type == SER else SER
    results = []

    # 拆分 n 为 k 个正整数划分 n = n_1 + ... + n_k, k >= 2
    for partition in integer_partitions(n, min_parts=2):
        # 递归求每个分量的合法子树（其父为 my_type）
        component_choices = [generate_trees(ni, my_type) for ni in partition]
        for combo in cartesian_product_with_canonical_order(component_choices):
            tree = Node(my_type, combo)
            if satisfies_rule_r2(tree):  # 同节点下无同类型叶子
                results.append(tree)
    return deduplicate_by_canonical_string(results)
```

### B.2 双引擎主流程

```python
def identify(f: np.ndarray, z: np.ndarray, weights=None, config=None):
    w = 2 * pi * f
    # 0. 剪枝前置：渐近斜率与端接预判
    features = extract_asymptotics(w, z)

    # 1. 引擎 B：有理拟合 + Foster 综合（获得极点特征与闭式候选）
    rat_model = sk_rational_fit(w, z, max_order=config.max_order)
    foster_candidates = synthesize_foster(rat_model)

    # 2. 剪枝过滤：基于 features 与 rat_model 的极点数
    library = get_topology_library(max_n=config.max_n)
    filtered_library = prune(library, features, rat_model)

    # 3. 引擎 A：在过滤库上多起点复残差拟合
    fit_candidates = fit_library(filtered_library, w, z, weights)

    # 4. 合并候选：引擎 A 结果 + 引擎 B 闭式候选
    all_candidates = fit_candidates + foster_candidates

    # 5. 选择器：等价类合并 + AICc 主排序 + 二级判据
    ranked_classes = rank_and_cluster_equivalent(all_candidates, w, z)

    return ranked_classes
```
