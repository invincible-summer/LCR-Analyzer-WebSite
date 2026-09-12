# ino 运行时修复与实板验收计划

状态：**实现完成，等待 CI 最终绿灯与实板验收**  
基线：`main@3ad2acc0126235d70549c8892d5994755c3aeb7b`  
修复分支：`fix/ino-runtime-20260912`  
评审入口：PR #3 `Fix ESP32-S3 TFT boot crash and harden ino runtime paths`

本文只约束 `ino/` 固件、其构建/测试门禁以及直接相关的硬件契约。`AlgorithmLcr`、网站拟合算法和已经锁定的 `DO_NOT_TOUCH_*` 测量核心不在本次修改范围。除非后续实板证据证明 DNT 自身存在缺陷，否则不得为了应用层问题修改 DNT 文件或绕过其公开 API。

## 1. 本次故障与确定根因

现场故障在打印

```text
LCR-UI v4.1.0 booting (BLE protocol 1, z-schema v2, h-schema v2)
```

之后、Worker 初始化之前立即发生：

```text
Guru Meditation Error: Core 1 panic'ed (StoreProhibited)
EXCCAUSE: 0x1d
EXCVADDR: 0x00000010
```

`LCR_UI.ino` 的启动顺序是 `Serial.begin()` -> `ui::begin()` -> `input.begin()` -> `lcrServiceBegin()`，因此该故障窗口首先落在 TFT 初始化路径。结合固定依赖源码，可得到闭合因果链：

1. 生产构建固定 Arduino-ESP32 `3.3.11` 与已发布 TFT_eSPI `2.5.43`。
2. Arduino-ESP32 3.3.11 在 ESP32-S3 上把 `FSPI` 定义为逻辑 bus index `0`；这是 Arduino HAL 的 API 编号，不是 TFT_eSPI S3 direct-register 宏所需要的外设号。
3. TFT_eSPI 2.5.43 的 ESP32-S3 默认 processor 分支使用 `SPI_PORT=FSPI`，因此默认组合得到 `SPI_PORT=0`。同一 2.5.43 源码已经提供 `USE_FSPI_PORT` 分支，在 S3 上会显式选择 `SPI_PORT=2`。
4. ESP32-S3 的用户 GP-SPI 是 SPI2/SPI3；SPI0/SPI1 属 flash/PSRAM 内部存储域。TFT 应使用用户 SPI2，而不是以 0 作为 direct-register 外设号。
5. TFT_eSPI 上游后续提交 `83d4d16451de9dfb55cd3c0242e641fd37152abc` 也把 S3 默认 `SPI_PORT` 从 `FSPI` 改成了 `2`，与本次根因一致。
6. 因此现场 `EXCVADDR=0x10` 与 `tft.init()` 的错误 direct-register host 选择一致；当前证据足以修复该路径。没有旧 ELF 文件时不把具体 PC 地址强行符号化成函数名，避免超出证据范围。

本次**不追踪未正式发布的 TFT_eSPI master/2.5.44**。Arduino Library Manager 当前可复现版本是 2.5.43，所以修复策略是继续冻结 2.5.43，使用它已经公开支持的 `USE_FSPI_PORT` 配置接口，并增加编译期断言。

## 2. 硬件与数据手册约束

以下约束是代码和构建脚本的硬条件，不得按“能编译”随意修改。

### 2.1 ESP32-S3 / N16R8

板卡原理图确认模块为 ESP32-S3-WROOM-1-N16R8。原理图 H4/H5 同时确认外接 TFT 候选线可以从排针取得：H4-4=GPIO4、H4-5=GPIO5、H4-6=GPIO6、H4-7=GPIO7，H5-18=GPIO21。因此固件继续使用：

```text
SCLK  GPIO4
MOSI  GPIO5
CS    GPIO6
DC    GPIO7
RST   GPIO21
MISO  unused
```

这些是**原理图网络映射**；外接屏线束和焊接仍需要万用表 continuity 实测，软件不能替代实体连接确认。

