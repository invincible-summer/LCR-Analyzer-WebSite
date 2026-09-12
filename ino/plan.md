# ino 模块最终接线合并、运行时修复与实板验收计划

状态：**最终测量接线已从 `main@6c46ebfdd6a937515fe9fa4ce27ff1b38313c4f3` 安全合入修复分支；UI/TFT 接线、构建门禁和 DNT manifest 已据此重新收敛。只有最新 head 的完整 CI 全绿并完成最终 diff review 后才 squash merge 到 `main`。实板验收仍是独立发布门禁。**

修复分支：`fix/ino-runtime-20260912`  
评审入口：PR #3 `Fix ESP32-S3 TFT boot crash and harden ino runtime paths`

本文是本次 `ino/` 软件集成的最终执行与验收计划。它同时记录两类事实：一类是用户已经冻结的测量硬件接线，必须原样保留；另一类是应用层/运行时修复，可以修改但不能突破 DNT API 边界。任何后续实现与本计划冲突时，优先级为：**用户确认的最终 `DO_NOT_TOUCH_*` 接线 > 当前 `board_profile.cpp` UI 接线 > 构建/静态门禁 > 旧文档或旧候选 pin。**

---

## 1. 合并目标、边界与完成定义

本次合并要同时满足六个目标：

1. 消除 ESP32-S3 在 `tft.init()` 的已知 `StoreProhibited / EXCVADDR=0x00000010` 失效路径，固定 TFT_eSPI 2.5.43 的正确 SPI2 direct-register 配置。
2. **完整保留主分支最终测量接线**，不得因旧 UI 映射或旧静态白名单把 GPIO 改回去。
3. 让 TFT/UI 迁移到与最终测量链无冲突的 GPIO，并让 `board_profile.cpp`、TFT 编译宏和 Gate F 三方完全一致。
4. 保留已完成的 `lcr_api`/Worker、SweepEngine、Signal Generator 非阻塞/StopTone、`millis()` rollover、错误出参保护等运行时修复。
5. 重新锁定 DNT manifest，使 Gate A 只接受本次用户批准的最终硬件版本，后续任何意外 DNT 漂移继续硬失败。
6. 最新 head 完整 CI 六个 job 全绿、最终 diff review 通过后，通过 PR #3 **squash merge** 到 `main`；不 force-push，不绕过失败 CI。

软件合并门禁与硬件发布门禁严格分离。CI 可以证明源代码、接口、静态约束、host regression 和 ESP32-S3 production compile；不能证明具体线束 continuity、TFT 面板 offset/invert/RGB、模拟精度或 StopTone 的真实模拟输出已经通过实板验证。

---

## 2. 最终测量接线：不可回退的硬件真源

主分支提交 `6c46ebfdd6a937515fe9fa4ce27ff1b38313c4f3` 修改了两份硬件拥有的 DNT 文件。本次分支通过 merge parent + 原 blob 引用保留它们的实际内容，而不是人工重写。

### 2.1 ADC

`DO_NOT_TOUCH_lcr_adc.h` 保持：

```text
GPIO2 = ADC1_CH1 = 电压
GPIO1 = ADC1_CH0 = 电流
```

### 2.2 LCD_CAM 8-bit 正弦 DAC

`DO_NOT_TOUCH_sinwave.h` 最终：

```text
D0 = GPIO6
D1 = GPIO7
D2 = GPIO15
D3 = GPIO16
D4 = GPIO17
D5 = GPIO18
D6 = GPIO8
D7 = GPIO9
```

### 2.3 74HC595

`DO_NOT_TOUCH_lcr_measure.h` 最终：

```text
SRCLK = GPIO21
SER   = GPIO19
RCLK  = GPIO20
```

因此应用层/UI 的**最终禁止占用集合**至少包含：

```text
{1,2,6,7,8,9,15,16,17,18,19,20,21}
```

Gate F 必须把这组值作为硬约束；不得再沿用旧 `{1,2,8..18}` 推导，也不得因为某个 UI 候选已经写进文档就豁免冲突。

### 2.4 DNT manifest 重新锁定

只有两份用户明确修改的 DNT 文件 hash 改变：

