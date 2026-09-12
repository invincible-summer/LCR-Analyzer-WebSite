# LCR_UI — ESP32-S3 LCR 仪表固件（v4.1.0 · DNT 测量核心 + 应用编排层）

本目录是自制 ESP32-S3-WROOM-1-N16R8 LCR 仪表的固件实现：ST7735S 本地 UI、Component R/C/L、One-Port Z Sweep、Two-Port H Sweep，以及封存后 BLE GATT v1 上传。测量硬件链由 `DO_NOT_TOUCH_*` 头文件实现；应用层只允许经 `lcr_api.h/.cpp` 编排，不得复制第二套波形、ADC、量程、校准或 Z/H 计算链。

## 当前最终接线契约

2026-09-12 主分支硬件提交 `6c46ebfdd6a937515fe9fa4ce27ff1b38313c4f3` 更新了最终测量接线。本次固件修复将这两份硬件文件原样合入并重新锁定 SHA-256：

- `DO_NOT_TOUCH_lcr_measure.h`：74HC595 `SRCLK=GPIO21`、`SER=GPIO19`、`RCLK=GPIO20`；
- `DO_NOT_TOUCH_sinwave.h`：LCD_CAM DAC `D0..D7 = GPIO6,7,15,16,17,18,8,9`；
- `DO_NOT_TOUCH_lcr_adc.h`：ADC 电压 `GPIO2`、电流 `GPIO1`（未改变）。

因此最终测量 GPIO 保留集合为：

```text
ADC:      1, 2
DAC:      6, 7, 8, 9, 15, 16, 17, 18
74HC595:  19, 20, 21
```

原修复候选 TFT `SCK4/MOSI5/CS6/DC7/RST21` 与最终 DNT 接线冲突，不能继续使用。最终 UI 映射恢复到已被测量链释放的 H4 GPIO10-14：

```text
ST7735S CS   = GPIO10  (H4-16)
ST7735S MOSI = GPIO11  (H4-17)
ST7735S SCK  = GPIO12  (H4-18)
ST7735S RST  = GPIO13  (H4-19)
ST7735S DC   = GPIO14  (H4-20)
ST7735S MISO = unused
SPI write    = 10 MHz
```

`board_profile.cpp`、`build_check.sh` 和 `static_check.sh` Gate F 必须保持三方一致；任何 UI pin 与最终 DNT 保留集合重叠都应直接失败，而不是加白名单绕过。

> GPIO19/20 现在属于最终 74HC595 接线。它们同时是 ESP32-S3 原生 USB D-/D+，因此产品构建继续保持 `CDCOnBoot=default/Disabled`，不能再启用原生 USB CDC 与测量硬件争用。板上烧录/日志路径继续使用 UART0 GPIO43/44 → CH340X。

## 测量架构与唯一依赖边

```text
UI / SweepEngine / ComponentMeter / Dataset / BLE
                    │
                    ▼
               lcr_api.h
                    │
                    ▼
               lcr_api.cpp
        （唯一 include DO_NOT_TOUCH_lcr_api.h）
                    │
             FreeRTOS lcr_worker
                    │
                    ▼
         DO_NOT_TOUCH_lcr_api.h
                    │
                    ▼
     DAC / 74HC595 / ADC / Z/H / calibration
```

必须保持的架构约束：

1. `lcr_api_init()` 与所有 DNT 测量 API 必须在同一 `lcr_worker` task 执行。DNT ADC 初始化保存当前 task handle，ISR 后续通知该 task；把 init 移回 Arduino `setup()` 会破坏 task-affinity。
2. DNT API 是同步硬件调用；UI 非阻塞由“loop 提交 job + Worker 串行执行 + completion event”实现。UI 不得 busy-wait 硬件。
3. job submit 对 UI 为零等待；completion 是状态机控制面，不允许静默丢包。Worker 在 event queue 满时可阻塞等待 UI 消费。
4. ready/busy/cancel 等跨 task 标志使用 `std::atomic`，不把 `volatile` 当同步原语。
5. 取消只能发生在 DNT 调用边界：当前 2/3 点 chunk 完成后停止继续提交，再走真实 StopTone completion。
6. StopTone completion 是安全发布边界：上一轮 cancel 状态必须先清除，再发布 completion；UI 收到 completion 后才能把硬件视为已停。
7. DNT 失败路径可能不写出参。wrapper 不得读取未写 `LcrZPoint/LcrWPoint/LcrCalcResult`；失败输出用 NaN / `E` / 真实 status 表达。
8. CSV 只使用实际频率 `f_act`；失败点不进入拟合 CSV，不伪造零值。

## 三个产品模式

| 模式 | 测量路径 | BLE |
|---|---|---|
| Component R/C/L | 5 个几何频点；DNT Z + calc；apiType 一致性 + 中位数 | 不启动 |
| One-Port Z Sweep | 2/3 点 chunk → StopTone → quiet guard → seal → `f,re,im` | seal 后用户确认才启动 |
| Two-Port H Sweep | DNT W/raw H → `f,re_h,im_h`，metadata 标注 `raw_w_path` | 同上 |

隐藏 Signal Generator 页面同样只能经 wrapper 控制激励。Back 路径是异步状态机：若 SetTone pending，先等其 id+kind 匹配的 completion；若确实建立输出，再提交 StopTone；只有 StopTone completion（或 SetTone 明确失败）才允许解除 measurement/radio lock 和离开页面。运行时禁止 `delay()` / busy-wait 等待硬件。

