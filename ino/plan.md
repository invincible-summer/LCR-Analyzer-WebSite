# ino 模块修复、合并与实板验收计划

状态：**代码修复候选已形成；按本文软件合并门禁验证通过后合并 `main`。实板验收属于后续硬件发布门禁，不以 CI 代替。**  
基线：`main@3ad2acc0126235d70549c8892d5994755c3aeb7b`  
修复分支：`fix/ino-runtime-20260912`  
评审入口：PR #3 `Fix ESP32-S3 TFT boot crash and harden ino runtime paths`

本文是本次 `ino/` 修复的唯一执行计划和合并审计记录。范围只包括 ESP32-S3 固件、固件构建/测试门禁、UI/FreeRTOS 编排以及直接相关的硬件契约。`AlgorithmLcr`、前后端拟合逻辑和已经 hash 锁定的 `DO_NOT_TOUCH_*` 测量核心不在本次修改范围。除非后续实板证据明确指向 DNT 自身缺陷，否则不得为了应用层问题修改 DNT 文件，也不得在应用层复制第二套 ADC、波形、量程、校准或 Z/H 计算实现。

---

## 1. 目标、完成定义与边界

本次修复有五个直接目标：

1. 消除现场 ESP32-S3 上电后 `StoreProhibited / EXCVADDR=0x00000010` 的已知 TFT 初始化失效路径，并把正确 SPI host 选择固化成构建时约束。
2. 修复 `lcr_api.cpp` 中 DNT 错误路径可能读取未写出参、跨 task `volatile` 同步、completion 丢失以及 StopTone completion 发布顺序等运行时正确性问题。
3. 保持 UI 主循环非阻塞。所有真实硬件调用仍在独立 `lcr_worker` 中串行执行；UI 只能提交 job、消费 completion、推进状态机。
4. 修复 sweep 时间回绕、校准状态真实性，以及隐藏信号发生器页面退出时可能“页面已退出但硬件激励仍在输出”的安全/状态一致性问题。
5. 在代码、静态门禁、ESP32-S3 production compile、host tests 和仓库全量 CI 全部通过，并完成最终 diff review 后，通过 PR #3 合并到 `main`。

“合并到 main”和“硬件发布通过”必须严格区分：

- **main 合并门禁**可以由仓库证据完成：最新 head 的全部 CI job 成功、DNT hash 不变、production firmware 编译成功、host 回归成功、最终 diff review 无未解决问题。
- **硬件发布门禁**必须由真实开发板完成：TFT continuity/显示属性、真实 R/C/L、One-Port/Two-Port、StopTone 示波器确认、BLE/测量互斥、长时间运行等。CI 不能替代这些测试。

因此，本计划允许在软件合并门禁全部满足后合并 `main`；合并本身不宣称具体 TFT 模组、模拟链精度和实体线束已经完成发布级实测。

---

## 2. 现场故障与根因闭环

现场日志在：

```text
LCR-UI v4.1.0 booting (BLE protocol 1, z-schema v2, h-schema v2)
```

之后、`lcrServiceBegin()` 之前立即发生：

```text
Guru Meditation Error: Core 1 panic'ed (StoreProhibited)
EXCCAUSE: 0x1d
EXCVADDR: 0x00000010
```

`LCR_UI.ino` 的真实启动顺序是：

```text
Serial.begin
-> ui::begin
   -> tft.init
-> input.begin
-> lcrServiceBegin
-> Worker 内 lcr_api_init
```

因此故障首先位于 TFT 初始化窗口，而不是 DNT ADC Worker。

固定依赖源码与上游问题形成以下因果闭环：

