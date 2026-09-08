# Try1 · 未知辨识

网页已接入 v4 C++ / WASM，也可使用原生 CLI。
输入为复阻抗测量，可附加 `count.txt` 或 `--exact-n`。
数量指**规范不可约等效模型器件数**，不是物理封装数量；L+DCR 算一个器件。
不指定时原生默认搜索 1..4 个器件、最大 SP 深度 4。

Strict 枚举声明的规范串并联空间，不使用未证明安全的有限带斜率删枝。
有理拟合仅贡献可显式构造为正值无源器件的辅助候选；失败不删 SP 假设。
参数拟合是多初值局部优化，离散空间完整不等于参数全局最优。

结果按明确的 AICc/RSS 策略排序后输出 Top-K 频带行为类。
查看器件数、原始误差、局部诊断及模型族限制，不能仅凭排名推断内部物理接线。

## 原生命令示例

```sh
lcr try1 --measurements measurements.txt --max-n 4 --top-k 8 --json
lcr try1 --measurements measurements.txt --exact-n 3 --seconds 30
```

时间预算到达时可能仅返回部分候选；先检查 `enumeration_complete` 和终止状态，
再比较候选。完整构建方法在仓库 AlgorithmLcr/README.md。