Espressif 官方资料约束：

- SPI2、SPI3 为用户 GP-SPI；SPI0/SPI1 用于 flash/PSRAM。
- GP-SPI 信号可以通过 GPIO matrix 路由到可用 GPIO；当前 10 MHz 远低于 Espressif 对 GPIO-matrix SPI 路由给出的 40 MHz 等效区间，因此 GPIO4/5 不需要强行改回 SPI2 IO_MUX 默认脚。
- GPIO0/3/45/46 是 strapping，应避开 UI 普通信号。
- N16R8/Octal PSRAM 条件下 GPIO35/36/37 被 PSRAM 占用；ESP32-S3 系列 OPI 映射还涉及 GPIO33/34。固件 Gate F 保守地把 GPIO33-37 全部排除，避免与存储总线争用。
- DNT 测量链已经占用 GPIO1/2、8-18，应用层 UI 不得复用。
- GPIO43/44 保留给本板 UART0/CH340 路径；GPIO19/20 属 USB 域，不纳入 TFT/UI 新映射。

### 2.2 ST7735S 4-wire serial

使用所附 ST7735S 数据手册的 4-line serial AC characteristics：write serial clock cycle `TSCYCW >= 66 ns`，对应理论 `fSCL <= 15.1515 MHz`。产品构建保持 **10 MHz**，并在 `display.cpp` 与 `static_check.sh` 双重拒绝超过 15,151,515 Hz 的配置。

当前不根据控制器数据手册猜测具体屏模组的 X/Y offset、RGB/BGR、invert 或 tab 版本。这些属性与模组玻璃/接线/初始化表有关，必须通过实屏 color-bar 和边界像素测试确定；没有实板证据时保持现状。

TFT_eSPI 的硬复位序列本身提供毫秒级低电平和复位完成等待，宽于 ST7735S 数据手册的微秒级最低复位要求，因此本次不另写第二套 reset sequence。

### 2.3 FreeRTOS / task 约束

DNT ADC 初始化保存当前 task handle，ADC ISR 后续通知该 handle。因此 `lcr_api_init()` 与所有 DNT 测量 API 必须始终位于同一个 `lcr_worker` task；禁止把初始化移回 `setup()`，也禁止在 UI task 直接调用 DNT。

ESP-IDF 的 `xTaskCreatePinnedToCore()` stack size 单位是 **bytes**，故 `kWorkerStack=16384` 表示 16 KiB。没有实板 high-water-mark 证据前不凭经验缩减该值。

## 3. 已实现修复及接口语义

### 3.1 TFT 启动崩溃

`ino/tools/build_check.sh` 的 TFT 编译参数必须包含：

```text
-DUSE_FSPI_PORT
-DTFT_SCLK=4
-DTFT_MOSI=5
-DTFT_MISO=-1
-DTFT_CS=6
-DTFT_DC=7
-DTFT_RST=21
-DSPI_FREQUENCY=10000000
```

`display.cpp` 在 ESP32-S3 构建中要求 `SPI_PORT==2`，否则编译失败；同时要求 `SPI_FREQUENCY<=15151515`。在 `tft.init()` 之前打印有效编译配置，实板启动必须看到：

```text
TFT init: TFT_eSPI 2.5.43, SPI_PORT=2, SCLK=4 MOSI=5 CS=6 DC=7 RST=21 @ 10000000 Hz
```

如果新固件仍 panic，必须保存**新构建对应的 ELF 和新 backtrace**再符号化，不能继续把旧 `EXCVADDR=0x10` 诊断机械套用到新故障。

### 3.2 DNT 出参失败路径

`DO_NOT_TOUCH_lcr_api.h` 的真实契约中，部分错误路径会在写出参之前直接返回。例如 `lcr_api_measure_z()` 在 `lcr_derive_z()` 失败时返回 `LCR_API_ERR_MEASURE`，此时不能读取调用者的 `LcrZPoint`。

wrapper 约束如下：

