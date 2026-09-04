# 统一输出格式规范：上三角邻接矩阵 + `vector<Edge>`

本文件是 AlgorithmLcr 三个课题（Try1 / Try2 / Try3）**搜索/拟合结果输出的唯一权威规范**。
各 Try 的实现模块（`rlc_id/adjacency.py`、`netgraph_id/adjacency.py`、`topofit_id/adjacency.py`）
实现本规范并各配测试对齐；各 DESIGN.md 只引用本文件、不重复定义。输入侧的统一
格式（测量数据、Try2 元件队列、Try3 拓扑矩阵+边类型队列）见
[INPUT_FORMAT.md](INPUT_FORMAT.md)。未来 C++ 移植（ESP32 固件 / `cppversion`）
以本文件为契约。

## 1. Edge 结构

每条边是一个三元组（Python dataclass 与 C++ `struct Edge` 一一对应）：

```python
@dataclass(frozen=True)
class Edge:
    type: str          # "R" | "L" | "C"
    parameter: float   # SI 物理值: R[Ω] / L[H] / C[F]
    dcr: float = 0.0   # 电感串联直流电阻[Ω]，仅 type=="L" 有效，其余恒 0.0
```

```cpp
struct Edge {
    enum Type { R, L, C } type;      // 或 uint8_t，三态枚举
    double parameter;                // R[Ω] / L[H] / C[F]
    double parameterOfCapacitanceDCResistance;  // 电感串联 DCR[Ω]，仅 L 有效
};
```

> 字段名 `parameterOfCapacitanceDCResistance` 沿用 C++ 侧命名；其语义是
> **电感的串联直流电阻（DCR）**，与电容无关（历史上三个 Try 的电感模型均为
> 理想 L 串联 Rd，Python 侧统一用短名 `dcr`）。

## 2. 矩阵形式

- 图模型：无向、无自环、**允许重边**（同节点对多条边）的多重图。
- 存储：**严格上三角** `rows[i][j]`（i < j）= 该节点对之间所有直接边的
  `list[Edge]`（即 `vector<Edge>`）；行 i 长度 V−1−i。
- C++ 对应：`std::vector<std::vector<std::vector<Edge>>> upper(V);`
  `upper[i].resize(V - 1 - i);`
- Python 侧访问器统一为 `adj.rows[i][j - i - 1]`（行内存上三角偏移），
  或直接 `adj.slot(i, j)`；构造函数保证 `i < j`。

## 3. 节点约定

- **节点 0、1 = 单端口的两个端点**（驱动点阻抗 Z 在 0–1 之间定义）。
- 节点标签**保留各 Try 的原始编号**，允许空洞节点（例如 Try3 的 `nested_red`
  使用节点 0/1/4/5 ⟹ V=6，节点 2/3 为空 vector）。
- V = max(所有出现过的节点标签) + 1。

## 4. 统一打印格式

demo 在现有报告之后追加矩阵块；`adjacency[rank]` 的 rank 与候选表排名一致：

```
adjacency[1] V=4 (ports 0,1):
  (0,1): R 1.000e+03 | C 1.000e-07
  (0,2): L 9.980e-04 dcr 4.900e-01
  (2,3): R 1.000e+02
```

- 每行一个非空槽位 `(i,j):`，后接该槽所有边，`|` 分隔。
- 边格式：`R {parameter:.3e}` / `C {parameter:.3e}` /
  `L {parameter:.3e} dcr {dcr:.3e}`（`dcr == 0` 时省略 dcr 段）。
- 槽位顺序 = 行主序上三角（(0,1),(0,2),…,(0,V−1),(1,2),…），同槽内边顺序
  见 §5 各 Try 的确定性规则。
- Try1/Try2 对 top-k **等价类代表元**各打一块；Try3 每个 DUT 一块，
  并附 merged/dropped 注释行（来自 `EdgeReport`）。

## 5. 各 Try 到本格式的映射

