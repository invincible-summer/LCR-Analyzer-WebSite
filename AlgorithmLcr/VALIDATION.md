# v4.1.2 原生验收记录

2026-09-09，dev 工作区（v4.1.2 / 原生报告 revision 2，基线 `f2943da`）；WSL Ubuntu
x86_64，GCC 15.2.0，CMake 4.2.3，C++17，Eigen 3.4.0；Emscripten 6.0.9（WASM 产物），
Node 22 / pnpm 11 / Vitest 2，Python 3.11（conda `lcr`；backend 另用仅安装
`requirements.txt` 的全新 venv 复现 CI）。这里记录实际运行结果，不把测试覆盖率或
有限样本恢复率解释为普适数学证明。CI 门（`.github/workflows/ci.yml`）复现同一组命令。

v4.1.2 是 correctness freeze：只修复可证明的边界/契约问题，nodal、SVD-LM、归约与
域传播、Try2 枚举、robust IRLS 本体、阈值与 bounds 全部未动。

## 构建与正确性检查

```sh
rm -rf /tmp/lcr-v412-build
cmake -S AlgorithmLcr -B /tmp/lcr-v412-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/lcr-v412-build -j4
ctest --test-dir /tmp/lcr-v412-build --output-on-failure
```

两个 CTest 项目 `core`、`real4` 通过。`lcr_tests` 含 **3032 项**运行时检查
（v4.1 基线 2935 项），使用独立 require，不依赖 Release 下会被删除的 assert。

v4.1 全部既有覆盖保持：文件行号/字段校验、原子电路与平衡桥、100 个随机小图的独立
long-double 求解及五点差分导数、完整割点死区、9 类精确归约性质与表达式树、
7 类聚合边界反例、E=1..5 枚举签名 oracle 与带颜色赋值 oracle、悬挂死区 Jacobian
恒零、六类选择器场景（语义按 v4.1.2 更新为 scored 先于 diagnostic 的分层不变量）。

v4.1.2 新增断言（对应 plan §12.1 列表）：

- pre-cancel no-best：首 start 前取消，`fit()` 返回 `budget_exhausted`、
  `NOT_EVALUATED`、`LOCAL_FIT_UNCONFIRMED`，描述符完整；`try3` 层
  `termination=budget_exhausted`、`enumeration_complete=false`、候选为空；
- all-starts numerical failure：域固定的并联 LC 反谐振使全部 start 失败，
  `fit()` 返回 `numerical_failure` 且不访问未赋值坐标，3 个参数描述符
  id/edge/quantity 完整；
- Try3 >8 内部节点：11 节点 9 内部节点串联 R 链（旧代码复现 canonical
  8 内部节点上限异常），无噪恢复 wRMSE<1e-8、精确归约为单 R 聚合、
  original key 保留用户标签（含 `0,2:R`/`1,10:R`）、effective key 非空、
  20 次边行乱序后 key 完全一致；Try2 canonical oracle 逐位不变；
- forward disposition：`numerics::classify` 的 Accept/Warn/Reject 边界
  （rcond∈[reject,warn) 且 backward 合格为 Warn；低于 reject 或 backward
  超限为 Reject）；真实近反谐振点落入 Warn 窗口；Try2 Exact 保留该候选
  （`numerical_status=WARN`、`identifiability=NOT_APPLICABLE`）、枚举完整但
  `termination=complete_with_numerical_warnings` 且撤销连续全局证书；
  精确反谐振（SINGULAR）仍按 numerical failure 计数；
- provisional selector：三层排序单元测试（未收敛低 AICc 压过差 qualified、
  qualified 更优时保持校准语义、scored 先于 diagnostic 与 RSS 无关、
  robust 恒回退、等价类代表 Qualified>Provisional>Diagnostic）；
- `observed_grid` JSON 契约与 `engine_version 4.1.2` 断言。

内存/未定义行为检查（全新目录）：

