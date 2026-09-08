# LCR v4 完整改进与网站重新接入实施计划

> 审计基线：`invincible-summer/LCR-Analyzer-WebSite` `dev@02b2e04cc8b4e409652104a60e5b90e623e79fae`（2026-09-08，commit message: `Algorithm is ok`）。  
> 本计划基于重新逐文件核对的 `AlgorithmLcr/include/lcr/lcr.hpp`、`src/{graph,nodal,fit,search,rational,report,io,main}.cpp`、`tests/{test,bench}.cpp`、`wasm/{bindings,build}.cpp/.sh`，以及网站 `fitTypes.ts / fitValidation.ts / fitAdapter.ts / lcrWasm.ts / fitWorker.ts / FitView.vue / csv.ts / browser-smoke.mjs`、测量后端 uncertainty 字段与当前 v4 理论/验证文档。  
> 当前环境无法从 GitHub 直接 clone 并重新编译（DNS 不可达），因此这里的“重新确认”是源码级重新审计；仓库 `VALIDATION.md` 记录的 995 checks、CTest、sanitizer、前后端测试属于仓库已有验证记录，最终合并前必须按本文验收矩阵重新独立执行。

## 0. 最终目标与不可妥协条件

本轮不是继续扩展更多启发式搜索，而是把 v4 收敛成一个**数学语义一致、C++/CLI/WASM/网页接口一致、可验证且不夸大结论**的稳定版本。完成后必须同时满足：

1. `nodal.cpp` 仍是唯一的端口前向求值和解析 Jacobian 真源，Try1、Try2-Tolerance、Try2.5、Try3 不再维护不同的连续求解数学。
2. 所有精确图归约都必须同时传播**等效电参数表达式、等效参数允许域、原始边来源**，任何合法原物理参数组合的等效参数都不能因为单器件全局 bounds 被排除。
3. Try3 与内部 Try2.5 必须经过同一 `prepare -> reduce -> effective domains -> common fit` 路径；Try2.5 不得再直接对未归约图拟合。
4. 固定参数、自由参数、触边界参数必须严格区分；固定零 DCR 不能被误报为 `at_bound`。
5. 模型选择必须将“可用于 AICc 的候选”与“仅供诊断展示的候选”分离；单个 AICc 无效的过参数模型不得迫使所有正常模型退化到纯 RSS 排名。
6. 所有参数诊断必须有显式 parameter ID / edge / quantity 映射，网站不得再靠“L 占两个连续索引”的隐式算术推断 `weak/at_bound/SE/CI` 属于谁。
7. CLI、C ABI、WASM、TypeScript job、网页控件必须共享同一配置语义；尤其 `maxDepth`、预算、噪声模型不能在绑定层静默改变。
8. C++ core 已有逐点 2x2 covariance 能力，WASM/网站至少要提供可选透传接口；无 covariance 时继续明确使用 relative fallback，不凭空制造统计精度。
9. 生成的 `lcr.js/lcr.wasm` 必须由当前 C++ 源码重建，并由 native↔WASM parity test 验证；当前缺失的 WASM smoke test 必须补齐。
10. 合并前必须有 GitHub CI；当前 `dev` 没有 workflow/status checks，不能继续只依赖手工 `VALIDATION.md`。

---

## 1. 重新核对后的当前状态与问题清单

### 1.1 可以保留的数学/数值主干

以下实现重新核对后没有发现需要推翻的基础公式：

- `nodal.cpp` 对 R/C/L+DCR 的导纳 stamp 正确；端口 0 接地、端口 1 单位电流注入，`Z=b^T Y^{-1}b`。
- 解析灵敏度使用 `-dy * (vu-vv)^2`，是复对称矩阵的转置形式而不是共轭转置，正确。
- `FullPivLU` + matrix scaling + explicit backward residual + assembly cancellation factor 的方向合理；继续保留 `PORT_OPEN / SINGULAR / ILL_CONDITIONED / NONFINITE`。
- 连续拟合已使用解析 Jacobian + augmented damped least squares + `JacobiSVD`，不再形成 `J^T J` 法方程。
- DCR 用线性非负坐标，精确 `DCR=0` 已可表示。
- AICc 已在 `n <= k+1` 时返回 unavailable，而不是修改分母。
- `liveEdges()` 已包含 articulation dead-zone 删除；pendant triangle 等单割点死区能够被 R0 删除。
- Try2 Exact 的有限多重图枚举逻辑、小规模独立 oracle、数值失败阻止全局证书的总体方向正确。
- Try1 当前 SP grammar 已主动去掉可直接归约的同类串/并联 primitive，并保留一般并联 L+DCR；rational path 只把可正值综合并通过 forward consistency check 的 Foster 子族作为辅助候选，定位合理。

这些不应在本轮重写成另一套算法。

### 1.2 P0：Try3 归约后的参数 bounds 错误，会直接排除真实解

当前 `Reduction` 只有：

```cpp
struct Group {
  std::vector<int> members;
  std::string mode; // single/par/ser
};
```

`reduce()` 会把等效元件数值算出来，但 `fit()` 随后仍根据等效边的 `type` 套用单个器件全局 bounds：

```cpp
R -> [c.rMin, c.rMax]
L -> [c.lMin, c.lMax]
C -> [c.cMin, c.cMax]
DCR -> [0, c.dcrMax]
```

这在数学上不闭合。反例：两个均合法的 `R=rMin` 并联，等效 `Req=rMin/2`；两个 `C=cMax` 并联，等效 `Ceq=2*cMax`；两个 `L=lMax` 串联，等效 `Leq=2*lMax`；多个 DCR/R 被串联合并后也可超过单个 `dcrMax`。

因此当前 Try3 存在“真实拓扑正确、数据无噪声、但真实等效参数不在 optimizer domain 中”的确定性失败模式。这是 release blocker。

同时，`Group.members + mode` 无法表达嵌套关系，例如 `(R1 || R2) + R3`；最后一次 merge 覆盖 `mode` 后已经丢失内部等效表达式，所以不能靠简单扩大一个常量倍数来修复。

### 1.3 P0：Try2.5 没有真正复用 Try3

当前：

