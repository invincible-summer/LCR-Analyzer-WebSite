# LCR v4 — 单端口 RLC 网络辨识理论与实现规范

2026-09，dev 分支。本文替代 v3 源码审计稿；旧源码及历史优化阈值已删除。
输入输出的文件结构仍由 INPUT_FORMAT.md / OUTPUT_FORMAT.md 约束。
本文区分数学结论、浮点诊断和经验算法，不把任一项替代另外两项。

## 1. 模型、先验与可观测性

目标是无源、线性、时不变、集总二端无向多重图，端口为节点 0/1。
允许同一节点对有多个元件，不允许自环；不建模互感、受控源、饱和、
磁滞、传输线和时变效应。取 s=j2πf：

\[
z_R=R,\quad z_C=(sC)^{-1},\quad z_L=R_d+sL,
\qquad R,L,C>0,\ R_d\ge0.
\]

L 与 DCR 绑定为一个器件；DCR=0 是真实边界，不能替换成正的最小阻值。

| 引擎 | 已知量 | 未知量 |
|---|---|---|
| Try2 Exact | 类型、数量、精确参数 | 活动二端图接线 |
| Try2 Tolerance | 类型、数量、标称值和显式容差 | 接线及容差箱内参数 |
| Try3 | 图和边类型 | 归约群的参数 |
| 内部 Try2.5 | 类型与数量 | 接线及宽参数箱内参数 |
| Try1 | 测量、可选规范数量 | 有界 SP 假设及参数 |

单端口通常只能识别端口行为，不能唯一识别物理内部结构。
例如 R1+R2 与一个等值 R、R+(Rd+sL) 与一个等值 L+DCR 不可区分。
故 exactN 是**规范不可约等效模型**器件数，不是物理 BOM 数量。
有限采样上的接近只称 observed-band equivalence，不能当作符号恒等。
普通图 2-isomorphism 不足以保证端口阻抗等价；还需保持端口分离两森林信息。

## 2. 统一前向方程与解析导数

以端口 0 接地，去掉接地行的关联向量为 a_e，边导纳 y_e=1/z_e。
对端口 1 注入 1 A，b 为其单位向量：

\[
Y(s)=\sum_e y_e(s)a_ea_e^T,\quad Yv=b,\quad Z=b^Tv.
\]

当 Y 正则时解唯一。无源器件构成的驱动点函数在其定义域满足正实性；
这不意味着任意正实函数有本项目有界 SP 假设中的实现。

对参数 q 微分 Yv=b，并用对称性 Y^T=Y：

\[
\frac{\partial Z}{\partial q}
=-v^T\frac{\partial Y}{\partial q}v
=-\frac{\partial y_e}{\partial q}(v_u-v_v)^2.
\]

这里是**转置，不是共轭转置**。

\[
\partial_R y_R=-R^{-2},\quad \partial_C y_C=s,\quad
\partial_L y_L=-s/(R_d+sL)^2,\quad
\partial_{R_d}y_L=-1/(R_d+sL)^2.
\]

`nodal.cpp` 是三个引擎唯一生产求值和 Jacobian 来源。
Eigen FullPivLU 解缩放矩阵，报告有效 rcond（LU 估计乘组装后矩阵最大模与
逐 stamp 模之和最大项的比率）与

\[
\rho_{back}=\|Yv-b\|/(\|Y\|\|v\|+\|b\|).
\]

状态为 OK / PORT_OPEN / SINGULAR / ILL_CONDITIONED / NONFINITE。
组装比率补充捕捉 LC 导纳相消：一维矩阵的传统条件数恒为 1，不能独自检测此风险。
这是浮点可靠性诊断，不是严格误差界。LU 秩阈值 1e-15；rcond<1e-12 或后向误差>1e-10 报病态。
局部优化可使用后向误差≤1e-10 且 rcond≥1e-15 的病态点，但必须保留诊断。
Strict Exact 不把病态求值计作可信完整评价。任何数值失败均阻止全空间最优证书。
精确无损反谐振可以产生无穷端口阻抗；有限输入格式不能表达它，求值器返回奇异状态。
不通过巨大有限哨兵隐瞒求解失败。