| | 源结构 | 映射 | 边值来源 | dcr |
|---|---|---|---|---|
| Try1 | `Candidate.tree`（SER/PAR 规范树）+ `theta` | §5.1 树→图展开 | `10**theta`（规范参数序） | L 器件第 2 个参数（拟合 DCR） |
| Try2 | `Network(structure, assign)` + `ComponentSet` | §5.2 槽位展开 | `Component.value` | `Component.dcr` |
| Try3 | `FitResult.groups`（`GroupReport`） | §5.3 群放置 | 聚合物理值元组 | L 群 `value[2]` |

### 5.1 Try1：串并联树 → 二端图展开

递归 emitter，从端子对 (0, 1) 出发，内部节点计数器从 2 起：

- `Leaf("R")`/`Leaf("C")` 在当前端子对 (a, b) 放一条 `Edge(kind, v, 0)`，
  消耗 `theta` 的 1 个参数；
- `Leaf("L")` 是**实电感器件**（L 与串联 DCR 绑定，一个器件两个参数），
  消耗 `theta` 的 2 个参数 [log10 L, log10 Rd]，放出
  `Edge("L", L, Rd)`；
- `SER(c1..ck)` 在 (a, b) 间新分配 k−1 个内部节点，沿链**从端口 0 侧起**
  依次连接：`emit(c1, a, n1), emit(c2, n1, n2), …, emit(ck, n_{k-1}, b)`；
- `PAR(c1..ck)` 的每个子树在同一 (a, b) 上递归 ⟹ 重边落进同一 vector。

确定性：子女按树内规范序（`make_node` 已按 canonical 串排序）遍历，节点编号
按 emitter 分配序；同一棵规范树永远得到逐位相同的矩阵（C++ 移植锁定此规则）。
规范规则（v2）：R2' 保证同槽同型 R/C 重边已合并，但**同槽多条 L 边合法**
（两个 (L+DCR) 并联是二阶系统，不可合并为单电感）；R4 保证 SER 节点不会
同时含 R 叶与 L 叶（串联 R 折入 L 的 DCR，算一个器件）。

### 5.2 Try2：多重图槽位展开

`Structure.mult` 即上三角行主序扁平矩阵（`slot_list(V) = [(0,1),(0,2),…,
(V−2,V−1)]`），边实例序 `slot_of_instances()` 槽内连续。对每个实例 t：
`(i,j) = slot_list(V)[soi[t]]`，`comp = compset.components[assign[t]]`，
`rows[i][j].append(Edge(comp.kind, comp.value, comp.dcr))`。
槽内边顺序 = 实例序（确定性）。零信息损失。

### 5.3 Try3：减支群放置

拟合在拓扑减支（F1–F4）后的**群**上进行，只有群聚合值可辨识。矩阵呈现：

- 每个 `GroupReport` 在其原始节点标签 `rows[min(u,v)][max(u,v)]` 放一条
  `Edge(kind, value[1], value[2] if L else 0)`；
- par-merged 群 = 该槽一条**聚合边**（成员单独值不可辨识）；
- ser-merged 群 = 跨外端点一条边，被合并掉的中间节点保留为空（孤立节点）；
- F4 吸收的 R 已折入相邻 L 群的 rd；F1 删边不进矩阵；
- `edges_out`（`EdgeReport`）生成 merged/dropped 注释行附在矩阵块后。

## 6. 验证要求（各 Try 测试必须包含）

1. **形状**：行 i 长度 V−1−i、槽位 (i<j)、端口 0/1 存在；
2. **守恒**：矩阵总边数 = 源结构边数（Try3 为群数）；
3. **Z 交叉验证**：从矩阵重建边表，用独立求值器（Try1 测试内联节点导纳
   矩阵；Try3 复用 `NodalModel`）计算 Z(f)，与原引擎输出 allclose——
   证明转换是电学等价的，而不仅是结构平移。