```cpp
try3:  reduce(g) -> fit(reduced.graph)
try25: enumerate(g) -> fit(g)
```

这与项目理论中“Try2.5 = Try2 topology enumeration + Try3 inner fit”不一致。典型两串联 R 会作为两个自由参数进入 Try2.5，Jacobian 两列完全共线；本应只拟合 `R1+R2` 一个可辨识 aggregate。

修复后 Try3、Try2.5 必须调用同一个 `prepareForFit()`，不能再在搜索函数内各自拼装逻辑。

### 1.4 P0/P1：Try1 selector 的 all-or-none AICc fallback 不安全

当前 `finish()`：

```cpp
bool aicc = selection && std::all_of(candidates, has_aicc);
// 只要有一个 AICc=null，所有候选全部按 RSS 排序
```

这会让一个数据不足/过参数候选把整个集合从有复杂度惩罚的选择准则拉回 RSS，从而给更复杂模型系统性优势。当前 theory 文档也描述了这条规则，因此修代码时必须同步修理论文本，避免“实现错、文档对”或“实现对、文档旧”。

新的规则必须明确区分：

- **primary selectable**：满足当前选择准则基本正则条件的候选；
- **diagnostic-only**：可展示，但不能参与 ΔAICc / “rank-1 best model” 的候选；
- 若没有 primary candidate，则可以给 exploratory fallback 排名，但必须标 `selection_qualified=false`，不能显示校准的 ΔAICc。

建议 Primary-AICc eligibility：

```text
finite metrics
AND aicc available
AND optimizer converged
AND !robustUsed
AND rank == nParams
AND no free parameter at bound
```

边界/秩亏/robust 下 AICc 可以保留为“诊断数值”，但不得作为正式 ΔAICc 排名依据。若后续实现真正固定尺度的 Huber likelihood，再另定义 robust information criterion；本轮不要伪装。

Try2 Exact 继续按共同 exact RSS/GLS objective；Try2 Tolerance 由于同一物理 BOM 的自由参数维数原则上相同，可继续按共同 raw objective 排名，同时单独显示 rank/boundary 诊断。

### 1.5 P0/P1：固定参数被错误标记为 `at_bound`

`fit.cpp` 计算 `nParams` 时正确排除了 `lo==hi` 的 fixed 参数，但后面 `diag.atBound` 遍历的是**全部** `model.params`：

```cpp
if (best.x[j] - p.lo < ... || p.hi - best.x[j] < ...)
    atBound.push_back(j);
```

因此 fixed parameter 永远“触边界”。最常见实例是 Try2 Tolerance 中 nominal `DCR=0` 且 `dcrAbsoluteTolerance=0`：DCR 被正确固定为 0，但随后会污染 `atBound`，最终触发 `LOCAL_FIT_UNCONFIRMED`。

修复条件：

- `fixed` 是独立状态；
- `atBound` 只对 `free=true` 参数判断；
- `weak` 默认也只对 free 参数判定，fixed 参数直接显示 `fixed`；
- `nParams/rank/condition/covariance` 全部以 free 参数集合为准。

### 1.6 P1：参数 SE/CI/weak/at_bound 缺乏显式映射

当前 `Diagnostics.standardErrors` 和 `confidenceIntervals95` 只写 free params；`weak/atBound` 使用原 `model.params` 索引；JSON 没有 parameter descriptor。前端 `fitAdapter.ts` 因此用：

```ts
L => [parameter++, parameter++]
R/C => [parameter++]
```

猜测参数编号。

这在存在 fixed parameter、未来 aggregate domain、接口扩展时非常脆弱。必须改为原生 C++ 输出显式：

```text
parameter id
edge index
quantity = value | dcr
kind = R | L | C | DCR
value
lower / upper
free / fixed
weak / at_bound
standard_error / ci95
```

网站只消费这些 ID，不再重建 C++ 参数顺序。

### 1.7 P1：浏览器 Try1 与 native CLI 的 maxDepth 语义漂移

native default：`maxN=4, maxDepth=4`。  
WASM binding 当前在 `lcr_try1()` 内强制：

```cpp
c.maxDepth = c.maxN;
```

因此 `exactN=6` 时浏览器搜索 depth 6，而 CLI 若未显式 `--max-depth` 仍是 depth 4；同一输入、同一“v4 Try1”实际不是同一 hypothesis family。

修复：WASM `lcr_try1` 显式接收 `maxDepth`，默认 4；绑定层禁止静默根据 maxN 修改 maxDepth。网站 Advanced options 暴露 `maxN / maxDepth` 或至少固定传入 4。

### 1.8 P1：core covariance 能力没有接到 WASM/网站

C++ `Config.covariance` 已支持每点 `Eigen::Matrix2d` SPD whitening；但：

- `lcr_configure()` 没有 covariance setter；
- `FitJob/ZPoint` 没 covariance；
- Worker 不分配 covariance typed arrays；
- 历史扫描虽然已有 `z_sigma`、`z_phase_sigma_deg`，拟合页面只导入 `f/re/im`。

因此网页当前永远走 relative fallback。需要增加可选 covariance C ABI；默认仍保持 relative，不可静默自动改变旧 CSV 行为。

### 1.9 P1：WASM 测试脚本契约已断

`frontend/package.json` 定义：

```json
"test:wasm": "node ../AlgorithmLcr/wasm/smoke.mjs"
```

但当前 `AlgorithmLcr/wasm/smoke.mjs` 不存在。必须补回真实 Node/WASM smoke，并放入 CI，否则“WASM 已接入”没有稳定回归门。

### 1.10 P1：网站/文档/验证记录存在状态漂移

当前 `DESIGN.md` 与实际代码都说明 Worker/WASM 已运行；`FitView.vue` 的 `FIT_AVAILABLE=true`，`fitWorker.ts` 已直接调用 `_lcr_try1/2/3`。但 `VALIDATION.md` 末尾仍保留“网页 v4 WASM 接入不属于本次已完成验收、执行按钮禁用”的旧描述。

修复后 README / DESIGN / LCRTheory / INPUT / OUTPUT / VALIDATION 必须在同一 commit 同步更新，且 `VALIDATION.md` 只能写本次实际执行过的命令与结果。

### 1.11 P1：网站若干配置与提示不一致