```text
DO_NOT_TOUCH_lcr_measure.h
1a020e6c55df9d0add824712ae86fda0fac0bfb13d294fa7b06169bc22ec98e6

DO_NOT_TOUCH_sinwave.h
97944b47bf463496598b908e5ca89b1ab3936b58491c05d674b3bf5c66fe1141
```

其余 DNT hash 保持原值。Gate A 后续继续执行 `sha256sum -c`；普通应用层 PR 不允许用 `DNT_UPDATE_MANIFEST=1` 消除意外漂移。只有硬件团队明确批准新的 DNT 版本时，才进入重新锁定流程。

---

## 3. UI/TFT 最终映射与冲突消解

原修复候选曾使用：

```text
SCK=4 MOSI=5 CS=6 DC=7 RST=21
```

这组映射在新的最终 DNT 下已经非法：GPIO6/7 被 LCD_CAM DAC 占用，GPIO21 被 74HC595 占用。因此不能把“保留用户 DNT”与“保留旧 TFT 候选”同时成立；硬件真源优先，必须迁移 TFT。

历史板卡 profile/排针映射已经确认 H4 的 GPIO10-14 是连续可用的一组，而最终 DNT 正好释放了它们，因此最终 TFT 使用：

```text
CS   = GPIO10  H4-16
MOSI = GPIO11  H4-17
SCK  = GPIO12  H4-18
RST  = GPIO13  H4-19
DC   = GPIO14  H4-20
MISO = unused
```

接口约束：

```cpp
const BoardProfile kBoard = {
    .tftCs = 10,
    .tftDc = 14,
    .tftRst = 13,
    .spiSck = 12,
    .spiMosi = 11,
    .spiMiso = PIN_UNUSED,
    .tftSpiHz = 10000000,
    ...
};
```

TFT_eSPI 编译宏必须逐项等于 `kBoard`：

```text
-DTFT_CS=10
-DTFT_MOSI=11
-DTFT_SCLK=12
-DTFT_RST=13
-DTFT_DC=14
-DTFT_MISO=-1
-DSPI_FREQUENCY=10000000
```

### 3.1 ESP32-S3 平台限制

除最终 DNT GPIO 外，UI 仍不得使用：

```text
GPIO0/3/45/46   strapping
GPIO26-32       SPI0/1 flash/PSRAM
GPIO33-37       N16R8 octal PSRAM 条件占用
GPIO43/44       本板 UART0/CH340X 烧录/日志
```

GPIO19/20 现在由最终 74HC595 接线占用，同时也是 ESP32-S3 原生 USB D-/D+。因此产品 FQBN 必须继续保持 `CDCOnBoot=default/Disabled`，不能启用原生 USB CDC。日志/烧录继续走 GPIO43/44 → CH340X。这个约束属于最终接线的一部分，不能在以后“为了 Serial 方便”单独改开。

---

## 4. TFT boot panic 根因与受支持构建

现场故障发生顺序：

```text
Serial.begin
-> ui::begin
   -> tft.init
-> input.begin
-> lcrServiceBegin
```

所以原始 `EXCVADDR=0x10` 首先落在 TFT 初始化窗口。

受支持依赖冻结为：

```text
Arduino-ESP32 3.3.11
TFT_eSPI      2.5.43
ESP32-S3      N16R8
PSRAM         OPI
FlashSize     16M
```

根因链：Arduino-ESP32 在 S3 把 `FSPI` 暴露为逻辑 bus index 0；TFT_eSPI 2.5.43 的 S3 默认 direct-register 路径使用 `SPI_PORT=FSPI`，而寄存器宏需要真实外设号。2.5.43 已提供 `USE_FSPI_PORT`，在 S3 选择 `SPI_PORT=2`。

因此 production compile 必须同时满足：

```text
-DUSE_FSPI_PORT
SPI_PORT == 2
```

`display.cpp` 在 S3 上使用 compile-time `#error` 防止配置漂移。启动诊断必须在 `tft.init()` 前打印：

```text
TFT init: TFT_eSPI 2.5.43, SPI_PORT=2, SCLK=12 MOSI=11 CS=10 DC=14 RST=13 @ 10000000 Hz
```

若新固件仍 panic，必须保存该次 SHA 对应 ELF、backtrace 和完整串口日志重新符号化；不能把旧 PC 或旧 `FSPI=0` 结论机械套用。

### 4.1 ST7735S 时序

