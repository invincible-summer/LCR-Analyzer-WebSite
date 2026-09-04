# 统一输入格式规范：测量数据 + 各 Try 的先验输入

本文件是 AlgorithmLcr 三个课题（Try1 / Try2 / Try3）**输入数据的唯一权威规范**，
与输出侧规范 [OUTPUT_FORMAT.md](OUTPUT_FORMAT.md) 配套：相同的 `Edge`
结构体（type / parameter / dcr）既用于输出结果，也用于 Try2 的元件输入；
相同的上三角邻接矩阵既用于输出结果，也用于 Try3 的拓扑输入（只是不带参数）。
各 Try 的实现模块（`rlc_id/iofmt.py`、`netgraph_id/iofmt.py`、
`topofit_id/iofmt.py`）实现本规范并各配测试对齐；各 DESIGN.md 只引用本文件。
未来 C++ 移植（ESP32 固件 / `cppversion`）以本文件为契约。

## 0. 文件通用规则

- 编码 UTF-8，行结尾 `\n`。
- 字段以空白（空格/Tab）分隔；行内 `#` 起至行尾为注释，整行空行忽略。
- 数值为十进制 ASCII（允许科学计数法），由 `float()` / `int()` 解析。
- 写出方（dump）浮点数一律用 `%.17g`，保证 读回 == 写出（逐位一致）。
- 建议文件名：`measurements.txt`（三种通用）、`count.txt`（Try1，可选）、
  `components.txt`（Try2）、`topology.txt`（Try3）。

## 1. 测量数据格式（三种完全相同）

第一行为采样点数 `n`（正整数），之后**恰好** n 行，每行 3 个数：

```
n
f_1  Rz_1  Iz_1
f_2  Rz_2  Iz_2
...
f_n  Rz_n  Iz_n
```

| 字段 | 含义 | 约束 |
|---|---|---|
| `f` | 采样频率 [Hz] | > 0，有限 |
| `Rz` | 阻抗实部（电阻分量）[Ω] | 有限 |
| `Iz` | 阻抗虚部（电抗分量）[Ω] | 有限 |

即第 i 个采样点 `Z_i = Rz_i + j·Iz_i`，`j` 为虚数单位。加载后得到
`(f: np.ndarray[float], z: np.ndarray[complex])`。

完整示例（2 个点）：

```
# measurements.txt
2
1.0e+03  9.98e+02  -1.2e-01
1.0e+04  6.13e+02  -4.88e+02
```

## 2. 各 Try 的完整输入格式

三个 Try 都以 §1 的测量数据为主输入，先验知识逐级增加：

| Try | 先验 | 输入文件 |
|---|---|---|
| Try1 | 元件类型/参数/拓扑全未知；可选：器件总数恰为 n | `measurements.txt` + 可选 `count.txt` |
| Try2 | 元件多重集已知（类型+参数），拓扑未知 | `measurements.txt` + `components.txt` |
| Try3 | 拓扑与每条边的元件类型已知，参数未知 | `measurements.txt` + `topology.txt` |

### 2.1 Try1：测量数据 + 可选器件数约束（count.txt）

```
measurements.txt := <测量数据，§1 格式>
count.txt        := <单个正整数 n>        # 可选
```

`count.txt` 是可选的先验约束文件：去掉注释与空行后**恰好一行**，内容为一个
正整数 `n`，含义是"被测电路的器件总数恰好为 n"。器件计数规则与
[OUTPUT_FORMAT.md](OUTPUT_FORMAT.md) 的边一致：R、C 各算 1 个器件；
**一个电感（L 与其串联直流电阻 DCR 绑定）算 1 个器件**（实验室电感的绕线
电阻不可忽略，模型上 L 与 DCR 串联绑定、一起拟合）。

提供该文件时，拓扑搜索被限制在"恰好 n 器件"的规范树层（引擎 B 中器件数
不符的候选也不参与排序）；缺省（不提供）= 自由搜索 1..max_n 器件。

完整示例（电路恰有 3 个器件）：

```
# count.txt
3
```

进入管线：`identify(f, z, config=Config(exact_n=n))`
（`rlc_id/iofmt.py::load_count`；CLI 侧为 `demo.py --count count.txt`
或 `--exact-n 3`）。

### 2.2 Try2：测量数据 + 元件队列（Edge 结构体，不带拓扑）

`components.txt` 每行一个元件，即 OUTPUT_FORMAT.md §1 的 `Edge` 结构体
的字段平铺（**无节点信息**——接线正是要搜索的对象）：

```
type  parameter  [dcr]
```

| 字段 | 含义 | 约束 |
|---|---|---|
| `type` | 元件类型 | `R` \| `L` \| `C` 之一 |
| `parameter` | SI 物理值 | R[Ω] / L[H] / C[F]，> 0 |
| `dcr` | 电感串联直流电阻 [Ω]（对应 C++ 侧 `parameterOfCapacitanceDCResistance`） | 仅 `L` 行可带，≥ 0，缺省 0 |

规则：`R`/`C` 行恰好 2 个字段（不允许第三字段）；`L` 行 2 或 3 个字段。
行序无关（多重集语义，重复行 = 多个可互换元件）。至少 1 行。

完整示例（对应 `ComponentSet.make(n_R=[1e3], n_C=[100e-9],
n_L=[(1e-3, 5.0)])`）：

```
# components.txt
R 1.0e+03
C 1.0e-07
L 1.0e-03 5.0
```