- 生产工具链冻结为 Arduino-ESP32 `3.3.11` + 已发布 TFT_eSPI `2.5.43`。
- Arduino-ESP32 在 ESP32-S3 上把 `FSPI` 定义成逻辑 bus index `0`。
- TFT_eSPI 2.5.43 的 S3 默认 processor 路径使用 `SPI_PORT=FSPI`，但其 S3 direct-register 宏需要外设编号；用户 GP-SPI 应为 SPI2/SPI3，而不是 0。
- TFT_eSPI 2.5.43 已经提供 `USE_FSPI_PORT` 分支，在 ESP32-S3 上显式选择 `SPI_PORT=2`。
- TFT_eSPI 上游后续也把 S3 默认 `SPI_PORT` 直接改为 `2`；社区已有相同 `EXCVADDR=0x10` 的 S3 失效报告。

因此本次不追踪未通过 Arduino Library Manager 正式发布的库版本，不把构建变成“跟随 master”。修复继续冻结可复现的 2.5.43，并显式开启它已经提供的 `USE_FSPI_PORT`。

必须保留的编译期事实：

```text
Arduino-ESP32 = 3.3.11
TFT_eSPI      = 2.5.43
USE_FSPI_PORT = defined
SPI_PORT      = 2
```

`display.cpp` 对 ESP32-S3 编译要求 `SPI_PORT==2`，否则直接 `#error`。烧录后在 `tft.init()` 前打印实际配置：

```text
TFT init: TFT_eSPI 2.5.43, SPI_PORT=2, SCLK=4 MOSI=5 CS=6 DC=7 RST=21 @ 10000000 Hz
```

如果修复版仍发生 panic，必须保存该次构建对应的新 ELF、新 backtrace 和串口日志，再重新符号化；旧固件的 PC 地址不得继续套用到新构建。

---

## 3. 硬件与数据手册硬约束

### 3.1 ESP32-S3-WROOM-1-N16R8 与 GPIO

项目开发板资料确认模块是 ESP32-S3-WROOM-1-N16R8。原理图/排针映射确认当前 TFT 候选线：

```text
SCLK  GPIO4   (H4-4)
MOSI  GPIO5   (H4-5)
CS    GPIO6   (H4-6)
DC    GPIO7   (H4-7)
RST   GPIO21  (H5-18)
MISO  unused
```

当前代码继续使用这组映射，不因为 SPI2 的 IO_MUX 默认脚不同而擅自改线。ESP32-S3 的 GPIO matrix 允许 GP-SPI 路由到其它可用 GPIO；当前产品时钟只有 10 MHz，低于 Espressif 对 GPIO-matrix SPI 在 40 MHz 及以下给出的等效工作区间。

固件静态门禁必须继续排除：

- DNT 测量链 GPIO1/2、GPIO8-18；
- strapping GPIO0/3/45/46；
- USB-JTAG GPIO19/20；
- SPI0/1 flash/PSRAM GPIO26-32；
- N16R8/Octal PSRAM 条件下 GPIO33-37；
- 本板 UART0/CH340 路径 GPIO43/44。

原理图网络映射不是实体 continuity 证明；线束、焊点、排针仍需断电万用表确认。

### 3.2 ST7735S 4-line serial

所附 ST7735S 数据手册的 4-line serial AC characteristics 规定：

```text
TSCYCW (Serial Clock Cycle, Write) >= 66 ns
```

理论写时钟上限约 `15.1515 MHz`。产品保持 `10 MHz`，并由：

- `build_check.sh` 固定 `-DSPI_FREQUENCY=10000000`；
- `display.cpp` 编译期拒绝 `>15151515`；
- `static_check.sh` Gate F 同时检查 board profile 与 build flags；

三层锁定。

控制器 datasheet 不能决定具体 TFT 模组的 tab、X/Y offset、RGB/BGR、invert。没有实屏 color-bar/边界像素证据时，这些 panel-specific 参数保持现状，不凭经验修改。

### 3.3 FreeRTOS / DNT task-affinity

DNT ADC 初始化时保存 `xTaskGetCurrentTaskHandle()`，ADC ISR 后续通知该 task。因此：

```text
lcr_api_init()
以及所有后续 DNT 测量 API
```

必须始终由同一个 `lcr_worker` task 调用。禁止把 DNT init 移回 `setup()`，也禁止 UI task 直接调用 DNT。

