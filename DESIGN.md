# DESIGN.md — 项目架构与数据契约（权威规范）

本文件约束项目内容、项目架构、数据传输类型与各关键组分的语义。改动涉及下列任一契约时**必须同步更新本文件**。
算法侧输入输出的数学规范以 `AlgorithmLcr/INPUT_FORMAT.md` / `OUTPUT_FORMAT.md` 为权威；本文件只定义 web 侧契约并引用之。

## 1. 项目总览

ESP32 网页 LCR 阻抗分析仪。两条相互独立的数据流：

```
流 A（测量流，后端计算）:
  ESP32 固件 ──HTTP──▶ FastAPI 后端 ──正弦拟合 DSP──▶ SQLite（scan/measurement）
                                            │
                                            └──▶ 前端 时域分析/扫频/实时/历史 视图

流 B（拟合流，前端本地计算，不经后端）:
  CSV 上传 │ 示例生成 │ 历史扫描导入(读流 A 结果)     [ESP32 蓝牙导入: 规划中，稍后完成]
      └──────────────┬──────────────┘
                     ▼
              ZPoint[]（f, Re, Im）
                     ▼
   Web Worker(lcr_wasm WASM 模块) ── Try1 rlc / Try2 ng / Try3 tf（AlgorithmLcr cppversion）
                     ▼
   Top-K 候选(JSON) ─▶ 候选表 + 电路图(Schematic/GraphSchematic) + 叠加图
```

- **算法唯一来源**：`AlgorithmLcr/` 的三个 `cppversion`（C++17、零第三方依赖）。
  web 端不重新实现任何算法；Python 参考实现（`rlc_id` 等）仅作对照。
- **旧 VF 拟合**：后端 `app/dsp/fit_auto.py`（矢量拟合 + Foster 综合）及 `/api/models`、`/api/fit` 端点保留但**前端无入口**；`docs/algorithms.md` 是其历史文档。

## 2. WASM 计算层（`frontend/wasm/`）

| 文件 | 职责 |
|---|---|
| `CMakeLists.txt` | 以 `file(GLOB <Try>/cppversion/src/*.cpp)` 直接编译三个引擎为静态库（子项目有重名工具目标，不能用 add_subdirectory），链接 glue 产出 `lcr_wasm.js/.wasm`（Emscripten：MODULARIZE + EXPORT_ES6 + ENVIRONMENT=web,worker + 异常开启）。同时支持宿主 g++ 构建 `glue_test` |
| `src/common_glue.hpp` | JSON writer（`%.17g`、非有限 → `1e999` 哨兵）、测量校验、theory 频栅（测量点 ∪ [fmin/2, fmax×2] 100 对数点） |
| `src/try1_glue.cpp` / `try2_glue.cpp` / `try3_glue.cpp` | 每引擎一个 TU（三库头文件同名，必须 include 隔离），typed-array 进 / JSON 出，全异常守卫 |
| `src/version_glue.cpp` | `lcr_version` + `lcr_free` |
| `tests/glue_test.cpp` | 宿主原生测试（21 项：三引擎已知电路回收 + 错误路径） |
| `tests/smoke.mjs` | Node 烟测（与浏览器同一 C ABI） |
| `build.sh` | emsdk 定位 → emcmake/emmake → 产物拷贝到 `frontend/src/wasm/` |

### C ABI（`EXPORTED_FUNCTIONS`）

```c
char* lcr_try1(f*, re*, im*, n, exactN /*0=自由*/, maxN /*0=默认*/, topK);
char* lcr_try2(f*, re*, im*, n, kinds[R|L|C]*, values*, dcrs*, counts*, rows, topK);
char* lcr_try3(f*, re*, im*, n, us*, vs*, kinds*, m);
void  lcr_free(char*);
const char* lcr_version();
```

### 输入约束（glue 层强制）

