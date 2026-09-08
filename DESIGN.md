# 项目架构与数据契约 — v4 dev

## 当前状态

v4 重点是原生 C++ 算法重写。测量网站和 ESP32 固件保留，旧 Python 拟合与旧 WASM
已移除。网页通过 Worker/WASM 调用共享 v4 核心，在浏览器本地完成拟合。
算法数学规范：`AlgorithmLcr/LCRTheory_rendered.md`。
文本输入输出规范：`AlgorithmLcr/INPUT_FORMAT.md`、`OUTPUT_FORMAT.md`。

## 两条独立数据流

1. **测量流**：ESP32 HTTP 原始 V/I 波形 → FastAPI 正弦拟合 DSP → SQLite。
   时域、扫频、实时与实验历史继续读取测量结果。协议见 `docs/api_contract.md`。
   模拟器的前向 DUT 公式独立于算法库，不包含拟合功能。
2. **辨识流**：原生测量文本/CSV + Try 先验 → C++17/Eigen 公共核心 → Top-K 图与诊断。
   CLI 与网页 CSV/示例/历史扫描 → Worker → WASM 均可运行。
   辨识结果不写 SQLite。Web Bluetooth 测量导入仍是规划项。

旧 `/api/models`、`/api/fit`、`/api/fit/{id}`、SPICE 导出和扫描旧拟合列表已删除。
扫频页面不再请求旧拟合数据。历史 fitresults 表不主动删除、不迁移、不再由 ORM 管理；
测量表和数据保留。测量上传、扫描详情、导出、删除、WebSocket 契约不变。

## 原生算法

`AlgorithmLcr` 是独立 CMake 项目，公开库 `lcr_core` 和 CLI `lcr`。
Eigen 3.4.0 vendored 头文件及许可证固定入库，构建不需联网。
模块为 io、graph、nodal、fit、search、rational、report，共享一套数学实现。
Try2 Exact/Tolerance、Try3、内部 Try2.5、Try1 有界 SP 与 Foster 辅助路径。
默认 Strict；预算耗尽明确标记未完成。仅完成且数值可靠的 Try2 Exact 支持有限候选空间
的条件最优声明，其他连续拟合始终是局部方法。

L+DCR 算一个器件，最多两个自由参数。exactN 表示规范不可约等效模型器件数。
原生 JSON schema 为 `lcr.native.v4` revision 2（engine_version 4.1.0），
由网页适配器转换为前端展示协议：参数诊断用显式 `parameters[]` 描述符
（id/edge/quantity/free/fixed/SE/CI），选择状态用运行级与候选级
`selection`（primary/diagnostic 分层，校准 ΔAICc 仅限合格 AICc 运行），
归约群携带传播有效域 `value_bounds/dcr_bounds`、`parameter_ids` 与表达式树。
非有限诊断/无效 AICc 为 null；绘图曲线在原始频点直接求值。

## 网站接入

`AlgorithmLcr/wasm/bindings.cpp` 提供 C ABI；Emscripten 将相同 `lcr_core` 编译为
`frontend/src/wasm/lcr.js` 与 `lcr.wasm`。执行 `pnpm build:wasm`（frontend 下）重建，
需要 Emscripten（当前固定 6.0.9）。普通网站启动和打包直接使用这些产物，不要求安装 SDK；
C++ 源或绑定改动后必须重建并提交三个产物（`lcr.js/lcr.wasm/lcr.d.ts`），
`pnpm test:wasm` 与 `pnpm test:parity`（native CLI ↔ WASM 对照）作为回归门。
C ABI 保留 `lcr_try1/2/3` 的 typed-array 参数和 `lcr_free/lcr_version`：
`lcr_try1(...,exactN,maxN,maxDepth,topK)`（maxDepth 独立于 maxN，默认 4）；
配置 setter 为 `lcr_configure(fast,budget,seconds,tolerance,dcrTolerance,robust)`
（每次任务重置）、`lcr_configure_search(fast,budget,seconds,starts,iterations,
seed,equivalenceTolerance)`、`lcr_configure_bounds(...)` 与
`lcr_set_covariance(rr,ri,ii,n)`（all-or-none，null 清除回相对加权）。
输入缓冲区由调用方释放，JSON 响应由 `lcr_free` 释放；C++ 异常不越过边界。

页面 → 独立 Worker（断言 wasm revision 2）→ WASM → 原生 JSON rev 2 →
TypeScript 适配（显式 parameter_ids/描述符）→ 候选表、电路图、曲线。
任务结束释放缓冲区并销毁 Worker；取消直接终止 Worker，Promise 明确失败。页面卸载
也取消计算，同一页面同时仅允许一个任务。不会上传测量数据来进行辨识，不写数据库。
噪声模型三态显示：相对加权回退 / 逐点协方差（6 列 CSV 或仪器）/ 扫描极坐标
不确定度近似（显式 opt-in，J·diag(σρ²,σφ²)·Jᵀ，任一点不满足整组回退）。

网页测量要求 4..100000 个点，Try1 数量 1..12（默认 maxN=4，maxDepth 独立输入）、
Try2 行数与总数 1..8（允许纯 R），Try3 边数 1..32、节点标签 0..15。Try2 默认
Exact；显式容差后才优化参数，允许独立 DCR 绝对容差，robust 仅限容差模式。
Strict 默认无限预算，Fast 默认 1000 个候选；用户可设候选预算、时间预算与
高级 starts/iterations/seed/等价容差。预算用尽保留部分结果，取消则舍弃
正在运行的任务结果。Try3 为多起点局部优化（无离散枚举语义）。

`fitTypes.ts` 保留候选 adjacency/theory 形状；`fitAdapter.ts` 转换报告与 Try3
群诊断，群参数状态按 `parameter_ids` + `diagnostics.parameters` 对齐，不做
连续索引推断。原生 groups 显式携带对应拟合边的端点、类型、数值、有效域与
表达式摘要。AICc、条件数允许 null 并显示不可用；ΔAICc 仅在选择准则为合格
AICc 时展示，diagnostic-only 候选明确标记。曲线使用原始测量频点直接计算，
不向未测频段外推。页面区分枚举完成、部分搜索结果与有限候选空间最优证书，
连续拟合不宣称物理结构唯一。空候选显示提示，不绘制空电路。

## 验证

CI（`.github/workflows/ci.yml`）覆盖 native（ctest+real4）、ASan/UBSan、
WASM 重建一致性 + smoke + parity + vitest + build、后端 pytest 与浏览器
smoke。本地快速命令：

```sh
cmake -S AlgorithmLcr -B /tmp/lcr-v4-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/lcr-v4-build -j2
ctest --test-dir /tmp/lcr-v4-build --output-on-failure
/tmp/lcr-v4-build/lcr_bench random 40 21
```

算法层验证数学、浮点、枚举和实测行为，详见 AlgorithmLcr/VALIDATION.md。
测量后端 `conda run -n lcr python -m pytest`（在 backend 下）；前端 `pnpm test`、
`pnpm test:wasm`、`pnpm test:parity`、`pnpm build`（在 frontend 下）。
UI 保持中文浅色科学仪表风格，不重做网站架构。