`xTaskCreatePinnedToCore()` 在 ESP-IDF FreeRTOS 中的 stack 参数单位是 bytes，因此当前 `kWorkerStack=16384` 是 16 KiB。没有真实 `uxTaskGetStackHighWaterMark` 数据前不缩栈。

---

## 4. `lcr_api` 服务层修复计划与最终接口语义

### 4.1 公开边界保持不变

UI/编排层只依赖 `ino/LCR_UI/lcr_api.h`：

```cpp
class ILcrService {
public:
    virtual bool submit(LcrJob& job) = 0;
    virtual bool takeEvent(LcrEvent& ev) = 0;
    virtual bool busy() const = 0;
    virtual void requestCancel() = 0;
};
```

`lcr_api.cpp` 是非 DNT 生产代码中唯一允许 include `DO_NOT_TOUCH_lcr_api.h` 的编译单元。此边界由 Gate C/D 强制。

### 4.2 DNT 未写出参的错误路径

DNT 的真实契约明确存在“返回错误但不写出参”的路径。例如：

```cpp
int lcr_api_measure_z(double f, LcrZPoint *out)
```

在 `lcr_derive_z()` 失败时返回 `LCR_API_ERR_MEASURE`，此时 `*out` 未被写入。

wrapper 必须遵守：

- 临时结构统一 `{}` 初始化，但零初始化不能被当成“DNT 已写出结果”；
- `MeasureAndCalcZ` 的 Z 测量失败时绝不读取 `p`，也不调用 calc；
- 失败 `AppZPoint`：`fReq=requested`，`fAct/re/im/mag/phase/D/Q=NaN`，`apiType='E'`，`apiStatus=真实错误码`；
- 未执行的 `AppCalcResult` 全数值 NaN，`type='E'`，状态镜像前置失败码；
- `SweepZChunk/SweepWChunk` 只有 `r>0` 或 `LCR_API_ERR_MEASURE_ALL` 才读取 DNT 输出数组，因为这两类返回已经进入逐点测量循环；参数/缓冲区等早退错误由 wrapper 自己构造失败槽位；
- 成功 Z 后继续调用 `lcr_api_calc(..., false)`，禁止二次校准。

### 4.3 跨 task 状态同步

以下状态在 Arduino loop task 与 `lcr_worker` 间共享：

```text
ready
initFailed
jobInFlight
cancelRequested
```

不能用 `volatile bool` 当作 C++ 跨 task 同步。最终实现使用 `std::atomic<bool>`；状态发布/消费用 release/acquire。job/event 数据本身由 FreeRTOS queue 传递。

### 4.4 completion 可靠性

`LcrEvent` 是控制面，不是可丢 telemetry。原实现 event queue 满时有限次重试后静默丢弃，会造成 `SweepEngine::m_pendingId` 永久等待。

最终语义：

```cpp
xQueueSend(s_eventQ, &ev, portMAX_DELAY)
```

只允许阻塞专用 Worker；UI 主循环继续运行、消费 event 并释放队列空间。job submit 仍保持 `0 tick` 非阻塞。

### 4.5 StopTone completion 的因果顺序

StopTone completion 被 UI 观察到时，上一轮取消状态必须已经完全收尾。最终顺序固定为：

```text
DNT lcr_api_set_freq(0) 返回
-> s_jobInFlight = false
-> 若 StopTone：s_cancelReq = false
-> push StopTone completion
```

禁止先发布 completion 再清 `cancelReq`，否则 UI 可在看到“停机完成”后立即提交下一次测量，而 Worker 仍可能用上一轮 cancel 标志错误取消新 job。

Gate J 必须验证此顺序。

---

## 5. SweepEngine 修复计划与状态机约束

### 5.1 频率与数据真实性

保持当前全局几何网格和 2/3 点 chunk 方案。成功数据只能使用 DNT `f_act`；requested frequency 只用于诊断失败记录。失败点不进入拟合 CSV，也不伪造为零。