## 3. 完整死区与严格归约

若某子图仅通过一个割点连接活动网络，且不含另一个端口，其所有节点可取
相同电位，没有支路电流，对端口响应无贡献。这是 R0 死区定理。
它适用于悬挂三角形，不能只用度一节点删除代替。

公共 liveEdges 检查端口连通性，删除非端口连通分量和完整割点死区。
Try2 枚举只保留所有输入元件均活动的网络，明确排除隐藏死区中的物理 BOM。
Try3 在参数化之前执行 R0，并迭代下列精确恒等归约：

- 并联 R：倒数和；并联 C：直接和。
- 非端口度二节点的同类串联 R/L：直接和；C：倒数和。
- 串联 L 的 DCR 同时相加；串联 R 可吸收进相邻 L 的 DCR。
- **不一般合并并联 L+DCR**：两支路的时间常数可以不同。

原始边序号映射到聚合群；死区边标记 dropped。保留原输入节点标签和 V。
输出聚合值不能被解释成各物理成员的唯一值。

归约恒等式同时以表达式树（Sum/HarmonicSum 于原始边叶）传播。每个归约群的
等效参数允许域按表达式单调传播：串联取区间端点相加，并联取调和组合；
任何合法物理成员组合的等效值因此总落在传播域内——两个 rMax 串联的等效
2·rMax、两个 cMax 并联的 2·cMax、DCR 串联合计超过单个 dcrMax 都不会被
单器件全局箱排除。连续拟合使用这些有效域，不得按等效边类型重新套用
单器件全局箱。

## 4. 残差、噪声与 AICc

严格测量文件只有 f/Re/Im。缺省

\[
z_{floor}=\max(10^{-15},10^{-9}\,\operatorname{median}|Z_k|),\quad
r_k=\frac{[\Re(\hat Z_k-Z_k),\Im(\hat Z_k-Z_k)]^T}
{\max(|Z_k|,z_{floor})}.
\]

floor 比率可配置。若 C++ Config 提供逐点正定协方差 Σ_k，则 Σ_k=L_kL_k^T，
r_k=L_k^{-1}[Re error, Im error]^T，采用 GLS。
当前文件不凭空推断协方差，不改动测量后端的不确定度算法。

目标 J=Σ||r_k||²。wRMSE 与 maxRel 始终用原始相对权重报告，包括 robust 模式。
可选外层 Huber IRLS 对白化复残差范数降权，阈值为 2.5×中位范数、最多三轮，
要求至少半数点未降权；这是自适应工程规则，并非全局鲁棒性定理。
报告 robustUsed/outlierCount，不把原始异常点从报告中抹掉。

实观测数 n=2M。相对权重且未知共同方差时 k=p+1（p 个自由电参数及一个方差）；
已知协方差时 k=p。

\[
\mathrm{AICc}=\begin{cases}
n\log(\max(J/n,10^{-300}))+2k+2k(k+1)/(n-k-1),&\text{未知共同方差},\\
J+2k+2k(k+1)/(n-k-1),&\text{给定协方差}.
\end{cases}
\]