- Try2 UI 单行 count 提示允许 1..64，但 worker/core 实际总器件硬上限 8；应统一为 1..8，并限制最多 8 rows。
- UI 没有候选 budget 输入，虽然 job/type/core 都支持；Fast 的 1000 是隐含行为。
- `robust` 在 Try2 Exact 时页面仍可勾选，最终到 worker 后才被拒绝；应在 UI 直接禁用/提示。
- Try3 也显示 Strict/Fast，但当前 Try3 没离散枚举，Fast candidate budget 对它没有实际意义；页面应改成“本地优化”语义，不显示“枚举完成”。
- Try1 stats 的 `n_library/n_pruned_kept`、Try2 的 `n_funnel_kept` 是旧命名；v4 已无旧 destructive pruning/funnel，应改成 `generated/structures/evaluated/classes/failures` 的真实字段。

### 1.12 P2：Try2 E=7/8 性能仍是现实边界，不是 correctness bug

当前槽位 multiplicity 原始组合数（规范化前）为：

```text
E=5:      13,904
E=6:     274,486
E=7:   6,396,195
E=8: 171,997,851
```

因此 `E<=8` 是 API 支持上限，不等于 Strict E=8 是交互式实时功能。正确性问题修完后，再考虑 canonical augmentation / automorphism orbit / nauty-Traces 一类图同构工具；不要在 P0 修复中混入新的破坏性 heuristic。

---

## 2. 目标 C++ 核心接口

### 2.1 新增参数域与归约表达式

在 `lcr.hpp` 中新增：

```cpp
enum class ParamQuantity { Value, Dcr };

struct Interval {
  double lo = 0;
  double hi = 0;
};

struct EdgeDomain {
  Interval value;
  std::optional<Interval> dcr; // only L
};

enum class ExprOp {
  PrimitiveValue,
  PrimitiveDcr,
  Sum,
  HarmonicSum
};

struct ReductionExpr {
  ExprOp op = ExprOp::PrimitiveValue;
  int sourceEdge = -1;                 // primitive leaf only
  std::vector<ReductionExpr> children; // Sum/HarmonicSum
};

struct Group {
  std::vector<int> members;
  ReductionExpr valueExpr;
  std::optional<ReductionExpr> dcrExpr;
};

struct Reduction {
  Graph graph;
  std::vector<Group> groups;       // aligned 1:1 with graph.edges
  std::vector<EdgeDomain> domains; // aligned 1:1 with graph.edges
  std::vector<int> dropped;
};
```

只需要 `Sum` 与 `HarmonicSum` 就能表达当前全部安全归约：

```text
series R       -> Sum(R values)
parallel R     -> HarmonicSum(R values)
parallel C     -> Sum(C values)
series C       -> HarmonicSum(C values)
series L       -> Sum(L values), Sum(L DCRs)
series R + L   -> L value expr unchanged/summed,
                  DCR expr = Sum(L DCR expr, R value expr)
```

禁止把一般 parallel L+DCR 表成一个 L；保持现实现。

### 2.2 参数域传播必须是表达式级单调传播

实现：

```cpp
Interval bounds(const ReductionExpr&, const std::vector<EdgeDomain>& source);
double evaluate(const ReductionExpr&, const Graph& source);
```

规则：

```cpp
Sum:
  lo = sum(child.lo)
  hi = sum(child.hi)

HarmonicSum (all child.lo > 0):
  lo = 1 / sum(1 / child.lo)
  hi = 1 / sum(1 / child.hi)
```

leaf domain 构造：

```text
WidePhysical:
  R [rMin,rMax]
  C [cMin,cMax]
  L [lMin,lMax]
  DCR [0,dcrMax]

NominalTolerance:
  value = global physical bounds ∩ nominal*(1±tol)
  L DCR = [max(0, nominal*(1-tol)-dcrAbsTol),
           min(dcrMax, nominal*(1+tol)+dcrAbsTol)]
```

这样 aggregate bounds 可以自然小于/大于单器件 bounds，而不需要“乘一个最大器件数”的脆弱补丁。

### 2.3 引入统一 PreparedNetwork

新增：

```cpp
enum class ReductionPolicy {
  None,
  ExactElectrical
};

struct PreparedNetwork {
  Graph original;
  Graph effective;
  std::vector<EdgeDomain> domains; // effective edges
  Reduction reduction;
};

PreparedNetwork prepareForFit(
    const Graph&,
    const Config&,
    ReductionPolicy policy);
```

行为：

- `policy=None`：保留图，生成 identity groups/domains；用于 Try1 normalized SP、Try2 Tolerance（第一轮先保持物理 BOM 身份）。
- `policy=ExactElectrical`：先 R0，再串并联归约、传播 expression/domain；用于 Try3、Try2.5、rational auxiliary normalization。
- `prepareForFit()` 是唯一能构造 continuous-fit model domain 的入口，`fit()` 不再自行根据 edge type 重新发明 bounds。

改 `fit`：

```cpp
Candidate fit(const PreparedNetwork&, const Data&, const Config&,
              const std::vector<Graph>& initial = {});
```

保留一个内部 convenience overload 也可以，但 search 层只能调用 PreparedNetwork 版本，防止未来再次绕过归约/domain。

### 2.4 参数描述符取代平行索引数组

新增：

```cpp
struct ParameterDiagnostic {
  int id = -1;              // stable optimizer parameter id
  int edge = -1;            // effective graph edge index
  ParamQuantity quantity;   // Value / Dcr
  char kind = 'R';
  double value = 0;
  double lower = 0;
  double upper = 0;
  bool free = true;
  bool fixed = false;
  bool weak = false;
  bool atBound = false;
  std::optional<double> standardError;
  std::optional<std::array<double,2>> ci95;
};
```

`Diagnostics` 新增：

```cpp
std::vector<ParameterDiagnostic> parameters;
```

旧 `weak/atBound/standardErrors/confidenceIntervals95` 可在一个过渡版本继续从新结构生成，以免一次性破坏 CLI/旧适配器；最终网站必须只消费 `parameters`。

判定顺序：

```text
fixed -> free=false, fixed=true, atBound=false, weak=false
free  -> 才允许 atBound/weak/SE/CI
```