### 5.2 取消语义

DNT 调用是同步真实硬件操作，应用层不能中途打断。取消始终是：

```text
用户请求取消
-> 当前 2/3 点 chunk 完成
-> 不提交后续 measurement chunk
-> 提交 StopTone
-> 收到 StopTone completion
-> 解除 measurement lock
-> Cancelled / 返回 UI
```

任何 invariant 异常也必须走 StopTone 收尾，不能仅把软件状态改成 Error 后假装硬件已经停止。

### 5.3 calibration state

`ReadCalibrationStatus` 失败不得被零初始化结构伪装成 `cal:0/10`。失败时进入 cancel/StopTone 路径，不封存带虚假 calibration metadata 的数据集。

### 5.4 `millis()` 回绕

StopTone 后 20 ms quiet guard 使用：

```cpp
(int32_t)(nowMs - deadlineMs) >= 0
```

只要 deadline 距离小于 `2^31 ms` 就正确；本项目只有 20 ms。新增 `test_rollover.cpp` 固定覆盖：

```text
StopTone time = 0xfffffff5
+20ms deadline = 0x00000009
0x00000008 不可 seal
0x00000009 才允许 seal
```

测试入口必须使用仓库统一的 `testSummary("test_rollover")`。

内部封存时间字段从错误语义 `sealedUnixMs` 改为 `sealedUptimeMs`，类型与 `millis()` 保持 `uint32_t`，不把 uptime 宣称成 Unix epoch。

---

## 6. 隐藏 Signal Generator 修复计划

这是第二轮完整 review 新发现、必须在合并前修掉的运行时问题。

旧 `screen_siggen.cpp` 的 Back 路径存在三个错误：

1. `while (m_pending) + delay(2)` 最多阻塞 UI 1 秒，违反运行时非阻塞架构。
2. 如果 Back 恰好发生在 SetTone pending：`stopOutput()` 因 pending 直接返回；等待期间 SetTone 成功后旧代码立即 `screens.pop()`，没有再提交 StopTone，硬件可能继续输出。
3. 1 秒超时会直接 `radioLockNotifyMeasurementActive(false)`，即使尚未得到 StopTone completion，软件可能提前宣称硬件已安全停止。

最终状态机约束：

```text
Back
-> m_exitRequested = true
-> 不阻塞、不 delay、不强制 unlock

若当前 SetTone pending：
   等 SetTone completion
   -> 成功：m_running=true
   -> 下一 onTick 提交 StopTone
   -> 失败：确认未建立输出，才可 unlock

若当前已 running：
   onTick 非阻塞提交 StopTone
   -> 队列暂满则下一 tick 重试

StopTone pending：
   等 id + kind 匹配的 completion

StopTone completion：
   m_running=false
   actualHz=0
   unlock measurement/radio
   下一 tick screens.pop()
```

具体字段：

```cpp
bool m_running;
bool m_pending;
bool m_exitRequested;
uint32_t m_pendingId;
LcrJobKind m_pendingKind;
```

`m_running` 表示“硬件输出尚未被 StopTone completion 证明已停止”，所以提交 StopTone 时不能提前清零。

completion 必须同时满足：

```cpp
ev.id == m_pendingId && ev.kind == m_pendingKind
```

仅按 kind 匹配不够，旧/过期的同类 event 不能误完成当前动作。

Gate K 固定以下不变量：

- `screen_siggen.cpp` 不允许 `while(...)` busy wait；
- 不允许 `delay(...)` 等待硬件；
- Back handler 不得直接 release measurement lock；
- 必须维护 `m_exitRequested/m_pendingId/m_pendingKind`；
- completion 必须 id+kind 匹配；
- measurement lock 的释放点只能是“SetTone 明确失败”和“StopTone completion”。

---

## 7. TFT / 构建配置修复计划

`ino/tools/build_check.sh` 是唯一受支持的 production compile 入口，要求：