省略同一数据集候选共有常数。仅 n>k+1 有定义，否则 unavailable/null。
非线性、有界、秩亏、鲁棒或系统误差场景下 AICc 是诊断性近似，不能宣称校准的
模型概率。候选分两层：满足正则条件（有限指标、AICc 有效、优化器收敛、
非 robust、自由参数满秩且无触界）的 primary 候选按 AICc 排名并给出校准
ΔAICc；其余为 diagnostic-only，排在全部 primary 之后按原始 RSS 内部排序，
不参与 ΔAICc。单个 AICc 无效的过参数候选不再把整组拖回 RSS。若一个
primary 都没有（或 robust 运行），所有有限候选按原始 RSS 做 exploratory
回退，报告 selectionCriterion=RSS_DIAGNOSTIC_FALLBACK、
selectionQualified=false，不展示校准 ΔAICc。
Try2 Exact 按共同精确目标排序，Tolerance 按共同 RSS 排序。
不重用 v3 的经验 regime 阈值；系统误差可能仍使较复杂模型胜出，应查看阶数和诊断。

## 5. 共享连续优化

R/L/C 用 x=log10(q)，导数乘 ln(10)q。DCR 用 x=Rd/Zscale，
Zscale=max(1e-9,median|Z|)，采用显式非负箱约束，可精确到达零。
**softplus 对任何有限坐标均严格为正，因此不能单独实现精确零边界**；
v4 不采用审计稿中这一含混建议。

默认物理箱：R=[1e-3,1e7] Ω，L=[1e-10,10] H，C=[1e-13,1e-3] F，
DCR=[0,1e7] Ω。Try2 Tolerance 与标称容差箱求交，交集为空报输入错误。
标称零 DCR 无绝对容差时固定为零，不计为自由参数。
连续拟合的模型域由 prepareForFit 唯一构造：Try3 与内部 Try2.5 走
ExactElectrical 策略（R0 + 精确归约 + 表达式级域传播），Try1 SP 库与
Try2 Tolerance 保持物理 BOM 身份（None 策略，逐元件标称箱）；fit() 不再
按边类型自行发明界。

每轮用解析 J 求解增广 LM：

\[
\min_\Delta\left\|
\begin{bmatrix}J\\\sqrt\lambda D\end{bmatrix}\Delta+
\begin{bmatrix}r\\0\end{bmatrix}\right\|_2,
\quad D_{jj}=\max(10^{-8},\|J_{:j}\|).
\]

使用 Eigen JacobiSVD，不形成法方程求步长。步长最大坐标限制为 2，投影到箱内；
成功降低阻尼，失败增加阻尼。终止区分梯度/代价收敛、停滞、迭代上限、数值失败、预算耗尽。
物理尺度初值由阻抗中位数与频带几何中心形成，辅以固定种子的多初值及零 DCR 初值。
默认 16 次启动、每次 160 轮；种子 1。全部配置及实际启动数可复现。

算法是局部方法，多初值不能证明全局最优。预算在搜索、启动和 LM 步之间协作检查；
单次矩阵分解和最终诊断不强制中断。Strict 缺省无时间/候选上限；Fast 缺省最多
1000 个已评价候选，是显式预算模式，v4 不采用未经验证的 probe/F2 破坏性筛选。

## 6. Try2 与内部 Try2.5

E 个活动连通边的二端图满足 2≤V≤E+1。对每个 V 枚举所有节点槽位的 E 边多重集，
检查活动性，对内部重标号和互易端口交换规范化；再枚举器件赋值，剔除相同器件置换
以及带值图同构重复。支持 E≤8，包含桥式和重边；复杂度随 E 急增。

Strict Exact 对每个剩余候选全频评价，排序后按 observed-band 容差（默认 1e-6）
聚类，返回 Top-K 代表元。初版不做 partial-cost pruning。

非负部分和确实给出总损失下界，但旧伪代码 `partial>best` 只足够保留最佳值；
Top-K 尤其是 Top-K 等价类不能直接使用该阈值，因为前 K 个网络可能同属一类。
这是 v4 相对审计稿的重要修正。

条件性结论：仅当声明空间全部枚举、每个候选数值可靠地完成评价，才能报告该有限空间
目标的全局最小值。浮点误差仍受求解诊断限制，且它不是内部物理图唯一性证明。