线性 DCR CI 保持“未静默裁剪”的理论语义；JSON 同时输出 domain，网站可显示“线性化区间越过物理边界”的 warning，而不是偷偷 clamp。

### 2.5 将数值阈值集中为内部 policy

当前 `1e-15 / 1e-12 / 1e-10 / 1e4 / 0.1` 分散在 `nodal.cpp/fit.cpp`。不建议全部暴露给普通用户，但应集中：

```cpp
struct NumericsPolicy {
  double luRankThreshold = 1e-15;
  double rcondWarn = 1e-12;
  double rcondReject = 1e-15;
  double backwardReject = 1e-10;
  double identConditionWarn = 1e4;
  double weakElasticity = 0.1;
};
```

可以先放匿名 namespace `constexpr`，至少保证同一处定义、测试可定位；不要求本轮把它加入公开 Config。

---

## 3. `graph.cpp` 详细修改

### 3.1 保留 `liveEdges()`，新增归约 property tests

`liveEdges()` 当前 articulation dead-zone 实现保留。新增 test：随机生成带 pendant component 的图，删除后在多频点验证：

```text
forward(original).z == forward(reduced).z
```

并验证 dropped edges 的 Jacobian 全零。

### 3.2 重写 `reduce()` 的 group merge 数据结构，不改电学公式

当前 merge 只拼 `members`、覆盖 mode；改成同时合并 expression：

- parallel R：`valueExpr=HarmonicSum(old_i.valueExpr, old_j.valueExpr)`
- parallel C：`valueExpr=Sum(...)`
- series R：`Sum`
- series C：`HarmonicSum`
- series L：`value=Sum(L value exprs)`；`dcr=Sum(dcr exprs)`
- R-L series absorption：`value=L.valueExpr`；`dcr=Sum(L.dcrExpr, R.valueExpr)`

merge 后立即通过 expression 计算新 effective value 和 new domain；group `members` 仍排序保存原边号。

### 3.3 不压缩节点编号

Try3 当前保留原始 V 和 sparse node labels，网站/OUTPUT_FORMAT 已依赖这一点。不要为了归约清理空节点而改变 contract。

---

## 4. `fit.cpp` 详细修改

### 4.1 Model 直接消费 PreparedNetwork domains

当前：

```cpp
lo/hi <- c.rMin/rMax/... + tolerance
```

改为：

```cpp
lo/hi <- prepared.domains[edge]
```

其中 positive value 转 log10 domain，DCR 仍用 `x=Rd/zscale`。

`guess` 仍可用 zscale/omega 生成，但 encode/project 只投影到 effective domain。这样 aggregate 参数可以超出单器件 bounds。

### 4.2 修复 fixed 参数

- 构造 `Param` 时增加 `free = hi > lo`；
- optimizer 中 fixed column 可以继续 zero，但所有 convergence/diagnostics 都只对 free columns做统计；
- fixed 不计 `nParams`；
- fixed 不进入 atBound/weak；
- 输出 ParameterDiagnostic 标 `fixed=true`。

必须增加 regression：Try2 tolerance，L nominal DCR=0，tol>0、dcrAbsTol=0；最终 DCR=0、`free=false`、`atBound=false`，不能仅因为固定 DCR 得到 `LOCAL_FIT_UNCONFIRMED`。

### 4.3 SE/CI 显式映射

SVD covariance 仍只对 `J_free`。对每个 free column `j`：

```cpp
parameterDiagnostics[paramId].standardError = ...;
parameterDiagnostics[paramId].ci95 = ...;
```

不再 `push_back` 一个失去 ID 的数组。

### 4.4 分离诊断维度

建议保留 `verdict` 作为兼容摘要，但新增三个独立字段：

```text
optimizer_status: converged_gradient / converged_cost / stalled / ...
numerical_status: OK / WARN / FAIL
identifiability_status: FULL_RANK / RANK_DEFICIENT / DATA_INSUFFICIENT
```

`verdict` 由这三者派生，不再让前端把一个字符串当全部语义。

### 4.5 robust 保持为工程层，不与 AICc 混淆

- 继续报告 raw `wrmse/maxRel/rss`；
- 新增 `fitObjective`（实际最后一轮优化 objective）用于 debug；
- `robustUsed=true` 时 selector 默认不把 AICc 作为 primary criterion；
- 本轮不重新命名为“严格 Huber MLE”，文档保持“custom IRLS / adaptive robust rule”。

---

## 5. `search.cpp` 详细修改

### 5.1 Try3

改成：

```cpp
auto p = prepareForFit(g, c, ReductionPolicy::ExactElectrical);
auto a = fit(p, d, run.fitConfig());
a.reduction = p.reduction;
```

不要在 `try3()` 手工 `reduce()` 后又让 `fit()` 自己重新构造 bounds。

### 5.2 Try2.5

改成与 Try3 同路径：

```cpp
enumerate(types, [&](Graph g) {
    auto p = prepareForFit(g, c, ReductionPolicy::ExactElectrical);
    auto a = fit(p, d, run.fitConfig());
    ...
});
```

`a.topology` 仍代表**原枚举拓扑类型图**，但 candidate 增加：

```text
original_topology_key
effective_topology_key/effective_devices
```

这样不会把两个不同物理 topology 因同一 reduced graph 在报告阶段完全混为一谈；行为等价聚类仍可把它们放同一 observed-band class。

### 5.3 Try1

SP library 继续直接生成 normalized irreducible graph；用 `prepareForFit(..., None)`。  
Foster auxiliary path 用 `ExactElectrical`，替代当前先 `reduce()` 再手动 fit 的分支。

### 5.4 Try2 Exact / Tolerance

- Exact：不动完整枚举与 fixed-value full-band evaluation；继续只有 `enumerationComplete && no numericalFailures` 才 `continuousGlobalCertified=true`。
- Tolerance：第一版保留物理 BOM identity，用 `prepareForFit(..., None)`，以免在本轮改变“用户输入每个已知元件的 refined value”语义；若 rank deficient，必须通过 parameter diagnostics 明确显示不可分别辨识。
- 后续可评估把 Tolerance 也转为 aggregate fit，但那需要重新定义“每个已知元件如何输出”，不要混入本轮 blocker 修复。