- 所有 DNT 临时结构使用 `{}` 初始化，但**零初始化不是读取失败出参的许可证**；只有契约保证写出的成功路径才能 copy。
- `MeasureAndCalcZ`：若 Z 测量失败，不读取 `p`，不调用 calc；`AppZPoint` 显式写 `fReq`，其余数值为 NaN、`apiType='E'`，保留真实负错误码；`AppCalcResult` 同样标记未得到结果。
- `SweepZChunk/SweepWChunk`：只有 `r>0` 或 `LCR_API_ERR_MEASURE_ALL` 才读取 DNT 数组，因为这两类返回发生在测量循环已经执行之后；参数/缓冲区等早退错误由 wrapper 自己生成失败槽位，禁止读取未写数组。
- `lcr_api_calc(..., false)` 继续保持；Z measurement 链已经完成校准，再 apply 一次会二次校准。
- 成功路径的 `f_act/z_re/z_im/h_mag/phase` 不被 wrapper 修改；网站拟合仍只使用实际 `f_act`。

### 3.3 Worker 跨 task 同步与 completion

`ready/initFailed/jobInFlight/cancelRequested` 是 loop task 与 worker task 共享的控制状态，不能用 `volatile` 充当同步原语；现改为 `std::atomic<bool>`，发布/读取使用 release/acquire 语义。

job/event 队列均保持固定长度 4。job submit 对 UI task 仍是 `0 tick` 非阻塞；但 completion event 是状态机控制面，**不得像 telemetry 一样在队列满时丢弃**。Worker 对 event queue 使用 `portMAX_DELAY`：这只阻塞专用 Worker 并让出 CPU，loop task 可以继续运行、取 event、释放队列空间。这样 `m_pendingId` 不会因为 completion 被静默丢失而永久悬空。

取消仍是 chunk-bounded：当前 DNT 同步调用完成 -> 不再开始下一个测量 chunk -> StopTone -> 收到 StopTone completion -> 解除 measurement lock。任何 invariant 异常也必须走此收尾路径，不能仅把状态置 Error 而留下激励未确认关闭。

### 3.4 时间与 seal

Arduino `millis()` 是 uint32 uptime，不是 Unix epoch。内部字段改名为 `sealedUptimeMs`，不序列化成“Unix time”。

20 ms StopTone quiet guard 使用：

```cpp
(int32_t)(nowMs - deadlineMs) >= 0
```

在 deadline 与当前时间相距小于 `2^31 ms` 的前提下正确跨越 uint32 wrap；本项目 guard 仅 20 ms，满足该前提。新增 `test_rollover.cpp` 固定覆盖 `0xfffffff5 + 20 -> 0x00000009` 的回绕边界。

校准状态读取若失败，不再把零初始化内容伪装成 `cal:0/10`；状态机转入 cancel/StopTone，不封存带伪造 calibration state 的数据集。

## 4. 构建与自动化验收

唯一受支持的固件复现组合：

```sh
arduino-cli core install esp32:esp32@3.3.11
arduino-cli lib install TFT_eSPI@2.5.43
bash ino/tools/static_check.sh
bash ino/tools/build_check.sh
bash ino/tools/run_tests.sh
```

`static_check.sh` A-J 必须同时通过：DNT hash、DNT 单入口、禁止低层旁路、旧硬件假设禁止、UI GPIO/构建宏一致、ST7735S 时钟上限、radio lock 接线、schema/version、固定依赖+`USE_FSPI_PORT`+`SPI_PORT=2`、atomic/completion/失败出参契约。

CI 的 firmware job 必须真实执行 ESP32-S3 production compile，而不仅是 host mock；host tests 必须包含 rollover 回归，并保持 golden CSV 不变。任何为了“让 CI 绿”而删除 gate、放宽 DNT hash、取消 production compile、或改变 golden fixture 都不接受。

## 5. 实板验收：合并/发布前必须执行