## 构建与验证

受支持的 production 构建组合固定为：

```text
Arduino-ESP32 = 3.3.11
TFT_eSPI      = 2.5.43
ESP32 target  = esp32s3 / N16R8
PSRAM         = OPI
Flash         = 16 MB
CDCOnBoot     = default / Disabled
TFT SPI       = SPI2, 10 MHz
```

执行：

```sh
arduino-cli core install esp32:esp32@3.3.11
arduino-cli lib install TFT_eSPI@2.5.43
bash ino/tools/static_check.sh
bash ino/tools/build_check.sh
bash ino/tools/run_tests.sh
```

### TFT_eSPI 2.5.43 / ESP32-S3 必须使用 `USE_FSPI_PORT`

Arduino-ESP32 3.x 在 S3 上把 `FSPI` 定义成逻辑 bus index `0`，而 TFT_eSPI 2.5.43 的 S3 direct-register 路径需要硬件外设编号。该版本默认 `SPI_PORT=FSPI` 可在 `tft.init()` 触发 `StoreProhibited / EXCVADDR=0x00000010`。2.5.43 已提供 `USE_FSPI_PORT`，在 S3 上选择 `SPI_PORT=2`。

因此 `build_check.sh` 必须注入：

```text
-DUSE_FSPI_PORT
-DTFT_CS=10 -DTFT_MOSI=11 -DTFT_SCLK=12
-DTFT_RST=13 -DTFT_DC=14 -DTFT_MISO=-1
-DSPI_FREQUENCY=10000000
```

`display.cpp` 对 S3 编译强制 `SPI_PORT==2`。ST7735S v1.3 Table 7 规定 4-line serial write `TSCYCW >= 66 ns`，理论上限约 15.15 MHz；产品固定 10 MHz，编译期和 Gate F 都拒绝超限。

烧录后，在 `tft.init()` 前应先看到：

```text
LCR-UI v4.1.0 booting (BLE protocol 1, z-schema v2, h-schema v2)
TFT init: TFT_eSPI 2.5.43, SPI_PORT=2, SCLK=12 MOSI=11 CS=10 DC=14 RST=13 @ 10000000 Hz
```

若第二行之后仍 panic，需要保存该次构建对应 ELF 与完整 backtrace 重新符号化，不能直接沿用旧固件地址或旧 `FSPI=0` 结论。

## 静态门禁 A-K

`tools/static_check.sh` 负责：

- A：11 个 DNT 文件 SHA-256 manifest；当前两份最终接线 DNT 已由硬件更新流程重新锁定；
- B/C/D：禁止 hong 进入生产、DNT API 单入口、禁止低层旁路；
- E：禁止恢复 PCM5102/I2S/custom ADC 等废弃硬件假设；
- F：最终 DNT GPIO + N16R8/strap/UART 保留脚、BoardProfile、TFT compile flags、ST7735S 时钟一致性；
- G/H：radio lock 与 firmware/protocol/schema；
- I：Arduino-ESP32 3.3.11 + TFT_eSPI 2.5.43 + `USE_FSPI_PORT` + `SPI_PORT=2`；
- J：atomic、completion reliability、未写出参保护、StopTone cancel-before-completion；
- K：Signal Generator 退出非阻塞、id+kind completion、StopTone 完成前不得提前 unlock。

DNT manifest 更新只允许在硬件团队明确修改 DNT 后执行：

```sh
DNT_UPDATE_MANIFEST=1 bash ino/tools/static_check.sh
```

常规应用层修改不得重生成 manifest 来掩盖意外 DNT 变化。

## 数据与状态真实性

- `millis()` 是 32-bit uptime，不是 Unix epoch；内部字段使用 `sealedUptimeMs`。
- deadline 比较使用回绕安全差值；`test_rollover.cpp` 覆盖 `0xfffffff5 + 20ms -> 0x00000009`。
- `ReadCalibrationStatus` 失败必须终止并 StopTone，不得把零初始化结构写成假 `cal:0/10`。
- 双端口 W 路径保持 `raw_w_path`，不套用单端口 calibration。
- 失败点进入诊断，不进入拟合 CSV。

## 合并与实板发布是两个门禁

软件合并到 `main` 前要求最新 PR head 的完整仓库 CI 六个 job 全部 `success`：native、wasm+frontend、browser smoke、backend pytest、ASan+UBSan、firmware（static A-K + ESP32-S3 compile + host tests）。较旧 SHA 的绿灯不能替代最新代码。

合并完成不等于实体硬件发布验收。真实开发板仍需完成：

- TFT GPIO10-14 continuity、上电、边框/color-bar、rotation/offset/RGB/invert；
- 标准 R/C/L 多次测量和 L+DCR；
- One-Port / Two-Port 全扫频与 `f_act`；
- 示波器确认 StopTone completion 后模拟输出确实停止；
- SetTone pending 时立即 Back 的竞态；
- 测量期间 BLE 静默、seal 后 BLE、反复连接/断开；
- 数小时 sweep/cancel/idle，记录 heap、stack watermark、异常复位。

详细修复、合并和实板验收计划见 `ino/plan.md`。最终接线发生冲突时，以当前 `DO_NOT_TOUCH_*` 测量硬件定义和 `board_profile.cpp` 为准；禁止根据旧文档或旧候选 GPIO 继续接线。