```text
esp32:esp32 core       3.3.11
TFT_eSPI               2.5.43
FQBN                    esp32:esp32:esp32s3
PSRAM                   opi
FlashSize               16M
CDCOnBoot               default / Disabled for this board path
PartitionScheme         app3M_fat9M_16MB
```

TFT flags：

```text
-DUSER_SETUP_LOADED
-DUSE_FSPI_PORT
-DST7735_DRIVER
-DTFT_WIDTH=128
-DTFT_HEIGHT=160
-DTFT_SCLK=4
-DTFT_MOSI=5
-DTFT_MISO=-1
-DTFT_CS=6
-DTFT_DC=7
-DTFT_RST=21
-DSPI_FREQUENCY=10000000
```

任何本机安装了其它版本、但“碰巧能编译”的环境都不视为受支持构建。

---

## 8. 自动化测试与 CI 合并门禁

### 8.1 静态门禁 A–K

最终 `static_check.sh` 必须全部通过：

- Gate A：11 个 `DO_NOT_TOUCH_*` 文件 SHA-256 不变；
- Gate B：生产代码不引用预留 `DO_NOT_TOUCH_hong.h`；
- Gate C：DNT API include 唯一入口是 `lcr_api.cpp`；
- Gate D：应用层无 DNT 低层符号旁路；
- Gate E：不恢复 PCM5102/I2S/custom ADC 等旧硬件假设；
- Gate F：BoardProfile GPIO、TFT build flags、N16R8 禁用脚、ST7735S 时钟一致；
- Gate G：radio lock 在 measurement/radio 编排层都接线；
- Gate H：firmware/protocol/schema 契约一致；
- Gate I：Arduino-ESP32 3.3.11 + TFT_eSPI 2.5.43 + `USE_FSPI_PORT` + `SPI_PORT=2`；
- Gate J：atomic、completion 不丢、DNT 未写出参保护、StopTone cancel-before-completion；
- Gate K：SigGen Back 非阻塞、id+kind completion、StopTone 前不虚假 unlock。

### 8.2 Host tests

`bash ino/tools/run_tests.sh` 必须全部成功，至少包括：

```text
test_sweep
test_rollover
test_component
test_csv
test_misc
```

同时重新生成 golden CSV 后：

```bash
git diff --exit-code -- \
  frontend/src/lib/__tests__/fixtures/golden_oneport.csv \
  frontend/src/lib/__tests__/fixtures/golden_twoport.csv
```

必须无 diff。本次固件运行时修复不应偷偷改变网站数据协议。

### 8.3 ESP32-S3 production compile

CI 必须真实执行：

```bash
bash ino/tools/build_check.sh
```

而不是只编译 host mock。当前历史验证已经证明 `SPI_PORT=2` 的受控组合可以完成 production build；最终以最新 PR head 的 CI 为准。

### 8.4 仓库全量 CI

最终合并使用**最新 PR head**，以下六个 job 必须全部 `success`，不能拿较旧 SHA 的绿灯替代最新代码：

```text
native (ctest + real4)
wasm + frontend
browser smoke
backend (pytest)
sanitizer (ASan + UBSan)
firmware (esp32s3 compile + static gates + host tests)
```

任何 job `failure/cancelled/skipped` 都阻止合并；修测试时不得删除测试、放宽 gate、改 golden fixture 来“制造绿灯”。

---

## 9. 合并前最终代码 Review

在最新 CI 全绿后，再做一次独立于 CI 的 diff review，顺序固定：

