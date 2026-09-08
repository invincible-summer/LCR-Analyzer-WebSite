# v4 统一输出格式：上三角邻接矩阵 + vector<Edge>

本文件与 INPUT_FORMAT.md 共同约束三引擎输出。实现位于公共 C++ 库；
旧 Python 实现、旧三套命名空间及 cppversion 路径已删除。

## Edge 与节点

```cpp
struct Edge {
    char type; // 'R' | 'L' | 'C'
    double parameter; // R[Ω], L[H], C[F]; >0
    double parameterOfCapacitanceDCResistance; // L 的串联 DCR[Ω], >=0
};
```

历史长字段名保留兼容，语义是**电感** DCR，与电容无关。R/C 的 DCR 恒零。
L+DCR 算一个器件、最多两个自由参数；固定零 DCR 不算自由参数。
端口为节点 0/1；无向、无自环、允许重边。

`Upper = vector<vector<vector<Edge>>>`，共 V 行，第 i 行长度 `V-1-i`；
槽位 `(i,j)` 访问 `upper[i][j-i-1]`，不是 `upper[i][j]`。
Try3 保留原始标签和空洞节点；v4 保留完整输入 V（包括末尾孤立节点）。
Try1 从 2 开始分配内部节点；Try2 输出枚举代表元的节点标签。

## 文本矩阵块

```text
adjacency[1] V=4 (ports 0,1):
  (0,1): R 1.000e+03 | C 1.000e-07
  (0,2): L 9.980e-04 dcr 4.900e-01
  (2,3): R 1.000e+02
```

每个非空槽位一行，按上三角行主序；同槽多边用 ` | ` 分隔。
值以 `%.3e` 显示；L 的 dcr==0 时省略整个 dcr 段。
17 位机器精度由原生 JSON 保留；显示块不承诺参数逐位往返。
Try1/Try2 对 Top-K 行为等价类代表元输出矩阵，rank 从 1 开始。
Try3 单拓扑单结果，附 `# merged group ...` / `# dropped edge ...` 注释。

## 确定性与归约

Try1：SER/PAR 扁平化、规范子树字符串排序；SER 内部节点按端口 0 侧顺序分配。
同类 R/C 可合并，同类串联 L 可合并；串联 R 折入 L 的 DCR。
一般并联 L+DCR 不可合并。`exactN` 约束规范模型边数。

Try2：按节点槽位和已排序器件队列确定性枚举，对内部节点置换及互易端口交换
取规范签名去重。固定输入多重集全部器件都放入图，不做改变元件身份的合并。

Try3：完整割点死区删除、严格串并联归约迭代到固定点。
每个群放一条聚合边；members 指向原输入边序号（从 0 起）。
merged 成员的单独物理值不可由输出聚合值唯一恢复；dropped 不输出矩阵边。

## 原生报告（schema revision 2）与网站接入

原生 CLI 默认输出摘要、矩阵块及归约注释；`--json` 输出 `lcr.native.v4`，
自 revision 2 起顶层携带 `schema_revision:2`、`engine_version:"4.1.0"`、
`noise_model`（`relative_unknown_scale | supplied_covariance`）、
`equivalence:"observed_band"` + `equivalence_metric:"relative_curve"` +
`equivalence_threshold`，以及运行级 `selection:{criterion,qualified}`。
criterion ∈ `AICc | RSS_EXACT | RSS_COMMON | RSS_DIAGNOSTIC_FALLBACK | NONE`。

保留 candidate 的 `rank/devices/n_params/wrmse/max_rel/aicc/rss`、
`adjacency:{v,slots:[{u,j,edges:[{t,p,d}]}]}` 和 `theory:{f,re,im}` 结构，
新增 `original_topology_key / effective_topology_key / effective_devices`。
原生 theory 在输入频点直接求值；网页适配器负责扩展绘图频栅。
非有限诊断和无效 AICc 输出 JSON `null`，不用 `1e999` 或巨大有限哨兵。

每候选另含：

- `selection:{eligible,criterion,score,delta,reasons}`：primary/diagnostic
  分层与资格原因；校准 `delta` 仅存在于合格的 AICc 运行。
- `diagnostics` 增加独立维度 `optimizer / numerical_status / identifiability_status /
  fit_objective`（verdict 保留为派生摘要），以及显式参数描述符数组
  `parameters:[{id,edge,quantity,kind,value,lower,upper,free,fixed,weak,
  at_bound,standard_error,ci95}]`。id 即优化器参数号；消费方按 id/edge/quantity
  对齐，禁止按连续索引推断（例如「L 占两个连续号」不再成立）。
  fixed 是独立状态，绝不标记 at_bound/weak。

`groups` 条目带 `gid/u/v/kind/value/dcr/members/mode`（mode 为派生摘要），并附
传播有效域 `value_bounds/dcr_bounds`、归属参数 `parameter_ids`、以及追踪用
表达式树 `value_expr/dcr_expr`（`{"op":"value|dcr","edge":N}` 叶与
`{"op":"sum|hsum","children":[...]}` 组合）。普通视图可只显示表达式摘要。
`members` 保持输入边索引，不能按邻接矩阵输出次序反推。

附加报告：搜索空间、模式、完备性、预算终止、种子、计数和耗时；每候选包含
优化终止、启动一致性、局部 Jacobian 秩/奇异值/条件数、弱参数、边界、
适用时的标准误差及近似 95% 区间、线性求解质量及 robust 标记。原始相对误差不被 robust 权重覆盖。
`enumeration_complete` 不代表连续优化全局最优，也不代表物理接线唯一。
聚类名称为 observed-band，非符号电学等价证明。

CLI 退出码：0 有结果且搜索完成；1 输入/运行错误；2 有部分结果但预算耗尽；
3 无可用候选。数值失败计数非零时不得宣称全候选空间全局证书。

## 必须验证

1. 上三角形状、端口与多重边守恒。
2. 从矩阵重建图，通过独立求值器核对 Z(f)。
3. 规范顺序、零 DCR、省略规则与归约来源。
4. 原生 JSON 可解析、不含 NaN/Infinity 数值字面量。
5. revision 2 字段（schema_revision、selection、parameters、groups 域/表达式）
   存在且合法；native 与已提交 WASM 产物按 parity 脚本一致。

浏览器适配使用原生 JSON。