进入管线：`identify(compset, f, z)`（`netgraph_id/iofmt.py::load_components`）。

### 2.3 Try3：测量数据 + 邻接矩阵（边数）+ 边类型队列（均不带参数）

`topology.txt` 编码 OUTPUT_FORMAT.md §2 的上三角邻接矩阵与边队列的
**无参数版本**：

```
V
<上三角第 0 行：V-1 个非负整数>
<上三角第 1 行：V-2 个非负整数>
...
<上三角第 V-2 行：1 个非负整数>
T_1
T_2
...
T_E
```

| 部分 | 含义 | 约束 |
|---|---|---|
| `V` | 节点数；节点 0、1 为端口端点 | ≥ 2 |
| 矩阵行 i | 第 i 行有 V−1−i 个整数，依次是槽位 `(i,i+1), (i,i+2), …, (i,V−1)` 上的**边数**（即该 `vector<Edge>` 的长度） | 非负整数；对角线/下三角不存在（无自环、无向） |
| 队列 | 共 E = 矩阵所有元素之和 行，每行一个类型 `R` \| `L` \| `C`，是**边实例的类型队列**，按槽位行主序（先 (0,1)，再 (0,2)…(0,V−1)，再 (1,2)…），同槽位内按队列先后 | 恰好 E 行 |

重建规则：沿槽位行主序走矩阵，每个槽位从队列依次取 `m(i,j)` 个类型，
生成边 `(i, j, T)`（i < j）。重建结果即 Try3 管线输入
`edges: list[(u, v, kind)]`（`topofit_id/iofmt.py::load_topology`）。

完整示例 1（`ladder`：`[(0,2,L), (2,1,C), (2,1,R)]`，V=3，E=3）：

```
# topology.txt
3
0 1
2
L
C
R
```

矩阵行 0 = 槽 (0,1),(0,2) 的边数 `0 1`；行 1 = 槽 (1,2) 的边数 `2`；
队列 `L C R` → 边 `(0,2,L), (1,2,C), (1,2,R)`。

完整示例 2（三条边全并联在端口上，含重边，V=2，E=3）：

```
2
3
R
C
L
```

→ 边 `(0,1,R), (0,1,C), (0,1,L)`。

## 3. 校验规则（加载器必须执行）

1. 测量：`n` 为正整数；数据行数恰为 n；每行 3 个可解析浮点；`f > 0`；
   三个数均有限。
2. 元件：类型 ∈ {R, L, C}；R/C 行 2 字段、L 行 2–3 字段；`parameter > 0`；
   `dcr ≥ 0`；至少 1 行。
3. 拓扑：`V ≥ 2`；第 i 矩阵行恰有 V−1−i 个非负整数；队列长度恰为 E；
   每个类型 ∈ {R, L, C}。
4. 器件数约束（Try1 count.txt）：去注释/空行后恰 1 行；可解析为正整数
   （≥ 1）。
5. 违反任何一条 → `ValueError`，消息指明第几行/哪个字段。

连通性与"死枝"检查不在加载器做（那是各引擎/减支的职责：Try2 枚举时
过滤，Try3 `reduce_graph` 删除）。

## 4. 与输出格式的关系（跨语言要点）

- Try1 的器件数约束 = 一个整数（无图结构），直接映射 C++ 侧
  `Config::exactN`（`std::optional<int>`，缺省空 = 自由搜索）；
- Try2 的元件输入 = 输出 `Edge` 的文本形式去掉节点坐标；
- Try3 的拓扑输入 = 输出邻接矩阵的文本形式，其中每槽 `vector<Edge>`
  退化为"边数"，边参数退化为队列中的类型字母；
- 测量文件与拓扑/元件/约束文件完全解耦，可任意组合重跑；
- C++ 侧对应：`struct Edge`（见 OUTPUT_FORMAT.md §1）+
  `vector<vector<int>> mult`（上三角边数）+ `vector<EdgeType> queue` +
  `vector<tuple<double, double, double>> points`（f, Rz, Iz）+
  `optional<int> exactN`（Try1 count.txt）。

## 5. 三个课题的 cppversion 实现（2026-09-04 起）

三个课题的 C++ 移植均以本文件为契约（零第三方依赖、C++17）：

| Try | C++ 模块（`cppversion/src/`） | 输入实现 | 文件模式入口 |
|---|---|---|---|
| Try1 | `rlc` 命名空间（既有） | `iofmt.{hpp,cpp}`（measurements + count） | `demo --measurements F [--count F]` |
| Try2 | `ng` 命名空间 | `iofmt.{hpp,cpp}`（measurements + components） | `demo --measurements F --components F` |
| Try3 | `tf` 命名空间 | `iofmt.{hpp,cpp}`（measurements + topology） | `demo --measurements F --topology F` |

- 三者共享 §0 通用规则与 §1 测量格式（`%.17g` 逐位回写，`#` 注释、
  空行忽略、加载器校验与报错行号语义一致）；
- Try2 的 `ComponentSet` 规范序 = 按 `('C'<'L'<'R', value, dcr)` 排序
  （与 Python 元组字符串序逐位一致）；Try3 的 `topology.txt` 重建规则
  按槽位行主序逐边消费类型队列，与 §2.3 相同；
- 一致性对拍工具位于各 `cppversion/tools/`（gen_cases / run_py /
  compare），2000 组随机案例双端加载同一批文本文件复算。