ST7735S v1.3 Table 7 的 4-line serial write：

```text
TSCYCW >= 66 ns
```

理论上 `fSCL <= 15.1515 MHz`。产品继续 10 MHz：

- `build_check.sh` 固定 10 MHz；
- `display.cpp` 编译期拒绝 `>15151515`；
- Gate F 同样检查 profile 与编译宏。

具体 TFT 模组的 visible offset、RGB/BGR、invert 不由控制器 datasheet 单独决定；没有实屏证据前保持当前参数。

---

## 5. `lcr_api`/Worker 接口与并发语义

应用层唯一测量边界保持 `ino/LCR_UI/lcr_api.h`；`lcr_api.cpp` 是非 DNT 生产代码中唯一允许 include `DO_NOT_TOUCH_lcr_api.h` 的编译单元。

### 5.1 task-affinity

DNT ADC init 保存 `xTaskGetCurrentTaskHandle()`，ISR 后续通知该 task。因此：

```text
lcr_api_init()
所有 lcr_api_measure/sweep/calc/status/set_freq 调用
```

必须始终由同一 `lcr_worker` task 执行。UI task 不直接调用 DNT。

### 5.2 job/event 语义

- UI `submit`：零等待、非阻塞；队列满返回 false，状态机下一 tick 决定重试。
- Worker：一次只执行一个真实硬件 job。
- `LcrEvent`：控制面 completion，不允许丢弃。
- event queue 满：允许专用 Worker `xQueueSend(..., portMAX_DELAY)` 等待 UI 消费；不能有限重试后丢包。

### 5.3 跨 task 状态

`ready/initFailed/jobInFlight/cancelRequested` 使用 `std::atomic<bool>`；不能依赖 `volatile` 提供跨 task happens-before。job/event payload 的跨 task 传递由 FreeRTOS queue 完成。

### 5.4 DNT 未写出参

DNT 可能返回错误但不写 `out`。wrapper 规则：

- 局部对象 `{}` 初始化仅防 UB，不代表结果有效；
- `MeasureAndCalcZ` 测量失败后不读取 `LcrZPoint`、不调用 calc；
- sweep 只有真实进入逐点循环的返回类型才读取输出数组；
- 早退参数/缓冲区错误由 wrapper 构造 NaN/`E` 失败槽位；
- `apiStatus/backendStatus` 保留真实 DNT 错误码。

### 5.5 StopTone completion 发布顺序

严格固定：

```text
DNT set_freq(0) 返回
-> jobInFlight=false
-> StopTone 时 cancelRequested=false
-> push completion
```

UI 看到 StopTone completion 时，上一轮 cancel 状态必须已经清理；否则新 job 可能被旧取消标志误伤。Gate J 必须检查此顺序。

---

## 6. SweepEngine、时间与数据真实性

Sweep 仍按 2/3 点 chunk 调 DNT，同步硬件调用不可从 UI 中途强杀。取消语义：

```text
用户取消
-> 当前 chunk 完成
-> 不提交下一个 measurement chunk
-> StopTone job
-> StopTone completion
-> 20 ms quiet guard
-> 解锁/Cancelled 或完成 seal
```

任何 radio invariant/calibration-status 异常也必须经过 StopTone 收尾，不能只改软件状态为 Error。

数据规则：

- CSV 成功点频率只用 `f_act`；
- requested frequency 只保留为诊断；
- 失败点不进入拟合 CSV；
- `ReadCalibrationStatus` 失败不得生成虚假 `cal:0/10`；
- Two-Port 保留 `raw_w_path`，不套用 One-Port calibration。

`millis()` 是 `uint32_t` uptime。deadline 使用：

```cpp
(int32_t)(nowMs - deadlineMs) >= 0
```

`test_rollover.cpp` 固定覆盖：

```text
StopTone = 0xfffffff5
deadline = 0x00000009
0x00000008 仍 Stopping
0x00000009 才 TransferReady/seal
```

内部字段使用 `sealedUptimeMs`，不再冒充 Unix epoch。

---

## 7. Signal Generator 非阻塞退出契约

Back 不得 busy-wait、`delay()` 或提前释放 measurement/radio lock。

状态必须至少包含：

```cpp
bool m_running;
bool m_pending;
bool m_exitRequested;
uint32_t m_pendingId;
LcrJobKind m_pendingKind;
```

