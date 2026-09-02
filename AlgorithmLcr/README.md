# AlgorithmLcr — 单端口 RLC 网络识别算法研究

本目录是 LCR 分析仪项目的**算法研究沙盒**：两个独立的研究课题各占一个子目录，
均含完整推导（DESIGN.md）、Python 实现、测试套件与 demo。生产后端使用的 DSP/
拟合代码在仓库根 `backend/`（见上级 [AGENTS.md](../AGENTS.md)）。

## 目录

| 子目录 | 课题 | 设定 | 状态 |
|---|---|---|---|
| [Try1-Completely unknown single port fitting](Try1-Completely unknown single port fitting/) | 完全未知单端口拟合 | 元件类型/数量/参数全未知，仅有 z(f) 测量点 → 双引擎（串并联规范树枚举 + SK 有理拟合/Foster 综合）+ AICc 选择 | P1 完成，56 测试全绿；附 C++ 版 `cppversion/` |
| [Try2-Known component types, quantities and parameters](Try2-Known component types, quantities and parameters/) | 已知元件的拓扑枚举识别 | 元件多重集完全已知（电感含 DCR 双参数）→ 多重图无同构完备枚举 + 批量节点分析 + Try1 误差度量 | P1 完成，50 测试全绿 |

## 两个课题的关系

- Try1 因每拓扑需参数拟合而将搜索空间限制为串并联规范树；Try2 利用"元件值已知
  ⟹ 免拟合"的先验实现**全多重图穷举**，可识别 Try1 原理上不可达的桥式拓扑与
  重边结构，二者互补。
- Try2 的误差度量逐式沿用 Try1 DESIGN §5.1/§8.1（相对加权复残差、wRMSE、
  等价类聚类），并有数值对拍测试锁定一致性。
- 共享的测量模型：复高斯相对噪声（σ_k ≈ σ₀|ẑ_k|，σ₀ = 0.5%）、
  10 Hz–10 MHz、30 对数频点。

## 快速开始

```bash
# 环境：conda env lcr（Python 3.11 + numpy/scipy），见上级 AGENTS.md
cd "Try1-Completely unknown single port fitting" && conda run -n lcr python demo.py
cd "Try2-Known component types, quantities and parameters" && conda run -n lcr python demo.py --stats
```

各子目录的 `DESIGN.md` 是算法的唯一权威文档（推导、决策记录、复杂度论证、
参考文献）；`explain.md`（Try1）为面向使用者的算法讲解。