```sh
rm -rf /tmp/lcr-v412-san
cmake -S AlgorithmLcr -B /tmp/lcr-v412-san -DCMAKE_BUILD_TYPE=Debug   -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g0 -O1"
cmake --build /tmp/lcr-v412-san --target lcr_tests -j4
ASAN_OPTIONS=detect_leaks=1 /tmp/lcr-v412-san/lcr_tests
```

AddressSanitizer、UndefinedBehaviorSanitizer 和泄漏检查通过（3032 项检查全部通过，
含上述 no-best 两条异常路径——旧代码在空坐标上为未定义行为）。

## 四组实测

```sh
/tmp/lcr-v412-build/lcr_bench real4 examples
```

基准 20 starts / 200 iterations / seed=1；Try1 maxN=4，Top-K=8。rank-1 wRMSE（%）：

| 数据 | Try1 | Try2 Exact（标称值） | Try2 Tolerance | Try3 |
|---|---:|---:|---:|---:|
| data1 | 0.5255 | 2.3355 | 0.9229 | 0.9229 |
| data2 | 0.9005 | 5.6132 | 1.8951 | 1.8951 |
| data3 | 1.4683 | 6.3641 | 2.1167 | 2.1167 |
| data4 | 0.5040 | 5.2584 | 0.5040 | 0.5040 |

与 v4.1 基线逐位一致：拟合本体无漂移。Try3/Tolerance 门槛 1.0/2.1/2.3/0.6%，
Try1 为各值 ×1.5，全部通过。

## 固定种子随机压力集

```sh
/tmp/lcr-v412-build/lcr_bench random 40 21
/tmp/lcr-v412-build/lcr_bench case 10 21
/tmp/lcr-v412-build/lcr_bench case 0 21
/tmp/lcr-v412-build/lcr_bench case 7 21
```

| 指标 | v4.1 基线 | v4.1.2（本次） |
|---|---:|---:|
| 行为 pass@1 | 35/40 | 35/40 |
| 行为 pass@8 | 39/40 | **39/40**（硬门 pass@8≥39 达标） |
| 类型图结构匹配 @1 | 9/40 | 9/40 |

逐例重放 40 个 case（固定种子）核对 pass@1 失败集：**{0, 7, 10, 21, 35}，与 v4.1
基线完全一致**——provisional 层没有引入任何新的 pass@1 回归，也没有隐藏既有失败。
生成器与行为门未改。

**case 10 归因修正（plan 6.8 before/after 记录）**：v4.1 把真模型（wRMSE≈3e-3，
`max_iterations`）降为 diagnostic 的记录原因只有 `optimizer_not_converged`；
v4.1.2 的完整 JSON 显示这些优良候选**同时** `parameter_at_bound`（真相 DCR=0，
拟合贴到线性非负坐标的 0 下界，`at_bound=[1,5]` 等）。按本计划冻结语义
（§6.3 at-bound 保持 Diagnostic；§13 不改 bounds、不设魔法阈值），它们保持
diagnostic，case 10 pass@1 仍失败（rank-1 clean wRMSE 0.9565 > gate 0.02）。
optimizer-status 这一降级因素本身已消除并可证明：case 10 的 rank 2–4
（`stalled`/`max_iterations`、满秩、无触界、AICc −81.0/−79.2/−74.9）现在以
`AICc_PROVISIONAL` 参与 scored 排序，排在 qualified rank-1（AICc −85.5）之后
按 AICc 升序，优良候选（AICc≈−1460）不因「未收敛」本身被压制——其仍在
diagnostic 的唯一原因是触界。运行级 criterion 仍为 `AICc`（rank-1 是 qualified）。

case 0（robust + 零噪 + 零 DCR）：保持 `RSS_DIAGNOSTIC_FALLBACK` 纯 RSS 诊断排序
（rank 1–8 全部 diagnostic），rank-1 clean wRMSE 1.1e-2 > gate 3e-3，与基线一致；
实现中 tier 判定对 run 级 robust 标志感知（robust 运行中从未触发 IRLS 重加权的
候选 `robustUsed=false`，不得进入 scored 集——实现中期该缺陷使本地 random 40
出现 1 例额外 pass@1 失败（35→34），修复后 40 例逐例重放核对：pass@1 失败集
与基线完全一致，无漂移）。无伪造 ΔAICc。

