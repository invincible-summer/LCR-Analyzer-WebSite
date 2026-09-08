# v4 统一输入格式规范

本文件与 OUTPUT_FORMAT.md 是 Try1 / Try2 / Try3 的输入输出契约。
实现：C++17 `include/lcr/lcr.hpp` 与 `src/io.cpp`；不再维护 Python 版本。

## 通用规则

UTF-8，换行 `\n`；字段以空格或 Tab 分隔。行内 `#` 起至行尾为注释，空行忽略。
数值允许科学计数法，必须有限；写出浮点用 `%.17g`（17 位有效数字）。
校验失败抛 `std::invalid_argument`，文件解析错误带实际行号。
整数不接受小数/指数记法；解析资源上限 1,000,000，拓扑节点上限 256。
加载器只做格式校验，连通性和死区由引擎检查。

## 测量文件（三种 Try 共用）

```text
n
f_1 Re_1 Im_1
...
f_n Re_n Im_n
```

`n≥1`，之后恰好 n 行、每行恰好三个数。频率 f[Hz] > 0；Re/Im[Ω] 有限。
对应 `Z=Re+j Im`。保留输入顺序和重复频点，不隐式合并测量。
文本格式不含协方差，默认使用带近零保护的相对权重。
C++ `Config::covariance` 可提供逐点 2×2 正定协方差，不改变文本文件。

`--csv` 是独立便利入口，接受无表头的 `f,Re,Im` 与注释；
不能与 `--measurements` 同时使用，不改变严格测量文件规则。

## Try1：可选 count.txt

去注释与空行后恰好一行，一个正整数 N。

```text
3
```

对应 `Config::exactN`；不提供则搜索 `1..maxN`。
**N 为规范不可约等效模型的器件数，不是物理封装/BOM 数量。**
R/C 各算一器件，L+DCR 绑定算一器件。串联 R 可以折入 L 的 DCR；
同类可归约支路可以合并，因此单端口数据不能恢复被归约的物理器件数量。
字段名和文件形状保持兼容，v4 修正了此前不严谨的数量说明。

```sh
lcr try1 --measurements measurements.txt --count count.txt
lcr try1 --measurements measurements.txt --exact-n 3
```

`--count` 与 `--exact-n` 互斥。原生支持 1..12 的声明数量，默认 maxN=4、
maxDepth=4；大空间成本可能很高，应显式设置预算。Strict 不默默缩小空间。

## Try2：components.txt

每行一器件，至少一行；行序无关，重复行表示多个可互换器件。

```text
R 1e3
C 1e-7
L 1e-3 5
```

字段：`type parameter [dcr]`。type 为 R/L/C；parameter 为 SI 值，>0。
R/C 行恰好两字段，L 行两或三字段；DCR[Ω] ≥0，缺省为零。
规范序按 `(type, parameter, dcr)`，其中 C < L < R。

默认 Exact：全部输入值固定，不自动精调。`--tolerance t` 显式启用
Tolerance，`0<t<1`，参数位于标称值乘 `[1-t,1+t]` 与物理边界的交集。
DCR 可另加 `--dcr-tolerance ΔΩ`；标称零 DCR 且 Δ=0 时保持零。
绝对 DCR 容差必须与正的 fractional tolerance 一起开启；Exact 不接受 robust 精调选项。
容差不是输入文件里的隐式误差字段。总器件数 1..8，允许纯电阻。

## Try3：topology.txt

```text
V
<第 0 行：V-1 个非负整数>
...
<第 V-2 行：1 个非负整数>
T_1
...
T_E
```

V≥2；端口固定为 0/1。矩阵只存严格上三角边数。
第 i 行按 `(i,i+1)..(i,V-1)` 排列；E 为所有边数之和。
类型队列恰好 E 行，每行 R/L/C 一个字符；按槽位行主序逐边消费。
允许重边，无自环。类型已知、参数未知；孤立节点和死枝交给引擎处理。

```text
3
0 1
2
L
C
R
```

表示 `(0,2,L), (1,2,C), (1,2,R)`。
输出保留输入节点编号及归约后的空洞节点。

## 扩展配置

统一 C++ Config / CLI 提供 Strict/Fast、Top-K、启动数、迭代数、随机种子、
候选预算、时间预算、robust 和物理参数边界。默认值与边界以公开头文件为准。
Try2.5 仅是内部 C++ 组合接口：已知类型/数量，未知参数与接线；不新增文件格式。