### 5.5 新 selector

新增：

```cpp
struct SelectionInfo {
  bool eligible = false;
  std::string criterion = "NONE";
  std::optional<double> score;
  std::optional<double> delta;
  std::vector<std::string> reasons;
};
```

`Candidate` 增加 `selection`；`SearchResult` 增加：

```cpp
std::string selectionCriterion;
bool selectionQualified = false;
```

Try1 / Try2.5 选择流程：

1. 先对每个 candidate 计算 eligibility；
2. 如果至少有一个 regular AICc candidate：primary candidates 按 AICc；diagnostic-only candidates 排在 primary 后，内部按 raw RSS；`selectionCriterion="AICc"`，`selectionQualified=true`；
3. 若一个 primary 都没有：所有 finite candidates 用 raw RSS 做 exploratory fallback，`selectionCriterion="RSS_DIAGNOSTIC_FALLBACK"`，`selectionQualified=false`，不计算 ΔAICc；
4. robust run 默认进入 diagnostic fallback，除非未来实现可比较的 robust likelihood；
5. equivalence clustering 在排序后执行，但 representative 必须优先选择 primary candidate；class `members` 保留。

关键回归：加入一个 AICc valid 的简单真模型 + 一个 AICc invalid 的过参数候选，确认后者不会让前者失去 AICc primary ranking。

### 5.6 observed-band equivalence

P0 先保留当前 relative curve tolerance，但输出：

```text
equivalence_metric = relative_curve
threshold = c.equivalenceTolerance
```

P1 若 supplied covariance：增加可选 whitened separation：

\[
D^2=\sum_k \Delta z_k^T\Sigma_k^{-1}\Delta z_k
\]

只把它称为 measurement distinguishability，不称符号电学等价。阈值必须配置/报告，不硬编码成“统计定理”。

---

## 6. Native JSON / `report.cpp` 新契约

不需要破坏 `schema="lcr.native.v4"`，建议增加：

```json
{
  "schema": "lcr.native.v4",
  "schema_revision": 2,
  "engine_version": "4.1.0",
  "noise_model": "relative_unknown_scale | supplied_covariance",
  "selection": {
    "criterion": "AICc | RSS_EXACT | RSS_COMMON | RSS_DIAGNOSTIC_FALLBACK",
    "qualified": true
  }
}
```

每 candidate 增加：

```json
"selection": {
  "eligible": true,
  "criterion": "AICc",
  "score": 12.3,
  "delta": 0.0,
  "reasons": []
},
"diagnostics": {
  "optimizer": "converged_cost",
  "numerical_status": "OK",
  "identifiability_status": "FULL_RANK",
  "rank": 3,
  "condition": 123.4,
  "parameters": [
    {
      "id": 0,
      "edge": 0,
      "quantity": "value",
      "kind": "L",
      "value": 0.001,
      "lower": 1e-10,
      "upper": 20.0,
      "free": true,
      "fixed": false,
      "weak": false,
      "at_bound": false,
      "standard_error": 1.2e-6,
      "ci95": [0.000997,0.001003]
    }
  ]
}
```

Try3/2.5 group：

```json
"groups": [
  {
    "gid": 0,
    "u": 0,
    "v": 1,
    "kind": "L",
    "members": [0,1,2],
    "value": 0.003,
    "dcr": 11.0,
    "value_bounds": [2e-10,20.0],
    "dcr_bounds": [0.001,20000000.0],
    "parameter_ids": [0,1],
    "value_expr": {"op":"sum","children":[...]},
    "dcr_expr": {"op":"sum","children":[...]}
  }
]
```

expression JSON 主要用于追踪/测试；网站普通视图可只显示生成的摘要 `L1+L2`、`DCR1+DCR2+R3`。

保持：

- 非有限数 -> JSON `null`；
- `theory` 只在输入频点；
- adjacency 仍为统一 upper-triangle edge list；
- `enumeration_complete` 与 `continuous_global_certified` 含义不变。

---

## 7. WASM C ABI 改造

### 7.1 不采用 JSON input parser，继续 typed arrays

当前 typed-array C ABI 简洁且高效，不需要为配置扩展引入新的 C++ JSON 依赖。保留 `guard()`，C++ exception 不跨 ABI。

### 7.2 明确配置 setter

保留现有 `lcr_configure()` 作为 reset + 常用配置，新增：

```cpp
void lcr_configure_search(
    int fast,
    int budget,
    double seconds,
    int starts,
    int iterations,
    unsigned seed,
    double equivalenceTolerance);

void lcr_configure_bounds(
    double rMin, double rMax,
    double lMin, double lMax,
    double cMin, double cMax,
    double dcrMax,
    double relativeFloor);

void lcr_set_covariance(
    const double* rr,
    const double* ri,
    const double* ii,
    int n);
```

实现原则：

- 每个 Worker 新 module；第一步 reset `config=Config{}`；
- `lcr_set_covariance(nullptr,...)` 不使用；网站只有 all-or-none covariance；
- setter 后最终仍由 `validate(data, config)` 做一次 C++ authoritative validation；
- `lcr_free` 所有响应路径必须可用。

如果希望最小改动，也可以保留当前 `lcr_configure(fast,budget,seconds,tol,dcr,robust)` 并只新增 `lcr_set_covariance` + 扩展 `lcr_try1`；但 `starts/iterations/seed/equivalenceTolerance` 最终应能从网站高级设置传入，避免 native/WASM 不可复现。

### 7.3 Try1 ABI 修正

改：

```cpp
lcr_try1(..., int exactN, int maxN, int maxDepth, int topK)
```

禁止内部 `maxDepth=maxN`。

### 7.4 CMake exports / d.ts

`CMakeLists.txt` 的 `EXPORTED_FUNCTIONS` 新增所有 C setter；同步 `frontend/src/wasm/lcr.d.ts`。  
Emscripten 官方要求 native export 名以 `_` 前缀放入 `EXPORTED_FUNCTIONS`，当前工程做法正确，新增函数必须沿同一规则。

### 7.5 重建生成物

C++ 修改后执行：

```sh
cd frontend
pnpm build:wasm
```