- 测量点 `n ≥ 4`，`f > 0`，全部有限；
- Try1：`exactN ≤ 12`（UI 限 1–6）、`maxN ≤ 12`；元件参数箱由引擎定（R 1e-3–1e7 Ω / L 1e-10–10 H / C 1e-13–1e-3 F / DCR 1e-6–1e7 Ω）；
- Try2：行数 ≥ 1、value > 0、dcr ≥ 0（仅 L 行非零）、count 1..64、**总数 E ≤ 8**、必须含 L 或 C；
- Try3：`1 ≤ m ≤ 32` 边、节点标签 < 16、无自环、节点 0 与 1 必须出现在边集中。

### 响应 JSON（与 `frontend/src/lib/fitTypes.ts` 镜像，改任一侧必须同步）

```
成功: { ok:true, try:1|2|3, elapsed, stats:{...按 Try...}, candidates:[...],
        try:3 时额外 try3:{ok, jac_rank, jac_cond, n_passes, groups[], edges[], notes[]} }
失败: { ok:false, code:'bad_input'|'port_open'|'internal', error }

candidate = { rank, devices, n_params, wrmse, max_rel, aicc, rss,
              engine?(try1), sp?(try2), refined?(try2), n_members?, topology?/structure?,
              adjacency:{ v, slots:[{u, j, edges:[{t:'R'|'L'|'C', p, d}]}] },   ← OUTPUT_FORMAT.md §2
              theory:{f[], re[], im[]} }
```

**精调语义（2026-09 R4-R5，`AlgorithmLcr/OPTIMIZATION_LOG.md`）**：Try2 的用户输入
数值按"±20% 可信的标称值"处理：搜索仍穷举全部拓扑，但头部结构会做有界数值精调
（log10 空间 ±0.3 十进位，DCR 下限 1e-5Ω），精调后的候选以 `refined:true` 克隆追加，
邻接矩阵携带精调后数值。显著性守则保证：无噪精确输入时返回精确名义值；
精调改善不足 χ² 波动带（1/√(2m)）的克隆不输出；统计平局时名义值候选排在前面。
Try1/Try3 另含离群点稳健重拟合（IRLS 单遍，5σ 降权），含野点的实测数据不再被拉偏。

关键语义：**`stats` 是引擎计数（库大小/候选数等）；Try3 的诊断（群/秩/merged/dropped）在 `try3` 子对象**。器件计数：R/C=1 器件，**L+DCR 绑定 = 1 器件 2 参数**。

## 3. 前端拟合页（`frontend/src/`）

| 模块 | 职责与语义约束 |
|---|---|
| `views/FitView.vue` | 三栏目页。数据面板（四入口）→ Tabs(Try1/2/3) → 结果区（候选表 + 电路图 + 叠加图 + Try3 诊断）。每次换数据清空三引擎结果 |
| `lib/fitTypes.ts` | WASM 契约的 TS 镜像 + 错误中文映射 |
| `lib/lcrWasm.ts` | Worker 客户端：单任务串行、取消=terminate+重建；**postMessage 前对 job 做 JSON 净化（Vue proxy 不可结构化克隆）** |
| `workers/fitWorker.ts` | module worker：加载 `src/wasm/lcr_wasm.js`（locateFile 指向 `?url` 的 wasm），C ABI 桥接 |
| `lib/csv.ts` | 网站标准 CSV（每行 `f,Re,Im` 逗号分隔）解析；宽容 `#` 注释/空行/分号空白/表头/measurements.txt 首行点数；`f>0`、≥4 点、自动排序 |
| `lib/adjacency.ts` | `graphToNetlist(adj)`：上三角多重图 → SP 树规约（度-2 串联合并 → 连通分量并联合并 → 割点串联分解），**非 SP（桥式）返回 null** 走 GraphSchematic；结果扁平化 |
| `lib/schematic.ts` | 既有 SP 树布局引擎；L 叶子可带 `dcr`，标签显示 `L 值 · DCR 值`（仍是 1 个器件） |
| `components/Schematic.vue` | 精美原理图（SP 树专用）——Try1/Try2-SP/Try3-SP 的结果渲染 |
| `components/GraphSchematic.vue` | 只读图论视图（节点圆 + 边符号 + 数值）——Try2 桥式的 fallback |
| `components/GraphEditor.vue` | csacademy 风格拓扑编辑器（绘制/拖动/编辑/删除四模式，端口 0/1 锁定，重边弧线，左栏 `u v R|L|C` 边表双向同步） |
| `components/HelpBubble.vue` | 圆形 ？ 帮助钮（数量级约束速查） |
| `components/Tabs.vue` | 轻量标签条 |
| `lib/synthData.ts` | 本地示例生成（Netlist Z 求值器 + 4 个内置 DUT + 可调噪声） |
| `lib/docToc.ts` + `docs/` + `views/DocsView.vue` | 使用文档（左文档列表 / 中正文 / 右 TOC，双栏 sticky + scrollspy）；文档源为 `docs/*.md?raw` |
| `lib/graphLayout.ts` | 图形布局共享：环形布局（端口 0/1 左右、内部节点上弧）+ 重边弧形展开 |