Tolerance 对每图调用公共局部优化器，不自动作用于 Exact。
内部 Try2.5 用相同枚举器产生已知类型的图，再复用 Try3 的 prepared 内层：
对每图执行同样的 R0 + 精确归约（含域传播）后共享局部拟合，不可分别辨识的
聚合（如两串联 R）只拟合一个等效参数。候选同时报告原枚举拓扑键与
有效拓扑键，二者不得混淆；行为等价聚类仍可把它们并入同一 observed-band 类。
二者必须分开报告 `enumeration_complete` 和 `continuous_global_certified`，后者为 false。

## 7. Try3 与可辨识性

在归约群上拟合。最终用加权实 Jacobian SVD：J=UΣV^T。
数值秩阈值为 `100*max(rows,cols)*eps*sigma_max`；报告所有奇异值及条件数。
秩为局部数值诊断。正则内点满列秩支持局部可辨识，单点秩亏不自动证明全局连续参数族。

弹性 max_f |q ∂Z/∂q|/|Z|<0.1 标记 weak；这是经验阈值。
区间端点重合的参数为 fixed（独立状态，绝不标记 atBound/weak、不计入
nParams、不参与秩与协方差统计）；标称零 DCR 无绝对容差即此情形。
atBound 仅对自由参数判定，不把先验截断值当成可靠估计。参数以显式
id/edge/quantity 描述符输出，消费方不得按隐式顺序推断归属。
仅在满列秩、自由度为正、非 robust、无边界时输出线性化物理参数标准误差：

\[
C_x=V\,\mathrm{diag}(\sigma_i^{-2})V^T,
\]

相对噪声模式再乘 J/(n-p)，并通过坐标导数转换至物理单位。另给 95% 近似区间：对优化坐标取 x±1.95996 SE，
正参数通过 10^x 映射，DCR 通过线性尺度映射；区间不作静默物理边界截断。
不能在强非线性或模型失配时把这些误差直接理解为校准置信区间。
结果区分局部可辨识、等价/秩亏、数值不稳定、数据不足和局部拟合未确认。

## 8. Try1：有界 SP 发现与有理辅助

叶为 C/L/R，内部为 PAR/SER。同运算扁平化、子树规范字符串排序；
规范多重集递归枚举给定数量/深度内的所有树。规范规则去掉可直接归约的同类 R/C、
串联同类 L 及 SER 同层 R+L，保留一般并联 L。
默认 maxN=4、maxDepth=4；exactN 可指定单一规范数量层。
Strict-SP 不从有限频带斜率推断无限频率极限来删除合法候选。

辅助路径在 s/ω0、Z/Z0 的尺度上做实系数有理最小二乘，使用实极点或共轭极点对
的实基，解分母校正后用特征值重定位极点，最后重拟合留数。
稳定极点不等于无源性：仅接纳能显式综合为正值器件的 Foster 类实现。
支持常数 R、sL、1/(sC)、实负极点 RC 节，以及分子满足 K s 的共轭极点 RLC 节。
不满足综合条件则丢弃辅助候选，**不丢弃 SP 假设**。

例如 K/(s+a) = R∥C，其中 R=K/a、C=1/K（归一频率需恢复尺度）；
K s/(s²+a s+b) 是并联 RLC，R=K/a、L=K/b、C=1/K。
只有正值物理构造加上公共前向与有理响应一致性核对通过才进入参数精调与排序。
这覆盖一个明确的 Foster 子族，不能称作任意正实综合或完整 Cauer/Brune 实现。
辅助候选归约后同样满足器件数限制；maxDepth 约束枚举 SP 树，Foster 辅助族另行声明。

Try1 输出 family=`NORMALIZED_SP_PLUS_FOSTER`，continuous_global_certified=false。
SP 离散枚举完整不意味着连续内层找到全局最优，也不意味着真实桥式结构在假设空间中。

## 9. 测量流边界

