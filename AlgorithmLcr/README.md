# LCR v4 原生算法

C++17 / Eigen 3.4.0 的单端口 RLC 辨识库。三个引擎共享节点求值、解析 Jacobian、
图归约、SVD-LM 优化和诊断。没有 Python 拟合算法或旧 cppversion；原生构建不依赖 WASM 工具链。

```sh
cmake -S AlgorithmLcr -B /tmp/lcr-v4-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/lcr-v4-build -j2
ctest --test-dir /tmp/lcr-v4-build --output-on-failure
/tmp/lcr-v4-build/lcr --help
```

Eigen 头文件随 `vendor/` 固定，离线构建可用。公开 API 在 `include/lcr/lcr.hpp`。
命令行错误退出码、机器输出及格式见 [OUTPUT_FORMAT.md](OUTPUT_FORMAT.md)。

```sh
# 输入标准测量文件；Try1 可不指定 count，自由搜索 1..4 个规范器件
/tmp/lcr-v4-build/lcr try1 --measurements measurements.txt --count count.txt --json
# 本项目实测 CSV，可直接作为便利输入
/tmp/lcr-v4-build/lcr try1 --csv examples/data1.csv --max-n 4 --json
# 精确元件集合，接线未知
/tmp/lcr-v4-build/lcr try2 --measurements measurements.txt --components components.txt
# 显式容差：值 ±10%，零 DCR 另允许增加至 1 Ω
/tmp/lcr-v4-build/lcr try2 --measurements measurements.txt --components components.txt --tolerance .1 --dcr-tolerance 1
# 图与类型已知，参数未知
/tmp/lcr-v4-build/lcr try3 --measurements measurements.txt --topology topology.txt
```

- **Try1**：有界规范 SP 枚举 + 可物理综合的有理辅助候选 + 公共局部拟合。
  exactN 是等效模型器件数，不是物理 BOM；默认 maxN=4、maxDepth=4、Top-K=8。
- **Try2**：默认 Exact，固定已知参数；显式 `--tolerance` 才开启容差局部拟合。
  支持桥式/重边、1..8 元件；大 E 的严格枚举可能昂贵。
- **Try3**：完整死区和严格归约后拟合，返回群、边界、局部可辨识性。
- **Try2.5**：内部 C++ `try25` 接口及组合测试，不增加文件格式。

默认 Strict 无隐式搜索预算。`--mode fast` 默认 1000 候选；可以显式设
`--budget`、`--seconds`。预算在候选、启动和 LM 步间协作检查，结果会标记未完成。
`--starts`、`--iterations`、`--seed` 控制多初值；`--robust` 显式开启 IRLS。
CLI 提供 R/L/C 上下界和 DCR 上界；C++ Config 还支持逐点协方差及取消回调。

原生 JSON `lcr.native.v4` 包含原精度邻接矩阵与独立诊断，不是网页 Worker 协议。
网页通过 Worker/WASM 调用相同 C++ 核心。模型选择使用声明的 AICc/RSS 规则，
有限带系统误差下较复杂模型可能排前；不能把低残差或局部满秩当作唯一物理接线证明。

验证：`lcr_tests`、`lcr_bench real4 examples`、`lcr_bench random 40 21`。
随机基准分别报告行为 pass@1/pass@8、结构匹配率与耗时。
`lcr_bench case 7 21` 可单独重放种子 21 的第 7 个案例，打印原电路、测量输入与逐候选误差。
[理论](LCRTheory_rendered.md) · [输入](INPUT_FORMAT.md) · [验收记录](VALIDATION.md)

## 浏览器构建

```sh
# frontend/ 下，Emscripten 在 PATH 或 EMSDK/$HOME/emsdk 中
pnpm build:wasm
pnpm build
```

WASM 与 CLI 共享核心。生成的 `frontend/src/wasm/lcr.js` 和 `lcr.wasm` 随源码保留，
普通网站启动不需要 SDK；修改 C++ 后须同步重建。当前验证 Emscripten 6.0.9。