1. `compare main...fix/ino-runtime-20260912`，确认分支只 ahead、不 behind；如 main 已移动，先重新评估差异，禁止盲合并旧基线。
2. 列出 PR 全部 changed files，确认没有任何 `DO_NOT_TOUCH_*`、AlgorithmLcr、frontend/backend 业务源码被意外修改；允许的范围只应是 `ino/` 固件、测试、构建门禁和 `ino/plan.md`/`ino/README.md`。
3. 逐文件复审：
   - `display.cpp`：SPI host/timing gate，不改变 UI 绘制逻辑；
   - `lcr_api.cpp/.h`：task affinity、错误出参、atomic、event reliability、StopTone 发布顺序；
   - `sweep_engine.cpp`：actual-f、取消、cal status、rollover、seal；
   - `screen_siggen.cpp`/`screens.h`：Back 纯异步、id+kind、unlock 条件；
   - `dataset.h`：只做 uptime 语义修正，不改变 CSV schema；
   - `build_check.sh/static_check.sh/run_tests.sh`：门禁不能比 main 原有验证更弱；
   - `test_rollover.cpp`：测试真的覆盖 wrap，而不是仅编译。
4. 再检查 PR diff 中不存在调试残留、临时 pin、未发布依赖、硬编码本机路径、修改 DNT manifest、跳过 sanitizer 等内容。
5. 确认 PR `mergeable=true` 且最新 head SHA 与最后一次 CI SHA 完全一致。

Review 通过后才进入合并动作。

---

## 10. 合并到 `main` 的执行方案

本分支已有多次小步审计提交，最终采用 **squash merge**，使 `main` 保留一个完整、可回滚的固件修复提交，而不是把诊断过程中的中间提交全部带入主线。

合并步骤：

```text
1. 最新 head 全部 CI success
2. 完成 §9 最终 diff review
3. 更新 PR 描述，记录：
   - TFT FSPI/SPI_PORT 根因
   - DNT wrapper/Worker 修复
   - Sweep rollover/calibration 修复
   - SigGen async StopTone 修复
   - 软件合并已通过，但实板发布验收仍待执行
4. Draft PR -> Ready for review
5. 以 expected_head_sha 锁定最新 head 做 squash merge
6. 读取 main HEAD，确认 merge SHA 已成为 main 最新提交
7. 检查 main 上的 merge-trigger CI；如 main CI 出现仅在 merge commit 才发生的问题，立即停止发布并修复，不宣称完成
```

建议 squash commit title：

```text
firmware: fix ESP32-S3 TFT boot crash and harden ino runtime
```

合并动作不得绕过失败 CI，也不得直接 force-push `main`。

---

## 11. 合并后的实板发布验收

以下测试是**硬件发布门禁**。它们不阻止完成软件集成，但在把当前 main 称为“实板发布验证完成”之前必须执行。

### 11.1 TFT / 启动

- 烧录从 main 同一 SHA 构建的固件；
- 串口必须显示 `SPI_PORT=2` 与 10 MHz 配置；
- 不再出现原 `EXCVADDR=0x10` 重启循环；
- 连续运行至少 10 分钟无复位；
- 断电测 TFT 五条信号 continuity，并确认不短接 DNT GPIO；
- 红/绿/蓝/白/黑全屏、1 px 四边框、四角标记验证 rotation/offset/RGB/invert；只有实测异常才调整 panel-specific 参数。

### 11.2 DNT Worker 与标准件

- `lcr_api_init()` 能在 Worker 内完成，不死锁；
- 标准 R/C/L 各至少 5 次；
- L 同时记录 L 与 DCR；应用层不擅自把负 DCR 钳成 0；
- 记录 Worker stack high-water mark；未测量前不缩减 16 KiB stack。

### 11.3 One-Port

- 10 Hz–10 kHz 完整 sweep；
- `f_act` 单调、CSV 使用 actual f；
- 失败点不进入 CSV；
- StopTone completion 后用示波器确认 DAC 输出停止；
- dataset seal 后才允许 BLE；
- 浏览器收到数据后 CRC 正确、schema v2 可解析和拟合。

### 11.4 Cancel / Signal Generator

必须专门覆盖这次修复的竞态：

- 2 点 chunk 内取消；
- 3 点 chunk 内取消；
- Signal Generator 正常 running 时按 Back；
- **SetTone 尚未 completion 时立即按 Back**；
- StopTone pending 时重复输入；