1. **TFT 启动**：烧录修复分支同一构建产物；串口先看到 `SPI_PORT=2 ... @10000000 Hz`；设备不再 StoreProhibited 重启，持续运行至少 10 分钟。
2. **实体连线**：断电测 H4-4/5/6/7 与外接屏 SCLK/MOSI/CS/DC，H5-18 与 RST 的 continuity；确认无短路到 DNT 保留 GPIO1/2/8-18。原理图确认网络名不能替代线束实测。
3. **ST7735S 面板**：执行全屏红/绿/蓝/白/黑、四边 1 px 边框和四角标记；据结果确认 rotation、offset、RGB/BGR、invert。只有实测异常才修改 panel-specific 参数。
4. **DNT Worker**：`LCR CORE INIT...` 后进入菜单；不得死锁。正常模式测量路径保持 diagnostics off。
5. **标准件**：标准 R、C、L 各至少 5 次；L 同时检查 L 与 DCR，保留负 DCR warning 路径，不在应用层把负值偷偷钳为 0。
6. **One-Port**：10 Hz-10 kHz 完整扫频；确认 actual `f_act` 单调且 CSV 使用 actual f；StopTone 后示波器确认 DAC 输出停止；seal 后才能 BLE；上传网站后 CRC 一致并能拟合。
7. **取消**：在 2 点块和 3 点块内各触发取消；UI 显示“当前块后停止”，当前块完成后不再开始新 chunk，必须收到 StopTone completion 后才退出 measurement lock；示波器确认激励归零。
8. **Two-Port**：完整 W sweep，metadata 必须仍为 `raw_w_path`；不得套用单口校准。用已知网络检查 magnitude/phase 与复 H 趋势。
9. **射频互斥**：测量窗口内 BLE 必须 Off；seal 后才能 advertising。重复连接/断开不少于 20 次，无 queue 卡死、无 pending-id 永久等待、无明显内存递减。
10. **长时间/回绕相关**：host 已覆盖数值回绕；若设备用于长期运行，至少做数小时 sweep/cancel/idle 循环并记录 heap/stack high-water mark。只有测得 high-water mark 后才允许缩减 Worker stack。

## 6. 最终 review 结论边界

本次修复允许作出的声明：

- 已确定并消除 `TFT_eSPI 2.5.43 + Arduino-ESP32 3.3.11` 默认 S3 SPI host 选择导致的已知 `EXCVADDR=0x10` 启动失效路径；
- 生产构建现在明确选择用户 SPI2，且由 compile/static gate 锁定；
- ST7735S 时钟满足数据手册 66 ns 约束；
- DNT 失败出参、跨 task flag、completion liveness、millis 回绕和 calibration-state 真实性已有明确契约和自动化保护；
- DNT 测量核心及其 GPIO/校准数学没有被本次修改。

本次修复**不能**仅凭 CI 声明：具体外接屏模组的 offset/invert/color 已正确、线束 continuity 已确认、模拟测量精度已通过标准件校准、或旧 ELF 的每个 PC 地址已经符号化。这些结论必须分别由实板测试或原构建 ELF 证据完成。

## 7. 依据

- 项目板卡资料：`开发板手册.pdf`、自制开发板原理图/PCB；仓库 `docs/HARDWARE_MAPPING.md`。
- ST7735S controller datasheet：项目 `TFT_ST7735S_Sitronix_datasheet_mirror (1).pdf`，4-line serial AC characteristics。
- Espressif ESP32-S3 Series Datasheet / ESP32-S3-WROOM-1 & WROOM-1U Datasheet。
- Espressif ESP-IDF Programming Guide：ESP32-S3 SPI Master Driver、GPIO & RTC GPIO、FreeRTOS task API。
- Arduino-ESP32 3.3.11：`cores/esp32/esp32-hal-spi.h`（S3 `FSPI` logical bus index）。
- TFT_eSPI 2.5.43 ESP32-S3 processor header（`USE_FSPI_PORT` -> `SPI_PORT=2`）；上游修复提交 `83d4d16451de9dfb55cd3c0242e641fd37152abc`。