流程：

```text
Back -> exitRequested=true

SetTone pending:
  等 id+kind 匹配 completion
  成功 -> running=true -> 下一 tick 提交 StopTone
  失败 -> 确认没有输出 -> 可解锁并退出

running:
  非阻塞提交 StopTone
  job queue 暂满 -> 后续 tick 重试

StopTone pending:
  保持 running=true / lock=true
  等 id+kind 匹配 completion

StopTone completion:
  running=false
  actualHz=0
  release measurement/radio lock
  pop screen
```

Gate K 允许的唯一 `while` 是零等待 drain event queue 的有限消费循环；任何硬件等待 while 或 runtime `delay()` 都失败。

---

## 8. 静态门禁 A-K 的最终职责

### Gate A

验证 11 个 DNT 文件的已批准 SHA-256。当前最终 wiring 的两份新 hash 已重新锁定，其余不变。

### Gate B-D

禁止 hong 进入生产；DNT API 只有 `lcr_api.cpp` 一个应用入口；禁止应用层直接调用 `out_freq/lcr_adc_/lcr_measure_/HC595_PIN_` 等低层实现。

### Gate E

禁止恢复 PCM5102/I2S/custom ADC 等已废弃的第二套硬件链。

### Gate F

同时验证：

```text
UI pins ∩ {1,2,6,7,8,9,15,16,17,18,19,20,21} == ∅
UI pins ∩ platform_reserved == ∅
board_profile TFT == build_check TFT macros
SPI_FREQUENCY == tftSpiHz
SPI_FREQUENCY <= 15.1515 MHz
MISO == PIN_UNUSED
```

任何冲突直接失败，不允许“这是最终接线所以豁免 UI”之类反向逻辑。

### Gate G-H

验证 radio lock 接线和 firmware/protocol/schema 契约。

### Gate I

验证 Arduino-ESP32 3.3.11、TFT_eSPI 2.5.43、`USE_FSPI_PORT`、`SPI_PORT==2` 和 ST7735S timing gate。

### Gate J

验证 atomic、completion reliability、未写出参保护和 StopTone cancel-before-completion。

### Gate K

验证 Signal Generator Back 非阻塞、id+kind completion、StopTone completion 前不提前 unlock。

---

## 9. 最新 head CI 门禁

必须以**最后一次代码/文档/manifest 修改后的 head SHA**为准。以下六个 job 全部 `success` 才允许 merge：

```text
firmware (esp32s3 compile + static gates + host tests)
native (ctest + real4)
wasm + frontend
browser smoke
backend (pytest)
sanitizer (ASan + UBSan)
```

firmware job 内至少必须依次完成：

```text
static_check.sh A-K
build_check.sh ESP32-S3 production compile
run_tests.sh host tests + golden checks
```

禁止使用较旧 head 的绿灯，禁止删除测试、放宽 gate 或改 golden fixture 来制造成功。

---

## 10. 合并前最终 Review 清单

CI 全绿后按以下顺序复审：

1. `main...fix/ino-runtime-20260912` 必须 `behind_by=0`；若 main 又移动，重新合并/评估后再跑最新 CI。
2. 确认 PR head 与 CI head 完全一致，PR `mergeable=true`。
3. `DO_NOT_TOUCH_lcr_measure.h` 与 main 最终 wiring blob 完全一致；`DO_NOT_TOUCH_sinwave.h` 同样一致。不得出现为了冲突解决而人工改写 DNT。
4. 确认 DNT diff 只有用户批准的最终 GPIO 变化，manifest 对应这两个新 hash。
5. 确认 TFT 最终为 GPIO10-14；不存在 4/5/6/7/21 的生产配置残留。
6. 确认 `build_check.sh`、`board_profile.cpp/.h`、Gate F 三方相同。
7. 确认 `display.cpp` 仍保留 `USE_FSPI_PORT/SPI_PORT==2` 和 15.15 MHz timing gate。
8. 复审 `lcr_api.cpp/.h`：DNT task-affinity、atomic、未写出参、event reliability、StopTone 发布顺序。
9. 复审 `sweep_engine.cpp`：actual-f、cancel、cal status、rollover、seal。
10. 复审 `screen_siggen.cpp/screens.h`：Back 纯异步、id+kind、unlock 只在可证明停机之后。
11. 确认没有 AlgorithmLcr、前后端业务逻辑、临时本机路径、未发布依赖或跳过 sanitizer 的意外修改。
12. `ino/README.md` 与本计划必须描述最终 GPIO10-14 TFT 和新的 DNT wiring，不能留下旧候选作为当前配置。