必须同时更新：

```text
frontend/src/wasm/lcr.js
frontend/src/wasm/lcr.wasm
frontend/src/wasm/lcr.d.ts
```

`lcr_version()` 更新为例如：

```text
lcr.native.v4 revision 2 / wasm 4.1.0
```

网站加载后可在 dev/test 中断言 version，避免旧 wasm 二进制与新 TS adapter 混用。

---

## 8. 网站 TypeScript / Worker / UI 完整回接

### 8.1 `fitTypes.ts`

扩展 `ZPoint`：

```ts
export interface Cov2 {
  rr: number
  ri: number
  ii: number
  source?: 'csv' | 'scan_polar_approx' | 'instrument'
}

export interface ZPoint {
  f: number
  re: number
  im: number
  cov?: Cov2
}
```

约束：一个 job 内 covariance **要么所有 point 都有，要么所有 point 都没有**；避免混合 whitening 语义。

Try1Job 增加：

```ts
maxDepth?: number // default 4
starts?: number
iterations?: number
seed?: number
budget?: number
```

`FitCandidate` 增加原生 diagnostics / parameters / selection 的 typed structure，不再把诊断只放 Try3 glue。

### 8.2 `fitValidation.ts`

统一前端与 C++ 公共条件：

```text
points: 4..100000, finite, f>0
covariance: all-or-none, finite, symmetric [rr ri;ri ii], SPD
Try1: exactN/maxN 1..12, maxDepth>=1, topK 1..100
Try2: rows 1..8, total components 1..8, each count 1..8
Try3: edges 1..32, node labels 0..15, no self-loop, R/L/C
budget >=0, seconds >=0, starts/iterations >=1
```

页面按钮运行前调用同一个 validation 并显示 inline error；worker 再执行一次，形成 UI + worker 双层防护。

### 8.3 `fitWorker.ts`

执行顺序改为：

```text
validate job
create module
assert lcr_version compatible
allocate f/re/im
reset/configure search + fit options
if all points have cov:
    allocate rr/ri/ii
    _lcr_set_covariance(...)
call try1/2/3
parse native report
adapt
free response
free all input buffers
terminate worker by caller lifecycle
```

所有 allocation 放一个 typed helper，保证异常路径 finally 释放。

### 8.4 `fitAdapter.ts`

删除参数索引推断：

```ts
// 删除：L -> parameter++, parameter++
```

Try3 group 直接使用：

```ts
g.parameter_ids
nativeCandidate.diagnostics.parameters
```

同时把 selection map 到候选：

```ts
selectionEligible
selectionCriterion
selectionDelta
selectionReasons
```

旧字段 `n_funnel_kept/n_pruned_kept` 删除或标 deprecated，新的 UI 直接使用 native stats。

### 8.5 `FitView.vue`

#### 公共设置

- `Search mode` 只对 Try1/Try2 明确显示 Strict/Fast；Try3 显示“多起点局部优化”。
- 增加 candidate budget 数字框（0=模式默认/不限，具体语义显示）。
- Advanced details：`starts / iterations / seed / equivalence tolerance`；默认沿 C++ Config。
- 数据区增加 noise-model badge：`relative fallback` / `supplied covariance` / `scan uncertainty approx`。

#### Try1

- 增加 `maxN` 与 `maxDepth` 两个独立 advanced 输入；exactN 不再隐式改变 maxDepth。
- HelpBubble 改成“SP + Foster declared family”，不要只写“按 AICc 排序”；应根据结果 `selection.criterion` 动态展示。

#### Try2

- 单行 count max=8；rows max=8；total>8 直接禁止。
- Exact 时 robust checkbox disabled，并提示“robust 只用于 Tolerance continuous refit”。
- 大 E（建议 E>=7）显示成本警告，推荐 Fast/预算，但不要偷偷自动切 Fast。

#### Try3

- 运行前对 edge/node labels 做 inline validation。
- group 表显示：aggregate value、effective bounds、expression 摘要、members、parameter status。
- fixed DCR 显示“固定 0”，不要显示“触边界”。
- `Jacobi` 文案统一改为 `Jacobian`。
- 搜索状态文案由“枚举完成”改成“优化完成 / 预算中止”。

#### 候选表

- ΔAICc 仅当 `search.selection.criterion === 'AICc' && qualified` 时显示；否则列显示 `—` 并说明原因。
- diagnostic-only candidate 加 badge，不允许它看起来像校准的第一名。
- 显示 `verdict` / numerical / rank warning，尤其 Try1/2 不再隐藏这些信息。

### 8.6 `csv.ts`

保持 3 列 canonical CSV 兼容；可选支持 6 列：

```text
f,re,im,cov_rr,cov_ri,cov_ii
```

要求整文件统一 3 或 6 列。6 列逐行检查 SPD；导出时若全部点有 covariance，则输出 6 列，否则仍 3 列。

不要把 `z_sigma/phase_sigma` 两列伪装成 Cartesian covariance 列。

### 8.7 历史扫描 uncertainty bridge

后端当前已有：

```text
z_sigma [ohm]
z_phase_sigma_deg
```

网站可以提供**显式可选**“使用扫描不确定度”按钮，把 polar uncertainty 近似变换为 Cartesian covariance：

\[
J=\begin{bmatrix}
\cos\phi & -\rho\sin\phi\\
\sin\phi &  \rho\cos\phi
\end{bmatrix},\quad
\Sigma_{RI}=J\,
\mathrm{diag}(\sigma_\rho^2,\sigma_\phi^2)J^T.
\]

前提：`rho>0`、两个 sigma>0、结果 SPD。任何点不满足就不启用整组 covariance，并提示回退 relative。

必须标 `source=scan_polar_approx`；当前后端 uncertainty 是近似传播，不得在 UI/论文里写成“完整 waveform LS covariance”。完整 V/I coefficient covariance 与复比值传播可以作为后续 measurement-track 独立任务。

---

## 9. WASM / 浏览器回归测试必须补齐

### 9.1 新建 `AlgorithmLcr/wasm/smoke.mjs`

至少覆盖：

