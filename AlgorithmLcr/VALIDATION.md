# v4 原生验收记录

2026-09-08，dev 工作区；Linux x86_64，GCC 15.2.0，C++17，Eigen 3.4.0。
这里记录实际运行结果，不把测试覆盖率或有限样本恢复率解释为普适数学证明。

## 构建与正确性检查

```sh
cmake -S AlgorithmLcr -B /tmp/lcr-v4-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/lcr-v4-build -j2
ctest --test-dir /tmp/lcr-v4-build --output-on-failure
```

两个 CTest 项目 `core`、`real4` 通过。`lcr_tests` 含 995 项运行时检查，
使用独立 require，不依赖 Release 下会被删除的 assert。

覆盖文件行号/字段校验、17 位往返、打印矩阵、从矩阵重建图；原子电路与平衡桥；
100 个随机小图的独立 long-double 求解及五点差分导数；完整割点死区、串并联归约、
一般并联 L 保留、零 DCR、开路、精确/近反谐振、标量导纳相消诊断。

枚举验证对 E=1..5 比较完整规范签名集合；独立 oracle 通过简单端口路径判定活动性、
全标签排列确定规范签名。额外对三个不同类型/数值元件比较完整带颜色赋值集合。
这为小规模枚举提供证据，不是 E=8 性能承诺，也不代替一般完备性推导。

拟合检查覆盖已知 RC 与零 DCR 恢复、Try1 exactN、内部 Try2.5、Try2 显式容差、
相关 GLS 的已知二次型目标、无效协方差、AICc 无效域、robust 与原始误差同时报告、
局部秩和近似区间、候选/LM 时间预算。辅助路径验证 RC 与并联谐振器的物理综合。
原生 CLI 另通过 JSON 解析、候选边数守恒、错误输入和预算退出码检查。

内存/未定义行为检查命令：

```sh
cmake -S AlgorithmLcr -B /tmp/lcr-v4-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g0 -O1'
cmake --build /tmp/lcr-v4-sanitize --target lcr_tests -j1
ASAN_OPTIONS=detect_leaks=1 /tmp/lcr-v4-sanitize/lcr_tests
```

AddressSanitizer、UndefinedBehaviorSanitizer 和泄漏检查通过。

## 四组实测

```sh
/tmp/lcr-v4-build/lcr_bench real4 examples
```

基准使用 20 次启动、200 次迭代、seed=1；Try1 maxN=4，Top-K=8。
下表均为测量频点、原始相对权重下的 rank-1 wRMSE，单位 %。

| 数据 | Try1 | Try2 Exact（标称值） | Try2 Tolerance | Try3 |
|---|---:|---:|---:|---:|
| data1 | 0.5255 | 2.3355 | 0.9229 | 0.9229 |
| data2 | 0.9005 | 5.6132 | 1.8951 | 1.8951 |
| data3 | 1.4683 | 6.3641 | 2.1167 | 2.1167 |
| data4 | 0.5040 | 5.2584 | 0.5040 | 0.5040 |

Try3/Tolerance 门槛分别为 1.0%、2.1%、2.3%、0.6%；Try1 门槛为各值乘 1.5。
这组门验证拟合误差，不要求 Try1 的 rank-1 物理拓扑唯一或必定等于已知电路。
Try1 在前三组找到更低残差的较复杂候选，不能据此认定真实器件更多。

Tolerance 验收显式使用 ±50% 宽参数箱，以覆盖 data4 的强相关参数；
这不是所有实际元件具有 ±50% 容差的声明，也不是推荐的实验先验。
Exact 使用文件中的标称值，不能要求它达到自由参数拟合的最小误差。
先验文件位于 `examples/v4/`，可逐组用 CLI 重放。

## 固定种子随机压力集

```sh
/tmp/lcr-v4-build/lcr_bench random 40 21
# 独立重放唯一未通过 pass@8 门槛的案例
/tmp/lcr-v4-build/lcr_bench case 7 21
```

实际验收将同一生成器的 case 0..39 分配到八个独立进程运行，每例输入、种子和配置
与上述串行命令相同。并行总墙钟约 111 秒；当时另有编译和实测进程运行，因此
不把此耗时作为性能保证。每例的拟合随机种子为 21+case。

生成器使用 2..4 器件规范 SP、对数随机参数、含零 DCR；频带 10..100000 Hz，
交替 20/60 点。复高斯噪声标准差为 0/0.1/0.2/0.3%，平滑乘性误差幅度为
0/0.3/0.6%；每第七例增加一个 50% 乘性离群点并显式开启 robust。
参数拟合为 12 次启动、200 次迭代，Try1 自由搜索到 4 个器件。

行为门比较同一采样网格上的无污染真实响应，wRMSE 门槛为
`max(0.003, 4*(noise+smooth))`。它不是全连续频带或频带外误差保证。

| 指标 | 成绩 |
|---|---:|
| 行为 pass@1 | 37/40（92.5%） |
| 行为 pass@8 | 39/40（97.5%） |
| 类型图结构匹配 @1 | 10/40（25%） |

case 21、35 的正确行为位于 Top-8 而非第一名；case 7 未通过行为门。
case 7 的原电路为 L+DCR 串联 (R∥C)，20 个频点且含离群点。
返回候选对污染数据的残差约 0.55%，但对无污染响应误差约 11.2%，超过 2.4% 门槛；
复杂候选吸收了离群点，伴随边界/病态或未收敛诊断。
因此当前鲁棒精调与 AICc/RSS 排序**不能保证排除由离群点诱导的伪结构**。
需保留诊断、Top-K 和复测频点，不应宣称已解决所有噪声/系统误差下的拓扑恢复。

结构匹配率与行为匹配率分别记录。严格图同构不合并所有电学等价实现，且部分
生成模型在有限频带内弱可辨识；该结构指标不等价于唯一物理拓扑恢复率。

## 网站与测量回归

- 前端：25 项 Vitest 测试通过，vue-tsc 与 Vite 生产构建通过。
- 后端：9 项 pytest 测试通过，包括启动、所有模拟器前向模型、上传、阻抗计算与扫描读取。
- 数据库：预置旧格式 fitresults 表和记录，启动并测量后内容保留；未执行删表迁移。
- 旧拟合 API 返回 404；网页执行按钮禁用，加载链路不引用已删除的旧 WASM。
- 旧算法 Python/C++、glue、WASM、旧基准及专属后端拟合依赖已清理。

仍有非阻断的工具提示：Vite 图表包体积提示、Starlette TestClient 的依赖弃用提示。
网页 v4 WASM 接入、真实 OSL 校准和硬件验证不属于本次已完成验收。