### 数据入口语义

1. **CSV 上传**：见 §3 csv.ts；预览统计 + Bode/Nyquist 小图 + 前 5 行；
2. **示例生成**：4 个内置电路，噪声 0–2%；
3. **历史扫描导入**：`api.getScan(id)` → measurements 的 `frequency/z_real/z_imag` → ZPoint[]（流 A → 流 B 的桥）；
4. **ESP32 蓝牙导入**：🚧 规划中（按钮禁用占位 + 文档标注）。落地时：Web Bluetooth 接收 CSV 片段 → 走同一 ZPoint 管线，`docs/esp32.md` 与 `docs/api_contract.md` 同步补协议。

## 4. 模型选择与指标语义（全站统一）

- **wRMSE**：加权（1/|Z|）相对均方根误差；**maxRel**：最大相对误差；**AICc**：小样本赤池量，跨候选可比，ΔAICc < 2 并列；
- **等价类**：电气等价（Z 频带内一致）的候选拍类，`n_members` = 类大小；
- **Try3 诊断**：`weak`（弹性<0.1 弱参数）、`at_bound`（触物理箱边界）、`jac_rank < n_params`（结构性秩亏，只给族代表元）、merged/dropped（减支 F1–F4 的注释行）。

## 5. 验证基线（改动后必须保持）

| 层 | 命令 | 内容 |
|---|---|---|
| glue 原生 | `cd frontend/wasm && cmake -S . -B build-native && cmake --build build-native --target glue_test && ./build-native/glue_test` | 21 项（三引擎回收 + 错误路径） |
| glue Node | `node frontend/wasm/tests/smoke.mjs` | 同 ABI 烟测 |
| 前端单测 | `cd frontend && pnpm test` | csv / adjacency(SP+桥式) / docToc / synthData |
| 类型+构建 | `cd frontend && pnpm build` | vue-tsc + vite build |
| E2E | `conda run -n lcr python /tmp/e2e_fit.py`（脚本随仓库演进迁移） | 三引擎全跑通 + 文档页 |

## 6. 已知决策记录

- 旧 `/fit` VF 页面被三引擎页**完全替换**（后端 API 保留无入口）——2026-09 用户确认；
- WASM 产物 `frontend/src/wasm/lcr_wasm.{js,wasm}` **提交入 git**（克隆即可运行；改 `AlgorithmLcr` 后重跑 `frontend/wasm/build.sh` 并提交产物）；
- emsdk 安装于 `~/emsdk`（项目外，一次性；`build.sh` 自动 source）；
- Try3 单拓扑单结果（不做多拓扑排名）——2026-09 用户确认；
- 拟合流不落库：测量 CSV 与结果均为页面态，不写 SQLite（单机实验工具定位）。
