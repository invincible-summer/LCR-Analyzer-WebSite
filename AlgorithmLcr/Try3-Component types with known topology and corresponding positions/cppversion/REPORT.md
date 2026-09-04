# Try3 (topofit_id) Python → C++ 重构验证报告

日期：2026-09-04 · 环境：WSL2 Ubuntu · g++ 15.2（-O2，C++17，零第三方依赖）
参照实现：Python 3.11 + numpy + scipy（conda env `lcr`）

---

## 0. 结论摘要

| 项目 | 结果 |
|---|---|
| 重构范围 | `topofit_id` 全部 7 个模块（graph 减支 / nodal 伴随 Jacobian / metric / fit 多起点漏斗 / synthetic / adjacency / iofmt）+ identify / identify_many，8 个 C++ 源文件 ≈ 2 900 行 |
| 求解器 | scipy `least_squares(trf)` → 自研箱约束 LM（SVD 子问题 + λ 尺度化 + 反射边界 + 投影梯度回溯保底 + 链式深抛光） |
| 单元测试 | **21/21 用例、764 项 check 全绿**（减支规则/聚合公式/30 组随机图 Z 不变、闭式对照、Jacobian vs 中心差分（可见通道 <1e-4）、弹性系数、秩亏标记、弱参数标记、iofmt/邻接阵、端到端） |
| demo | 无噪 12/12 机器精度（0.5 s）、0.5% 噪声 12/12 达 3× 底 |
| 2000 组一致性 | **REDUC_DIFF = 0**（减支结构 2000/2000 精确一致——离散核心完全锁定）；双端达底时拟合曲线全部一致（CURVE_DIFF 1 例为 rank1/4+weak3 的家族代表元）；单侧未达底 cpp 13 vs py 4（0.65% vs 0.2%，机理见 §3） |
| 性能 | py 87 s → cpp 13.6 s（2000 组 18 进程墙钟 ≈ 6.4×；demo 单套 12 DUT ≈ 4×） |

## 1. 算法逐模块论证

1. **减支 F1–F4**：每条规则是初等网络恒等式（KCL/KVL 直接推论），
   对一切正值参数保持 Z 恒等 ⟹ 与数值无关、拟合前执行合法；每遍
   边数严格递减 ⟹ O(E) 遍终止。C++ 逐字符复刻 py 的确定性贪心序
   （最小节点标签优先、dict 插入序、F3/F4 单步合并），2000/2000
   结构一致是完备的机器验证。
2. **节点分析 + 伴随 Jacobian**：Z=(Y_red⁻¹)₀₀；dZ/dθ_t =
   −(dy_t/dθ)(x_ri−x_rj)²（Director–Rohrer 伴随法，一次 LU 后
   O(Mp)）；对数参数导数逐式复刻（R: −λy；C: +λy；L: −λ·sL·y²、
   −λ·Rd·y²）。中心差分交叉验证锁定（可见通道相对差 <1e-4）。
3. **双重归一化是精确变量代换**：R̃=R/z₀、L̃=ω₀L/z₀、C̃=z₀ω₀C、
   R̃d=Rd/z₀ 下网络方程在 (s/ω₀, z/z₀) 尺度下逐项齐次 ⟹ 归一化
   不改变拟合问题的解，只改善条件数（DESIGN §5.1/D1）。
4. **多起点漏斗 A/B/B2/C/D/E**：阶段结构、起点公式（单位/LHS/
   中心箱 LHS/谐振种子/拓扑配对/阻尼同伦/最后手段混合）与升级阈值
   0.03 全部按 py 移植；随机流为 mt19937_64（同分布、不同流）。

## 2. 移植过程中发现并修复的实现缺陷

| # | 缺陷 | 症状与修复 |
|---|---|---|
| 1 | **聚合边指针悬空**（use-after-free）：F3/F4 与悬空删除先取 `work` 内元素指针，`work = move(next)` 释放旧缓冲后再解引用 | 命名 DUT 段错误 / members 垃圾值；修复为先把 expr/members 拷贝出来再重排（`e1Expr/e2Expr/victimMembers`） |
| 2 | **法方程子问题在秩亏 Jacobian 上失效**：J^T J 的条件数是 cond(J)²，秩亏案例（cond=∞）中 λ 阻尼产生畸变步，多起点也进不了真谷底（cpp 18 例 vs py 4 例未达底） | 子问题改为增广最小二乘 `min‖J·dx+r‖²+λ‖D^½dx‖²` 的单边 Jacobi SVD 求解（cond(J) 而非平方），修复后 00667 等秩亏案例从 7.9e-4 → 2.5e-10（与 py 2.3e-10 同级） |
| 3 | **λ 上限为绝对值 1e14**：H 对角可达 1e16 ⟹ 初始化即越限、一次拒绝即终止 | 改为尺度相关 `lamCap = 1e6·max(diag H)`；λ 下限同理 `1e-10·max(diag H)` |
| 4 | **边界停滞**：梯度向外压死在界上，阻尼 GN 步全被拒 | 预测下降改用**投影后步长**计算（Bertsekas 投影 LM）+ 边界**反射** + attempt 8 起**投影梯度回溯线搜索**保底（λ 重置）|
| 5 | 窄谷深收敛不足 | 精修后追加**链式深抛光**（top-1 起点、预算 8000、重启 λ，至多 4 轮）；case_00003 由 1e-5 → 8.6e-13 |
| 6 | 二级报告 use-after-move（`g.mode` 被移空） | 改读 `groups.back().mode` |

（#2–#5 为对 py 侧 scipy TRF 能力差距的逐层补强——每层都有单案例
前后对比；最终 cpp 单侧未达底 18→13，py 侧 4。）

## 3. 残余 13 例的机理（诚实声明）

全部为无噪/带噪的**高 Q 窄谷 + 秩亏复合病态**案例：从 py 解出发，
C++ 目标函数复算 rss=1e-29 且 LM 立即收敛（nfev=1）——证明求解器与
目标一致，差距仅在"从公共起点（单位起点/LHS）出发的逐起点深收敛
能力"：scipy TRF 的精确信赖域子问题（reflective 变换 + 精确 LSQ
路径）在极窄谐振谷中仍强于本 LM（同型不对称在 Try1 REPORT §6.3
以反方向存在——彼处自研 LM 强于 TRF；两端各自证实"多起点算法的
逐案例落点是优化器+随机流的联合抽签"）。彻底闭合需移植完整 TRF
子问题算法（P2 路线）。

## 4. 复现

```bash
cd cppversion
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/tf_tests                         # 21 用例
./build/demo --noiseless                 # 12/12
rm -rf /tmp/t3cases
python -B tools/gen_cases.py /tmp/t3cases 2000 1
python -B tools/run_py.py /tmp/t3cases 18     # FitConfig(n_starts=16, n_center=16)
ls /tmp/t3cases/case_* -d | xargs -P 18 -I{} build/case_run {} {}/cpp_result.json
python -B tools/compare.py /tmp/t3cases
```
