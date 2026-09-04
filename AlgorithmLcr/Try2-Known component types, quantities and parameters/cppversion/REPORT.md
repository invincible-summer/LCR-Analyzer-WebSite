# Try2 (netgraph_id) Python → C++ 重构验证报告

日期：2026-09-04 · 环境：WSL2 Ubuntu · g++ 15.2（-O2，C++17，零第三方依赖）
参照实现：Python 3.11 + numpy（conda env `lcr`）

---

## 0. 结论摘要

| 项目 | 结果 |
|---|---|
| 重构范围 | `netgraph_id` 全部 11 个模块（components/graph/enumerate/nodal/metric/filters/selector/synthetic/report/adjacency/iofmt）+ identify 入口，12 个 C++ 源文件 ≈ 2 400 行 |
| 单元测试 | **33/33 用例、1 425 项 check 全绿**（枚举计数锁定、闭式/独立 MNA 交叉验证、VTP、R0、格式 round-trip、邻接阵 Z 交叉验证、命名 DUT 端到端） |
| demo | 无噪与 0.5% 噪声均 **10/10 恢复**（E=6 混合桥 0.10–0.26 s） |
| 2000 组一致性 | **0 FAIL**：PASS 1 897（双端 rank-1 类含真值）+ TRUTH_RANK2 17（簇边界，真值在某一端 rank≤3）+ ONE_SIDED 25（噪声底单侧翻转，13 py-only:12 cpp-only 双向均衡）+ TRUTH_MISS 61（双端一致未入 top-3 的难例）；另有正交标记：CLUSTER_BORDERLINE 1、MACHINE_FLOOR 1、漏斗幸存 ±1 122 例 |
| 强离散锁定 | `n_structures` 与 `n_candidates` **2000/2000 逐例相等**（E=1..6 计数 1/2/4/11/31/104 结构、1/2/10/98/1426/27542 候选与 DESIGN §4.4 表一致） |
| 性能 | py 33.6 s → cpp 3.6 s（18 进程并行墙钟，**≈9×**；单核 E=6 案例 1.4 s → 0.10 s ≈ 14×） |

## 1. 算法逐模块论证（正确性依据）

1. **完备性（无漏）**：结构层枚举走遍 V∈[2,E+1] 的全部多重组合
   （combinations_with_replacement 的字典序完整遍历），连通性与 R0
   （死区定理：G−c 的无端口连通分量净电流恒为零，DESIGN §2.5）只删除
   电气不可观测的接线。C++ 计数与 py 逐例相等即为其机器验证。
2. **无重（同构免费）**：canonical = 重标记群 {端口互换}×Sym(内部节点)
   作用下多重向量的字典序最小者；每轨道恰一个最小元 ⟹ 去重完备。
   指派层以"逐槽排序键 + Aut 群最小化"序列化去重，同键元件自动坍缩。
3. **求值**：节点分析 Y 压印 + 部分主元 LU（与 LAPACK 同为双精度部分
   主元，舍入误差强相关 ⟹ 近简并候选的跨语言排序分歧最小化；曾实测
   long double 求解反而使一致性变差，见 §3）。
4. **漏斗 F4 无损**：探针阈值 best×1e6，任何被弃候选在探针点相对失配
   ≫1e3，不可能进入排序前列（单元测试 `e2e_funnel_keeps_truth` 锁定）。
5. **聚类 F6**：外扩 10×频带 200 点网格上 max 相对偏差 <
   max(1e-3, 3σ̂)；代表元二级序（内部节点数 → SP 优先 → 规范串）。

## 2. 与 Python 参考的确定性问题及修复

| 问题 | 处置 |
|---|---|
| `coarse_indices` 用 Python `round`（银行家舍入，round(14.5)=14） | C++ `roundHalfEven` 逐位复刻（M=30 → 探针 [0,14,29]） |
| 类内代表元末级排序键是 `str(serial)` 字符串序（非数值序） | C++ 实现 `pyRepr`（最短回环 + Python 定点/科学记数法切换与两位指数），按 Python repr 逐字符复刻排序键（14 个边界值对拍通过） |
| JSON 数值 `0` vs `0.0`（int/float）导致的比较假差异 | 对拍器统一数值类型 |
| 元件键序 `('C'<'L'<'R', value, dcr)` | C++ 以字符序复刻（注意不是 R<C<L） |

## 3. 残余分歧的机理论证（非缺陷）

2000 组中 103 例（5.2%）存在跨语言非致命分歧，三类机理均有实验证据：

1. **噪声底统计翻转**（25 例，13 py-only vs 12 cpp-only 双向均衡）：
   0.5% 噪声下多个近简并类的 χ² 差在噪声涨落内，最优次序统计量随
   候选集下移（DESIGN §6.3 的多重比较效应）；两端引擎各自"正确"。
2. **病态条件数**（簇边界/漏斗 ±1，≈180 例次）：混有小电容（fF 级）
   与欧姆通路的候选在频带低端 cond(Y) 可达 8.4e12（实测），
   双精度前向误差 ~1e-3 相对 ⟹ rss 差 ~1e-4、聚类/排序在容差边缘
   翻转。双端（LAPACK 与本 LU）各在其舍入误差内。改 long double
   反而扩大跨语言差异（py 自身 1e-3 误差不再相消）——回退为双精度。
3. **机器零排序**（无噪案例）：py 复现自身舍入得 rss=0，cpp 同一布线
   ~1e-9；rss=0 与 1e-9 的次序无信息量（对拍器按 wrmse<1e-4 归类）。

## 4. 复现

```bash
cd cppversion
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/ng_tests                         # 33 用例
./build/demo --noiseless                 # 10/10
# 2000 组对拍（临时数据在 /tmp）
rm -rf /tmp/t2cases
python -B tools/gen_cases.py /tmp/t2cases 2000 1
python -B tools/run_py.py /tmp/t2cases 18
ls /tmp/t2cases/case_* -d | xargs -P 18 -I{} build/case_run {} {}/cpp_result.json
python -B tools/compare.py /tmp/t2cases
```