要求：UI 无 1 秒 busy wait；当前硬件调用完成后才 StopTone；StopTone completion 前 measurement/radio lock 不释放；离开页面时示波器确认输出已经停止。

### 11.5 Two-Port / BLE / 长时间

- 完整 W sweep，metadata 保持 `raw_w_path`；
- 不把单端口 calibration 套到 W path；
- 测量窗口 BLE 必须 Off；seal 后才 advertising；
- 连接/断开至少 20 次，无 event queue 卡死和 pending-id 永久等待；
- 做数小时 sweep/cancel/idle 循环，记录 heap、stack watermark 和异常复位。

---

## 12. 回滚与失败处理

如果 main 合并后的 CI 或实板验证出现新问题：

- 若为 merge-only 软件回归：优先 revert 本次 squash commit，恢复已知 main 基线，再在新修复分支处理；
- 若仍是 TFT panic：保存新 main SHA 对应 ELF 和完整 backtrace，重新 addr2line；不要假定仍是旧 FSPI=0；
- 若 TFT 能启动但显示错位/颜色错：只调整 panel-specific 初始化参数，不改 SPI host/测量 GPIO；
- 若 DNT 测量死锁：先验证 init/measurement 是否仍同 task、ISR notification target 是否正确；不得在 UI 增加 blocking workaround；
- 若 StopTone/取消异常：保持 radio lock 为 active，直到有可证明的硬件停止事件；绝不能为了恢复 UI 响应而提前 unlock；
- 若发现确实需要修改 `DO_NOT_TOUCH_*`：本 PR 不扩大范围，单独启动硬件核心变更流程、更新依据和 manifest，再做完整实板回归。

---

## 13. 本计划完成时允许和不允许的声明

软件合并门禁通过并合并 main 后，可以声明：

- 已消除当前受控依赖组合中已知的 ESP32-S3 TFT `SPI_PORT=0` 失效路径，并用 `USE_FSPI_PORT + SPI_PORT==2` gate 固化；
- 当前 TFT SPI 时钟符合 ST7735S `TSCYCW>=66ns` 的控制器时序约束；
- DNT 未写出参路径、cross-task atomic、completion reliability、StopTone 发布顺序、millis rollover、calibration metadata 和 SigGen 非阻塞停机流程已有代码与 CI 门禁；
- DNT 测量核心未被修改；
- 最新候选通过仓库定义的 production compile、host regression 和全量 CI。

仍然不能仅凭 merge/CI 声明：

- 实体 TFT 线束 continuity 已通过；
- 当前具体屏模组的 offset/invert/RGB 设置已被实屏验证；
- 模拟前端精度/校准已达到某个误差指标；
- StopTone 的实际模拟输出已由示波器确认；
- 整机已经完成硬件发布验收。

这些只能由 §11 实板证据支持。

---

## 14. 依据与真源

项目内真源：

- `开发板手册.pdf`；
- 自制开发板原理图/PCB资料；
- `TFT_ST7735S_Sitronix_datasheet_mirror (1).pdf`；
- `docs/HARDWARE_MAPPING.md`；
- `ino/LCR_UI/DO_NOT_TOUCH_lcr_api.h` 及其下游 DNT headers；
- `ino/LCR_UI/board_profile.cpp`；
- `ino/tools/dnt_manifest.txt`。

外部官方/上游依据：

- Espressif ESP32-S3 Series / WROOM-1 datasheet；
- Espressif ESP-IDF Programming Guide：ESP32-S3 GPIO、SPI Master、FreeRTOS task API；
- Arduino-ESP32 3.3.11 `esp32-hal-spi.h`；
- TFT_eSPI 2.5.43 ESP32-S3 processor header 的 `USE_FSPI_PORT` 路径；
- TFT_eSPI 上游 S3 `SPI_PORT=2` 修复与相关 S3 `EXCVADDR=0x10` issue。

任何后续实现如果与这些真源冲突，以真实硬件数据手册、开发板原理图和被锁定的 DNT API 契约为准，而不是以“编译通过”或经验写法为准。