1. module load + `_lcr_version()`；
2. Try1 单 R，无噪声，返回 finite candidate；
3. Try2 pure R Exact，`continuous_global_certified=true`；
4. Try3 两串联 R 归约为一 group；
5. Try3 fixed-zero-DCR parameter status 正确；
6. port-open 返回 `{ok:false,code:'port_open'}`；
7. optional covariance setter 的已知 GLS case；
8. response `lcr_free` + input `_free`；
9. schema revision/version assertion。

`pnpm test:wasm` 必须真正通过。

### 9.2 native↔WASM parity

固定 3~5 个小 case，将 native CLI `--json` 与 WASM JSON 比较：

- adjacency/topology 相同；
- candidate count/class count 相同；
- metrics/parameters 数值在明确 floating tolerance 内；
- selection criterion/eligibility 相同；
- reduction expressions/domains 相同。

这是防止“C++ 源改了但 committed wasm 还是旧二进制”的最有效 gate。

### 9.3 `browser-smoke.mjs`

保留现有 Try1/Try2/Try3/port-open/cancel/restart/null-AICc/partial-result 用例，新增：

- maxDepth 与 maxN 独立；
- fixed DCR 显示“固定”而非“触边界”；
- mixed AICc candidate 集不显示错误 ΔAICc fallback；
- historical covariance badge/GLS run；
- Try3 aggregate bounds 可超出单器件 bounds；
- Try2 count UI 不能输入/提交 >8。

---

## 10. C++ 验证矩阵与硬验收条件

### 10.1 前向/导数（保留现有并增强）

- R/C/L+DCR closed form；
- bridge；
- 100+ random graph vs independent long-double oracle；
- 5-point finite-difference Jacobian；
- near/exact antiresonance；
- port open / nonfinite guards。

验收：现有精度门不回退；新增测试不能通过放宽旧 tolerance 来“解决”。

### 10.2 reduction-preserves-Z property tests

每种归约单独 + nested：

```text
parallel R
parallel C
series R
series C
series L + DCR
series R absorbed into L DCR
nested: (R||R)+R
nested: (C+C) series C
R0 pendant triangle
```

对每 case 在至少 20 个 log frequencies：

```text
relative |Z_original - Z_reduced| <= 1e-11（正常尺度 case）
```

并验证 group members/expression 正确。

### 10.3 aggregate domain containment tests（P0 必须）

固定 Config 取边界值：

```text
Rmin || Rmin        -> Req = Rmin/2, domain 必须包含
Rmax + Rmax         -> Req = 2*Rmax, domain 必须包含
Cmax || Cmax        -> Ceq = 2*Cmax, domain 必须包含
Cmin series Cmin    -> Ceq = Cmin/2, domain 必须包含
Lmax + Lmax         -> Leq = 2*Lmax, domain 必须包含
DCRmax + DCRmax     -> DCR aggregate = 2*dcrMax, domain 必须包含
L(DCRmax) + Rmax    -> aggregate DCR > dcrMax, domain 必须包含
nested expression   -> evaluate(source) 始终位于 propagated interval
```

再做随机 property test：随机生成每个 leaf domain 内参数，`evaluate(expr)` 必须总在 `bounds(expr)` 内。

### 10.4 Try3 recovery

- 上述每种 aggregate boundary case 用无噪声 sample 运行 Try3；
- recovery 应达到 `wRMSE < 1e-8`（可按数值尺度调整，但必须远低于当前实测门）；
- recovered aggregate 不被旧 global bound clamp；
- original physical member 不得伪造“各自恢复值”。

### 10.5 fixed parameter diagnostics

- L nominal DCR=0 + tolerance + dcrAbsTol=0：`fixed=true, atBound=false`；
- fixed 参数不计 `nParams`；
- rank dimension 等于 free parameter count；
- SE/CI parameter ID 对齐。

### 10.6 selector tests

至少构造：

1. all AICc valid；
2. valid + invalid AICc mixed；
3. rank-deficient candidate；
4. at-bound candidate；
5. robust run；
6. no primary candidate fallback。

断言：

- invalid candidate 不改变 valid candidate 的 primary AICc 顺序；
- ΔAICc 只对同一 qualified AICc 集定义；
- fallback 明确 `selectionQualified=false`；
- Try2 Exact 仍按 common raw objective。

### 10.7 Try2.5 integration

- 两串联 R：effective nParams=1；
- 串联 R+L：effective main L + aggregate DCR，两参数；
- parallel R/C topology 正确；
- enumeration completeness E<=5 oracle 继续全通过；
- Try2.5 `continuousGlobalCertified=false` 始终保持，因为连续内层仍是 local optimizer。

### 10.8 real4 / random benchmark

重跑：

```sh
lcr_bench real4 examples
lcr_bench random 40 21
lcr_bench case 7 21
```

本轮不要求随机 structural match 人为提高，但要求：

- 行为 pass@1/pass@8 不低于当前记录超过预先允许的统计波动；建议硬门 `pass@8 >= 39/40`，`pass@1 >= 36/40`；
- case7 必须仍明确输出 model/selection diagnostics，不允许“为了过 benchmark”藏掉 outlier/systematics；
- 若 selector 改动改变 rank-1，记录 before/after 和原因。

---

## 11. CI 与仓库发布门

新增 `.github/workflows/ci.yml`，至少四个 job：

### native

