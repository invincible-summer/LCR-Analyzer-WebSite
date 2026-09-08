# v4.1 原生验收记录

2026-09-09，dev 工作区（v4.1 / 原生报告 revision 2）；Linux x86_64，GCC 15.2.0，
C++17，Eigen 3.4.0；Emscripten 6.0.9（WASM 产物），Node 22 / pnpm 11 / Vitest 2，
Python 3.11（conda `lcr`）。这里记录实际运行结果，不把测试覆盖率或有限样本恢复率
解释为普适数学证明。CI 门（`.github/workflows/ci.yml`）复现同一组命令。

## 构建与正确性检查

```sh
cmake -S AlgorithmLcr -B /tmp/lcr-v4-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/lcr-v4-build -j2
ctest --test-dir /tmp/lcr-v4-build --output-on-failure
```

两个 CTest 项目 `core`、`real4` 通过。`lcr_tests` 含 **2935 项**运行时检查
（v4.0 基线 995 项），使用独立 require，不依赖 Release 下会被删除的 assert。

覆盖文件行号/字段校验、17 位往返、打印矩阵、从矩阵重建图；原子电路与平衡桥；
100 个随机小图的独立 long-double 求解及五点差分导数；完整割点死区、串并联归约、
一般并联 L 保留、零 DCR、开路、精确/近反谐振、标量导纳相消诊断。

v4.1 新增的归约性质测试：9 种精确归约（含嵌套 `(R‖R)+R`、`(C+C) 串 C`、
L+L+R）在每个 24 个对数频点上保持 Z（相对误差 ≤1e-11），表达式树与成员映射
显式核对；随机叶采样 50×9 次全部落在表达式传播域内；悬挂死区边 Jacobian 恒零；
7 类聚合边界反例（rMin‖rMin、2·rMax、2·cMax、cMin/2、2·lMax、2·dcrMax、
L(dcrMax)+rMax）全部位于传播域内，Try3 无噪恢复 wRMSE<1e-8 且不被单器件箱
截断；固定零 DCR（tol>0、绝对容差 0）报告 fixed 而非 at_bound，nParams/rank
按自由参数计；六类选择器场景（全有效、混合 null-AICc、秩亏连续统、触界、
robust 回退、无 primary 回退）验证分层语义与「单个无效候选不得拖垮整组」。

枚举验证对 E=1..5 比较完整规范签名集合；独立 oracle 通过简单端口路径判定活动性、
全标签排列确定规范签名。额外对三个不同类型/数值元件比较完整带颜色赋值集合。
Try2.5 聚合检验：两串联 R 有效参数 1 个、R+L 串联为主 L + 聚合 DCR 两参数，
E≤5 oracle 不变。

原生 CLI：JSON rev 2 契约（schema_revision/engine_version/selection/parameters/
groups 域与表达式）断言通过，退出码与预算行为不变。

内存/未定义行为检查命令：

```sh
cmake -S AlgorithmLcr -B /tmp/lcr-v4-san -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g0 -O1'
cmake --build /tmp/lcr-v4-san --target lcr_tests -j2
ASAN_OPTIONS=detect_leaks=1 /tmp/lcr-v4-san/lcr_tests
```

AddressSanitizer、UndefinedBehaviorSanitizer 和泄漏检查通过（2935 项检查全部通过）。

## 四组实测

```sh
/tmp/lcr-v4-build/lcr_bench real4 examples
```

基准使用 20 次启动、200 次迭代、seed=1；Try1 maxN=4，Top-K=8。
下表均为测量频点、原始相对权重下的 rank-1 wRMSE，单位 %。

| 数据 | Try1 | Try2 Exact（标称值） | Try2 Tolerance | Try3 |
|---|---:|---:|---:|---:|
| data1 | 0.5255 | 2.3355 | 0.9229 | 0.9229 |
| data2 | 0.9005 | 5.6132 | 1.8951 | 1.8951 |
| data3 | 1.4683 | 6.3641 | 2.1167 | 2.1167 |
| data4 | 0.5040 | 5.2584 | 0.5040 | 0.5040 |