---

## 11. 合并执行

全部门禁通过后：

```text
1. PR 从 Draft 标记为 Ready for review
2. 使用 expected_head_sha 锁定已验证的最新 head
3. squash merge PR #3
4. squash title:
   firmware: fix ESP32-S3 TFT boot crash and harden ino runtime
5. 读取 main HEAD，确认 GitHub 返回的 merge SHA 已在 main
6. 检查 push-to-main 触发的新 CI
7. 若 merge-trigger CI 失败，停止发布；不把“PR CI 通过”当成 main 已发布成功
```

禁止直接 force-push `main`，禁止绕过 latest-head CI。

---

## 12. 合并后实板发布验收

### 12.1 TFT/启动

- 使用 main 同一 SHA 构建/烧录；
- 串口确认 `SPI_PORT=2`、`SCLK12/MOSI11/CS10/DC14/RST13`、10 MHz；
- 不再出现原 `EXCVADDR=0x10` 重启循环；
- 断电 continuity 验证 TFT GPIO10-14 到对应排针/屏脚；
- 验证与 DNT `{1,2,6,7,8,9,15,16,17,18,19,20,21}` 无短接；
- 红/绿/蓝/白/黑、1 px 四边框、四角标记验证 rotation/offset/RGB/invert；
- 连续运行至少 10 分钟无异常复位。

### 12.2 测量链

- Worker 内 `lcr_api_init()` 正常完成；
- 标准 R/C/L 各至少 5 次；
- L 同时记录 L 与 DCR；
- One-Port 10 Hz-10 kHz 全 sweep，检查 `f_act`、失败点、CSV/CRC/schema；
- Two-Port 完整 W sweep，保持 `raw_w_path`；
- 示波器验证 StopTone completion 后 DAC 实际停止；
- 记录 Worker stack high-water mark，在没有数据前不缩 16 KiB stack。

### 12.3 取消/Signal Generator/BLE

专门覆盖：

```text
2 点 chunk 内取消
3 点 chunk 内取消
Signal Generator running 时 Back
SetTone pending 时立即 Back
StopTone pending 时重复输入
BLE 连接/断开 >=20 次
```

要求 UI 无 busy wait；StopTone completion 前 lock 不释放；测量期间 BLE 静默，seal 后才 advertising；数小时 sweep/cancel/idle 循环无 event queue 卡死、pending-id 永久等待或异常复位。

---

## 13. 回滚与允许声明

若 merge 后 main CI 或实板出现新回归，优先 revert 本次 squash commit，再在新分支修复；不要直接在 main 叠加未经门禁的 hotfix。

软件合并完成后可以声明：

- 用户最终 DNT 接线已原样保留并重新 hash 锁定；
- TFT 已迁移到不与最终测量链冲突的 GPIO10-14；
- 已封堵 TFT_eSPI 2.5.43 / ESP32-S3 `SPI_PORT=0` 的已知启动崩溃路径；
- Worker/错误出参/completion/StopTone/rollover/SigGen 非阻塞契约经过代码门禁与 CI；
- 最新候选通过 production compile、host regression 和仓库全量 CI（仅在真实全绿后声明）。

仍不能仅凭 CI 声明：

- 实体 TFT continuity 已通过；
- 具体面板 offset/invert/RGB 已实测冻结；
- 模拟前端达到某个精度指标；
- StopTone 模拟输出已由示波器确认；
- 整机已完成硬件发布验收。

项目内当前硬件真源：`DO_NOT_TOUCH_lcr_adc.h`、`DO_NOT_TOUCH_sinwave.h`、`DO_NOT_TOUCH_lcr_measure.h`、`board_profile.cpp`、`build_check.sh`、`dnt_manifest.txt`。ST7735S 时序、ESP32-S3 GPIO/SPI/FreeRTOS 约束分别以所附控制器数据手册、开发板资料和 Espressif 官方文档为依据。