```sh
cmake -S AlgorithmLcr -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

### sanitizer

```sh
-DCMAKE_BUILD_TYPE=Debug
-fsanitize=address,undefined -fno-omit-frame-pointer
lcr_tests
```

### frontend + wasm

- 固定 known-good Emscripten 版本（仓库当前记录 6.0.9；版本升级单独 PR）；
- `pnpm install --frozen-lockfile`；
- `pnpm build:wasm`；
- 检查 `git diff --exit-code frontend/src/wasm`，保证生成物已提交；
- `pnpm test:wasm`；
- `pnpm test`；
- `pnpm build`。

### backend

```sh
pytest
```

可再加 browser-smoke job；若 CI Chromium 成本太高，至少 release PR 必须执行。

合并条件：所有 required checks green；以后 `VALIDATION.md` 只能引用 CI run + 明确额外人工 benchmark，不再使用不可追踪的“本地已通过”作为唯一证据。

---

## 12. 文档同步清单

### `AlgorithmLcr/LCRTheory_rendered.md`

修改：

- aggregate parameter domain propagation；
- Try2.5 明确复用 exact reduction；
- selector 删除“任一 AICc invalid -> 全组 RSS”的旧规则，换成 eligibility/tier；
- fixed != at_bound；
- robust AICc 仅诊断，不作为 qualified selection；
- covariance 网站可选透传，但 scan-polar 是 approximate source。

### `OUTPUT_FORMAT.md`

新增：

- schema revision；
- parameter descriptors；
- expression/domains；
- selection eligibility；
- groups 不再只有 `single/par/ser` 的扁平 mode。

### `DESIGN.md`

明确：

```text
page -> worker -> current wasm version -> native report rev2 -> adapter -> UI
```

并列出 current ABI setters 和生成物重建约束。

### `VALIDATION.md`

删除已经失真的“网页 v4 未接入/按钮禁用”；用最终实际命令与真实数字重写。

### frontend docs

`try1.md/try2.md/try3.md/constraints.md/algorithms.md` 同步真实上限、Strict/Fast、selection 和 aggregate semantics。

---

## 13. 建议的实际提交顺序

为了可审查和方便定位回归，不要一个巨型 commit 完成全部：

1. **`v4: model reduction expressions and propagated domains`**  
   只改 `lcr.hpp/graph.cpp/tests`；先把 P0 domain property tests 做绿。
2. **`v4: prepared network and parameter diagnostics`**  
   改 `fit.cpp` + fixed/free/parameter IDs；native Try3 tests 通过。
3. **`v4: reuse prepared fit in try3 and try25`**  
   改 `search.cpp`，补 Try2.5 aggregate tests。
4. **`v4: qualified model selection`**  
   改 selector/report/tests；固定 mixed-AICc 行为。
5. **`v4: native report revision 2`**  
   完成 JSON contract、CLI golden tests。
6. **`v4: wasm abi parity`**  
   bindings/CMake/d.ts/smoke + rebuild `lcr.js/lcr.wasm`。
7. **`web: consume v4 report revision 2`**  
   fitTypes/validation/adapter/worker/UI/csv。
8. **`web: covariance bridge and diagnostics UX`**  
   历史 scan polar approximation（显式 opt-in）+ UI badges。
9. **`ci: native wasm frontend backend gates`**。
10. **`docs: freeze v4.1 contracts and validation`**。

每个 commit 都要求其层级测试通过；第 6 步以后开始要求 native↔WASM parity。

---

## 14. 最终 Definition of Done

只有同时满足以下条件，才把 v4 标记为“算法代码 + 网站接入完成”：

- [ ] aggregate bounds 六类边界反例全部通过；
- [ ] nested reduction expression/domain property tests 通过；
- [ ] Try3 与 Try2.5 共用 `prepareForFit(ExactElectrical)`；
- [ ] fixed zero DCR 不进入 atBound；
- [ ] diagnostics 参数全部有显式 ID/edge/quantity；
- [ ] mixed valid/invalid AICc 不再导致全组 RSS 降级；
- [ ] robust/irregular candidate 不显示伪 ΔAICc；
- [ ] native CLI 与 browser maxDepth 语义一致；
- [ ] optional covariance 能从 JS 传进 C++ 并通过已知 GLS test；
- [ ] `pnpm test:wasm` 有真实文件并通过；
- [ ] committed wasm 与 native parity 通过；
- [ ] browser smoke 覆盖 Try1/Try2/Try3/cancel/error/fixed/reduction/selection；
- [ ] native CTest + sanitizer + frontend Vitest/build + backend pytest 全部 CI green；
- [ ] `VALIDATION.md` 与实际网站状态一致；
- [ ] 实测 real4 不退化；固定随机 benchmark 无异常退化；
- [ ] UI 明确区分：搜索是否完整、连续优化是否仅局部、参数是否可辨识、模型是否只属 observed-band equivalence；
- [ ] 不出现“单端口恢复唯一物理拓扑”“Try1 全 RLC 完备”“Try2.5 连续全局最优”等超出证据的声明。

达到这些条件后，可以把该版本定义为：

```text
Try2 Exact:
  declared finite active multigraph space 内的条件性严格搜索（仅在完整且无数值失败时认证）

Try2 Tolerance:
  complete topology enumeration + bounded local continuous refinement

Try3:
  known topology/types -> R0 + exact electrical reduction -> propagated aggregate domains -> shared local fit

Try2.5:
  typed topology enumeration -> same Try3 prepared inner fit

Try1:
  declared normalized SP + physical Foster auxiliary family -> shared local fit -> qualified behavioral model selection
```

---

## 15. 参考实现/文献来源

以下外部资料用于确认所选实现路线，不替代项目自己的数学证明与测试：

1. **Eigen FullPivLU** — 完全主元 LU、rank/invertibility、`rcond()` reciprocal-condition estimate。  
   https://www.eigen.tuxfamily.org/dox/classEigen_1_1FullPivLU.html

2. **Eigen JacobiSVD** — thin U/V 足够用于 least-squares solve；适合当前 augmented LM，不必回退 normal equations。  
   https://eigen.tuxfamily.org/dox/classEigen_1_1JacobiSVD.html

3. **Emscripten `EXPORTED_FUNCTIONS`** — native symbol 必须显式保活/导出，C symbols 使用 `_` 前缀；新增 ABI setter 必须同步 linker export。  
   https://emscripten.org/docs/tools_reference/settings_reference.html

4. **nauty / Traces** — graph automorphism 与 canonical labeling；若后续 E=7/8 Strict 性能成为瓶颈，可作为 canonical augmentation/同构消重方向，而不是添加不可证明的频率启发式剪枝。  
   https://pallini.di.uniroma1.it/

5. **Hurvich & Tsai (1989), Biometrika 76(2), 297–307** — small-sample corrected AIC；支持继续要求 AICc 只在其有效/可比较模型条件下使用。  
   https://doi.org/10.1093/biomet/76.2.297

项目内的规范来源继续以 `AlgorithmLcr/LCRTheory_rendered.md`、`INPUT_FORMAT.md`、`OUTPUT_FORMAT.md` 和本次修改后的测试为准。
