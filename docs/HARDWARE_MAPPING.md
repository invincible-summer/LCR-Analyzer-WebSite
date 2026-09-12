# 硬件映射 — ESP32-S3 自制板（2026-09-12 最终 DNT 接线 + UI 外设）

> 状态：**当前代码真源文档。** 测量链 GPIO 以 `ino/LCR_UI/DO_NOT_TOUCH_*.h`
> 为最终依据；UI 外设以本文 + `ino/LCR_UI/board_profile.cpp` 为依据。
> 旧文档中“DAC 占 GPIO10-14、TFT 改到 GPIO4/5/6/7/21”的候选映射已经失效，
> 不得继续用于接线或代码。

真源资料：

- 测量链：`DO_NOT_TOUCH_lcr_adc.h`、`DO_NOT_TOUCH_sinwave.h`、`DO_NOT_TOUCH_lcr_measure.h`；
- 板卡：`ino/databook/开发板手册.pdf`、`自制开发板资料/.../开发板原理图.pdf`；
- ESP32-S3：`esp32-s3_datasheet_cn.pdf`、`esp32-s3_technical_reference_manual_cn.pdf`；
- 显示：`TFT_ST7735S_Sitronix_datasheet_mirror (1).pdf`；
- 编码器：`编码器数据手册 (1).PDF` / `(2).PDF`（两份仓库文件 SHA 相同）。

## 1. 测量链 GPIO：DNT 保留集

当前 DNT 定义如下，应用层/UI 不得占用：

| 功能 | GPIO | 真源 |
|---|---:|---|
| ADC 电流通道 | GPIO1 / ADC1_CH0 | `DO_NOT_TOUCH_lcr_adc.h` |
| ADC 电压通道 | GPIO2 / ADC1_CH1 | `DO_NOT_TOUCH_lcr_adc.h` |
| LCD_CAM DAC D0 | GPIO6 | `DO_NOT_TOUCH_sinwave.h` |
| LCD_CAM DAC D1 | GPIO7 | `DO_NOT_TOUCH_sinwave.h` |
| LCD_CAM DAC D2 | GPIO15 | `DO_NOT_TOUCH_sinwave.h` |
| LCD_CAM DAC D3 | GPIO16 | `DO_NOT_TOUCH_sinwave.h` |
| LCD_CAM DAC D4 | GPIO17 | `DO_NOT_TOUCH_sinwave.h` |
| LCD_CAM DAC D5 | GPIO18 | `DO_NOT_TOUCH_sinwave.h` |
| LCD_CAM DAC D6 | GPIO8 | `DO_NOT_TOUCH_sinwave.h` |
| LCD_CAM DAC D7 | GPIO9 | `DO_NOT_TOUCH_sinwave.h` |
| 74HC595 SRCLK | GPIO21 | `DO_NOT_TOUCH_lcr_measure.h` |
| 74HC595 SER | GPIO19 | `DO_NOT_TOUCH_lcr_measure.h` |
| 74HC595 RCLK | GPIO20 | `DO_NOT_TOUCH_lcr_measure.h` |

合计保留集：**{1,2,6,7,8,9,15,16,17,18,19,20,21}**。
`tools/static_check.sh` Gate F 必须从 DNT 宏读取并验证这组事实；不允许靠文档手工白名单绕过。

产品声明频段仍为 10 Hz–10 kHz（`measurement_types.h`）。DNT 内部能力比产品声明宽
不自动扩大产品频段。`lcr_api_init()` 与全部 DNT 测量 API 必须继续在同一个
`lcr_worker` task 执行，原因是 ADC ISR 的 task-affinity 依赖初始化 task handle。

## 2. ESP32-S3 / N16R8 其它限制

| 引脚 | 约束 |
|---|---|
| GPIO0 / 3 / 45 / 46 | strapping；不作为产品常规外设脚 |
| GPIO26–32 | 模组内 Flash/PSRAM，禁止外接 |
| GPIO33–37 | N16R8 八线 Flash/PSRAM 高位总线占用，禁止作为 UI GPIO |
| GPIO43 / 44 | UART0 → 隔离 → CH340X；烧录和日志保留 |
| GPIO19 / 20 | 当前已由 DNT 74HC595 占用，同时是原生 USB D-/D+ 功能脚 |

开发板 Micro USB 实际走 CH340X + 隔离，而不是 ESP32-S3 原生 USB。Arduino 产品配置保持
`USB CDC On Boot = Disabled/default`，否则会让 CDC 尝试占用 GPIO19/20，与当前
74HC595 DNT 接线直接冲突。

## 3. ST7735S 显示：最终接线与几何

最终 UI 使用已由 DNT 释放的 GPIO10–14：

| 信号 | GPIO | 排针 | 说明 |
|---|---:|---|---|
| `tftCs` | GPIO10 | H4-16 | FSPI CS0 可复用为普通 GPIO/SPI |
| `spiMosi` | GPIO11 | H4-17 | FSPI MOSI |
| `spiSck` | GPIO12 | H4-18 | FSPI CLK |
| `tftRst` | GPIO13 | H4-19 | 普通输出 |
| `tftDc` | GPIO14 | H4-20 | 普通输出 |
| `spiMiso` | unused | — | 屏只写 |