v4 从已给定复阻抗开始，不重写电压/电流正弦拟合和校准。
理论上的正弦回归为 y=a sin(ωt)+b cos(ωt)+c，完整通道协方差应由线性回归得到，
再通过复比值 V/I 的 Jacobian 传播。当前测量后端是否达到完整协方差和真实 OSL 校准
要求需独立验收；本次算法重写不作这种承诺。
频率自适应设计（如最大化最小奇异值）仍是未来仪器工作，不修改上传契约。

## 10. 实现与验证证据

C++ 公共模块：io、graph、nodal、fit、search、rational、report。
Eigen 3.4.0 头文件及许可随源码固定，原生构建离线可用；无 Python 算法或测试依赖。
网站通过 Worker/WASM 调用共享核心，C ABI/JSON 适配见 DESIGN.md。

验证分层：格式/矩阵守恒；闭式电路与独立 long-double 解；解析导数高精度差分；
E≤5 枚举完整签名；归约、零 DCR、秩亏及数值异常；无噪声恢复；四组实测；
固定种子的高 Q/稀疏网格/噪声/离群点/系统误差基准。
实测参考值不是精确元件标称值，Exact 与 Tolerance 应分别评价。
完整运行命令及实际结果见 README.md / VALIDATION.md。

---

# English normative counterpart

## Model and contracts

v4 replaces the audited legacy implementations with one C++17/Eigen core.
The network is a passive LTI lumped reciprocal terminal multigraph, with terminals 0 and 1,
ideal R/C and physical inductors z=Rd+sL. R/L/C are positive and Rd is nonnegative,
including an exact zero boundary. Mutual inductance, active and distributed devices are excluded.
Text inputs and triangular Edge outputs retain their shapes. `exactN` counts normalized
irreducible equivalent-model devices, not unobservable physical packages.

## Shared mathematics

Ground terminal 0, stamp Y=Σ y_e a_e a_e^T, solve Yv=b, and compute Z=b^Tv.
The exact analytic sensitivity is dZ/dq=−(dy_e/dq)(v_u−v_v)^2, with an ordinary transpose,
not a Hermitian transpose. FullPivLU reports singular/open/nonfinite/ill-conditioned states,
an effective reciprocal condition (including admittance-stamp cancellation) and scaled backward error.
This catches near-cancelling scalar LC admittances whose ordinary matrix condition is one. No large finite sentinel represents failure.

Full articulation dead-zone removal precedes fitting. Series/parallel R/C identities,
series L+DCR addition and series R absorption are exact. General parallel physical inductors
are retained. Reduced groups preserve original edge membership and node labels.
Reduction identities also propagate admissible domains monotonically through expression
trees (Sum/HarmonicSum over original-edge leaves), so any legal member combination —
two rMax in series, two cMax in parallel, DCR sums beyond a single dcrMax — stays
inside the optimizer domain instead of being re-clamped to single-device bounds.

Relative residuals use max(|Z|,max(1e−15,1e−9 median|Z|)); supplied SPD 2×2 covariances
instead whiten residuals by Cholesky factors. Robust Huber IRLS is optional, uses an adaptive
2.5×median norm cutoff and an inlier-majority guard, and always retains original-weight metrics.

Positive parameters use log10 coordinates. DCR uses a scaled nonnegative linear box;
finite softplus coordinates alone cannot represent exact zero. The common projected LM
solves augmented least squares by JacobiSVD, with deterministic multistart and explicit
termination. Defaults are 16 starts, 160 iterations and seed 1. It is a local optimizer.

AICc uses n=2M, k=p+1 for unknown common variance or k=p for supplied covariance;
its denominator is n−k−1 and it is unavailable when n≤k+1. It is only an approximate
model-selection diagnostic for nonlinear, constrained or misspecified problems.
Candidates are tiered: primary candidates (finite metrics, available AICc, converged
optimizer, non-robust, full free-parameter rank, no free parameter at a bound) rank
by AICc with calibrated deltas, while diagnostic-only candidates follow them by raw
RSS and never carry deltas — a single AICc-invalid over-parameter model can no longer
demote the whole set. With no primary candidate, or in robust runs, the finite set
falls back to exploratory RSS ordering reported as unqualified. The web may feed
supplied covariance through an explicit optional path; scan-derived polar uncertainty
converted to Cartesian covariance is an approximate propagation, never a full
waveform least-squares covariance.