与 v4.0 基线完全一致（选择器与域传播改动未影响实测 rank-1 行为）。
Try3/Tolerance 门槛分别为 1.0%、2.1%、2.3%、0.6%；Try1 门槛为各值乘 1.5。
这组门验证拟合误差，不要求 Try1 的 rank-1 物理拓扑唯一或必定等于已知电路。
Tolerance 验收显式使用 ±50% 宽参数箱，以覆盖 data4 的强相关参数；
Exact 使用文件中的标称值，不能要求它达到自由参数拟合的最小误差。
先验文件位于 `examples/v4/`，可逐组用 CLI 重放。

## 固定种子随机压力集

```sh
/tmp/lcr-v4-build/lcr_bench random 40 21
# 独立重放未通过行为门的案例
/tmp/lcr-v4-build/lcr_bench case 7 21
```

生成器与 v4.0 基线相同（2..4 器件规范 SP、对数随机参数、含零 DCR、噪声/平滑/
每第七例离群点+robust；行为门 `max(0.003, 4*(noise+smooth))`）。

| 指标 | v4.0 基线 | v4.1（本次） |
|---|---:|---:|
| 行为 pass@1 | 37/40 | 35/40 |
| 行为 pass@8 | 39/40 | 39/40 |
| 类型图结构匹配 @1 | 10/40 | 9/40 |

**选择器改动（plan 10.8 要求的 before/after 记录）**：pass@1 下降 2 例已逐例归因，
均为 v4.1 明确规定的选择语义的直接后果，而非拟合质量退化：

- case 0（robust + 零噪 + 零 DCR）：robust 运行按规范进入
  `RSS_DIAGNOSTIC_FALLBACK`（无可比较的 robust 似然，不显示校准 ΔAICc），
  纯 RSS 排序使吸收离群点的过拟合结构升至 rank-1（clean wRMSE 7.3e-8 → 1.1e-2）。
- case 10（系统性 smooth 误差）：真模型在迭代预算内未达严格梯度收敛
  （`max_iterations`，wRMSE 已到 3e-3 噪声地板），按 eligibility
  「optimizer converged」条款降为 diagnostic-only；唯一快速收敛的劣拟合模型
  （wRMSE 0.96）成为 rank-1。真模型仍在候选表中（rank 2，诊断标记）。

case 7、21、35 与基线一致（既有失败）；pass@8 保持在 39/40。case 7 的离群点
诱导伪结构问题在 v4.1 仍未解决，诊断与 Top-K 全部保留，不隐藏 outlier/systematics。

## WASM 与网站回归（本地实际执行）

```sh
cd frontend
pnpm build:wasm        # Emscripten 6.0.9，产物提交至 src/wasm
pnpm test:wasm         # 20 项检查
pnpm test:parity       # 190 项检查（native CLI ↔ 已提交 WASM）
pnpm test              # Vitest 42 项
pnpm build             # vue-tsc + Vite 生产构建
node scripts/browser-smoke.mjs   # 14 个用例（Playwright + vite preview）
cd ../backend
conda run -n lcr python -m pytest  # 9 项通过
```

WASM smoke 覆盖：版本/revision 断言、Try1 单 R、Try2 Exact 证书、Try3 串并联
聚合（域超 rMax）、固定零 DCR 与宽域触界状态区分、port-open 契约、协方差 GLS
路径 + 非 SPD 拒绝 + null 清除、重复运行内存释放、maxDepth/maxN 独立。
parity 在 5 个固定用例上比对 native CLI `--json` 与已提交 WASM：拓扑/类数/
选择资格与原因/归约表达式与域/邻接结构一致，数值在显式浮点容差内
（噪声地板拟合只要求「同为基本零残差」，Libm 差异经 AICc 对数放大的情形单独定义）。
浏览器 smoke 覆盖 Try1/Try2/Try3、取消、port-open、空结果、null-AICc、
maxDepth 独立、固定 DCR「固定」显示、混合 AICc 不降级、协方差徽标/GLS、
Try3 聚合域超单器件上限、Try2 数量 >8 拒绝；页面错误为零。

数据库：预置旧格式 fitresults 表保留策略不变；网页通过 Worker/WASM（rev 2）
执行三个引擎，加载时断言版本。真实 OSL 校准和硬件验证仍不属于已完成验收。
