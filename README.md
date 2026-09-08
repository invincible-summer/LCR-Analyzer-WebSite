# ESP32 LCR 测量与辨识

中文网页测量仪表与原生 C++ RLC 辨识算法。当前 `dev` 分支开发 **v4.1 算法层**
（原生报告 rev 2：显式参数描述符、分层模型选择、归约域传播）。

- **测量**：ESP32 上传 V/I 波形，FastAPI 计算阻抗并存入 SQLite；网页保留时域、
  扫频、实时和实验历史功能。模拟器可在没有硬件时生成数据。
- **辨识**：`AlgorithmLcr/` 使用 C++17 / Eigen 实现 Try1、Try2、Try3，保留原输入输出
  结构。旧 Python 拟合和旧 WASM 已删除，网页通过 WASM 在浏览器本地运行三个引擎，原生 CLI 仍可独立使用。归约参数按表达式传播有效域（聚合等效值可超出单器件箱）；
  候选分 primary/diagnostic-only 两层，校准 ΔAICc 仅对合格 AICc 运行显示。
- 固件目录 `ino/` 保留。Web Bluetooth 测量文件导入仍为规划功能。

## 构建 v4 算法

```sh
cmake -S AlgorithmLcr -B /tmp/lcr-v4-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/lcr-v4-build -j2
ctest --test-dir /tmp/lcr-v4-build --output-on-failure
/tmp/lcr-v4-build/lcr try1 --csv examples/data1.csv --max-n 4 --json
```

前端 wasm 门禁（frontend 下）：`pnpm build:wasm`（Emscripten 6.0.9）、
`pnpm test:wasm`、`pnpm test:parity`（native↔WASM 对照）。
CI（`.github/workflows/ci.yml`）覆盖 native/sanitizer/wasm+frontend/backend/browser。

详见 [算法说明](AlgorithmLcr/README.md)、[理论规范](AlgorithmLcr/LCRTheory_rendered.md)、
[输入格式](AlgorithmLcr/INPUT_FORMAT.md)、[输出格式](AlgorithmLcr/OUTPUT_FORMAT.md)。
Try1 的精确数量是规范等效模型数量，不是物理封装数量；L+DCR 绑定算一个器件。

## 启动测量网站

后端使用 conda 环境 `lcr`（Python 3.11），前端使用 pnpm。

```sh
conda run -n lcr pip install -r backend/requirements.txt
cd frontend
pnpm install
cd ..
./start.sh
```

`./start.sh` 自动选择端口并配置前端代理；`./start.sh stop` 停止服务。
模拟上传（将 URL 端口替换为启动器实际打印的后端端口）：

```sh
cd backend
conda run -n lcr python -m app.services.simulator --url http://localhost:8001 --model series_RLC --R 50 --L 1e-3 --C 1e-6 --f-points 30
```

保留测量 Python；不再有后端等效电路拟合 API。既有数据库不执行删除拟合表迁移。
[架构与 WASM 接口](DESIGN.md) · [ESP32 上传协议](docs/api_contract.md) · [固件](ino/README.md)