## Engine guarantees

Try2 Exact enumerates all declared active connected E-edge terminal multigraphs for
2≤V≤E+1, E≤8; canonicalization quotients internal permutations, terminal exchange and
identical components. Full-frequency evaluation precedes observed-band Top-K clustering.
The legacy partial-cost threshold against the single best result is not a valid Top-K-class
algorithm and is not used. Only complete, numerically reliable exact evaluation supports
a conditional finite-space minimum claim. Hidden dead-zone BOMs are outside this space.

Try2 Tolerance requires explicit fractional bounds, with optional absolute DCR tolerance;
nominal zero DCR otherwise remains fixed. Try2.5 composes typed graph enumeration with the
same Try3 prepared inner fit — exact reduction with domain propagation precedes the shared
local fit, non-identifiable aggregates collapse to one equivalent parameter, and candidates
report both original and effective topology keys. Neither certifies a continuous global
optimum.

Try3 fits reduced groups over propagated domains and reports numerical Jacobian rank,
singular values, condition, elasticity, boundaries, multistart agreement and conditional
linearized standard errors and transformed approximate 95% intervals. Fixed parameters
(collapsed interval, e.g. nominal zero DCR without absolute tolerance) are a separate
state, never marked at-bound and excluded from parameter counts; diagnostics carry
explicit id/edge/quantity parameter descriptors instead of positional guessing.
Full rank at a regular interior point supports local identifiability; one singular Jacobian
does not prove global non-identifiability. Weakness and rank deficiency are distinct.

Try1 exhausts its declared normalized SP grammar/count/depth with no unproved destructive
asymptotic pruning in Strict mode. A rewritten pole-relocation rational fit contributes only
positive-element Foster realizations independently checked by the shared forward model.
It is a limited synthesis family, not a general positive-real synthesis algorithm.
SP depth limits apply to the enumerated trees; the bounded Foster auxiliary family is
separately declared. Continuous optimization remains local and arbitrary bridge recovery
is not claimed. Lower residual alone does not establish the true physical topology.

Strict defaults to unlimited search; Fast defaults to a 1000-evaluated-candidate budget,
without legacy probe/F2 rules. User time budgets and cancellation are checked cooperatively
between hypotheses, starts and LM steps; interrupted results are explicitly incomplete.
Observed-band clustering is numerical, representative-based and is not symbolic equivalence.

## Measurement and validation boundary

Waveform DSP, covariance propagation and OSL calibration remain separate measurement work.
The v4 rewrite does not certify or change them. Native reports expose diagnostics separately
from the browser ABI. The WASM adapter runs the same core locally in a Worker.
Independent forward/derivative and enumeration oracles, synthetic recovery, real-data gates,
and reproducible noisy/systematic-error benchmarks supply implementation evidence;
none substitutes for mathematical assumptions or proves universal inverse recovery.

## References / 参考资料

- Gustavsen & Semlyen, rational approximation / vector fitting, 1999: https://doi.org/10.1109/61.772353
- Marquardt, nonlinear least squares, 1963: https://doi.org/10.1137/0111030
- Huber, robust estimation, 1964: https://doi.org/10.1214/aoms/1177703732
- Akaike, model identification, 1974: https://doi.org/10.1109/TAC.1974.1100705
- Hurvich & Tsai, small-sample model selection, 1989: https://doi.org/10.1093/biomet/76.2.297
- Eigen least squares: https://eigen.tuxfamily.org/dox/group__LeastSquares.html