ST7735S datasheet 的 128RGB×160 模式给出的可见列/行范围是 0..127 / 0..159；项目因此
把 **portrait 128×160** 作为唯一 UI 逻辑坐标：

```text
width=128
height=160
rotation=0
xOffset=0
yOffset=0
SPI write=10 MHz
```

4-wire serial write 最小时钟周期 66 ns，对应约 15.15 MHz 理论上限；产品继续固定
10 MHz。ESP32-S3 + TFT_eSPI 2.5.43 构建必须定义 `USE_FSPI_PORT`，使 direct-register
路径使用 SPI2 (`SPI_PORT=2`)；这与 GPIO10–14 的接线是两个不同层面的约束，都必须满足。

任何页面只能使用 `tft.width()/tft.height()` 推导布局。若实板发现固定整体平移，只能在
确认具体玻璃/模块 init variant 后统一修改 `tftXOffset/tftYOffset`；禁止 screen 层各自
加 magic offset。

## 4. 人机输入：4 按键 + EC11

| 信号 | GPIO | 排针 | 备注 |
|---|---:|---|---|
| `keyUp` | GPIO47 | H5-17 | 按下接地 + 内部上拉 |
| `keyDown` | GPIO48 | H5-16 | 同上 |
| `keyBack` | GPIO41 | H5-7 | JTAG 组 MTDI/MTMS 域，本固件不用 pad JTAG |
| `keyOk` | GPIO42 | H5-6 | 同上 |
| `encA` | GPIO38 | H5-10 | `CHANGE` 中断 |
| `encB` | GPIO39 | H5-9 | `CHANGE` 中断；JTAG 组 |
| `encSw` | GPIO40 | H5-8 | 编码器按键；JTAG 组 |

按键继续使用 25 ms 稳定消抖、450 ms 首次长按、110 ms 连发。编码器不再使用“只中断 A、
瞬时读取 B”的脆弱判向；`input.cpp` 对 A/B 两相完整 2-bit Gray 状态转移解码，合法相邻边沿
累计，反向 bounce 自动抵消，非法双比特跳变清掉半格累计；完整一个配置 detent 才向 UI
发布一个 `EncInc/EncDec`。host test 必须覆盖 CW、CCW、接点 bounce、非法跳变。

EC11 系列不同料号存在不同 pulse/detent 组合，因此“每机械格对应多少 Gray transition”最终
仍需以实物料号/手感测试确认。当前固件保持与此前 `ENC_COUNTS_PER_DETENT=2` 所表达的
“每机械格完整一组 A 升/降沿”语义一致；若实物确认是 30 detent / 15 pulse 型，只允许在
输入层统一调整 detent 归一化，不允许各 screen 自行补偿。

## 5. BLE 与测量硬件互斥

BLE 不占额外外部 GPIO，但会占 CPU/heap/controller 资源。产品 invariant：

- 测量/Signal Generator 激励活动期间 `RadioState == Off`；
- sweep StopTone completion + seal 后，用户确认才允许 BLE init/advertising；
- `RadioManager` 是唯一 BLE owner，主 `loop()` 是唯一 `radio.poll()` pump；
- stop 使用 `BLEDevice::deinit(false)`，禁止 `deinit(true)` 后再重启 BLE；
- BLE callback 只写 atomic mailbox，GATT/advertising/deinit 在主 loop 状态机处理。

这条资源互斥与 GPIO 冲突检查同等重要：GPIO 没冲突并不意味着测量期间可以开启 BLE。

## 6. 代码/文档同步规则

硬件事实发生变化时，必须同时检查：

1. 对应 `DO_NOT_TOUCH_*` 真源（仅硬件团队明确变更时才允许改）；
2. `board_profile.cpp`；
3. `tools/static_check.sh` Gate F；
4. `tools/build_check.sh` TFT compile flags；
5. 本文与 `ino/README.md`；
6. `ino/plan.md` 的实板验收项。

DNT manifest 只在真实硬件定义被批准修改时用 `DNT_UPDATE_MANIFEST=1` 更新；普通 UI/BLE
修复绝不能重写 manifest 来掩盖 DNT 意外变化。

## 7. 实板验收清单

自动 CI 通过后，至少完成：

- [ ] GPIO10–14 到 TFT 逐线 continuity；上电显示完整 128×160 portrait 边框/color-bar；
- [ ] 编码器慢速/快速 CW、CCW 各 50 格：一格一事件，无多跳/反跳/明显漏格；
- [ ] Component F0 可设并实际测到 10 Hz；标准 R/C/L 与 L+DCR 结果可重复；
- [ ] One-Port / Two-Port sweep 中 BLE 始终 Off，StopTone 后才 sealed；
- [ ] Two-Port TFT 同时显示 gain dB 与 phase degree，网站同一 CSV 语义一致；
- [ ] Signal Generator 从主菜单第 4 项直接进入，10 Hz / 1 kHz / 10 kHz start/stop；
- [ ] One-Port / Two-Port 交替至少 20 次 BLE session，含断连与 RESTART_TRANSFER，无 panic；
- [ ] 30 分钟 sweep/cancel/BLE/idle soak，无单调 heap/PSRAM 泄漏或异常复位。

更完整的根因、接口约束和验收矩阵见 `ino/plan.md`。