case 7（离群点诱导伪结构，既有失败）：behavior=0，rank-1 clean 0.112 > gate 0.024，
与基线一致，未通过改门消失。

## WASM 与网站回归（本地实际执行）

```sh
cd frontend
pnpm build:wasm        # Emscripten 6.0.9，产物提交至 src/wasm
git diff --exit-code -- src/wasm   # 已提交产物与重建一致（通过）
pnpm test:wasm         # 24 项检查
pnpm test:parity       # 200 项检查（native CLI ↔ 已提交 WASM）
pnpm test              # Vitest 42 项
pnpm build             # vue-tsc + Vite 生产构建
node scripts/browser-smoke.mjs     # 16 个用例（Playwright + vite preview）
cd ../backend
conda run -n lcr python -m pytest  # 9 项通过
python -m venv /tmp/ci-venv && pip install -r requirements.txt && python -m pytest
                       # 仅装 requirements.txt 的全新 venv：9 项通过
```

WASM smoke 覆盖 v4.0 起 20 项 + 新增：`engine_version 4.1.2`、
`equivalence=observed_grid`、Try3 >8 内部节点链（保留标签的 topology key）。
parity 比对 5 个固定用例的拓扑/类数/选择资格与原因/归约表达式与域/邻接结构，
并新增 equivalence/engine_version 契约一致性断言。浏览器 smoke 覆盖既有 14 用例
+ 新增：迭代上限=1 的 Try1（RC 数据）产生 provisional rank-1，UI 显示
`AICc 顺序（含未收敛候选…）`准则、「未收敛候选」徽标、ΔAICc 显示 —、
observed-grid 用语；Try3 9 内部节点链正常拟合（表达式 `R1+R2…`）。页面错误为零。

前端语义：qualified 候选显示校准 ΔAICc；`AICc_PROVISIONAL` 候选只参与排序、
`delta=null` 显示 —；robust/无 scored 回退显示「RSS 诊断回退（诊断排序，非校准
模型选择）」；Try3 帮助文案更正为「R/L/C 正参数用 log 坐标，DCR 用可精确表示 0
的线性非负坐标」；弱参数 tooltip 说明 relative-effect 启发式在零/近零 DCR 边界的
退化（阈值与 selector 未改）。

## CI（GitHub Actions，实际运行）

基线 run `34301046773`（`f2943da`）：native/sanitizer/wasm+frontend 绿，
browser smoke 与 backend pytest 红。两份失败 job 日志已取回并逐条归因（未猜测、
未 continue-on-error、未删用例、未放宽断言、未移出 required gate）：

- backend：产品代码 `app/dsp/__init__.py → spectrum.py` 依赖 scipy，
  `requirements.txt` 未声明（本地 conda 环境恰好装有 scipy 才通过）；
  → 声明 `scipy>=1.11`，全新 venv 复现验证 9 项通过。
- browser smoke：`page.goto net::ERR_CONNECTION_REFUSED`——vite preview 绑定
  `localhost`（runner 上仅解析 IPv6 `::1`），smoke 与就绪 curl 访问 IPv4
  `127.0.0.1`；静默的 30×1s curl 循环未拦截。→ preview 显式
  `--host 127.0.0.1`，就绪探测失败即让 serve 步骤失败。语义断言未改。

修复 commit `b2984eb` 推送后 CI run `34328538309` **五个 job 全绿**：
native（ctest+real4）、sanitizer（ASan+UBSan）、wasm+frontend（含
`git diff --exit-code -- src/wasm`）、browser smoke、backend pytest。

数据库与 Worker/WASM 契约不变（rev 2）；真实 OSL 校准和硬件验证仍不属于已完成
验收；v4.1.2 完成后仍不得宣称：Try1 恢复任意 RLC 图、Try3 连续参数全局最优、
满秩证明全局唯一内部结构、robust 回退是严格 robust 模型选择、observed-grid
等价等于连续频带数学等价。
