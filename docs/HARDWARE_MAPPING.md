# 硬件映射 — ESP32-S3 自制板（v4.1.0：DNT 测量链 + UI 外设）

> 状态：**v4.1.0 重构后的硬件真源文档。** 测量链 GPIO 以
> `ino/LCR_UI/DO_NOT_TOUCH_*.h` 为最终依据（实板验证、不可修改）；
> UI 外设（TFT/按键/编码器）以本文 + `board_profile.cpp` 为依据。
> TFT 新映射（GPIO4/5/6/7/21）**待实物 continuity 确认**（§5 清单）。

> 真源文件：
> - 测量链：`DO_NOT_TOUCH_lcr_adc.h` / `DO_NOT_TOUCH_sinwave.h` /
>   `DO_NOT_TOUCH_lcr_measure.h`（引脚宏直接取自这些文件，见 §1）
> - 板卡：`ino/databook/自制开发板资料/.../开发板原理图.pdf`、`开发板手册.pdf`
> - 芯片：`esp32-s3_datasheet_cn.pdf`、`esp32-s3_technical_reference_manual_cn.pdf`
> - 屏：`TFT_ST7735S_Sitronix_datasheet_mirror (1).pdf`；编码器：`编码器数据手册 (1)/(2).PDF`

## 0. 架构与硬件事实（v4.1.0 重构的出发点）

测量硬件是一条**已在实板验证的不可变链**（详见 `plan.md`）：

```
LCD_CAM 外设 → 8 个 GPIO 输出并行正弦码 → 外部电阻网络 DAC → 模拟正弦
3 个 GPIO → 74HC595（串行移位）→ 10 个控制端：
    4×TIA 放大倍数、2×电压放大、2×电流放大、2×双端口模式
模拟链处理后 → 两个 ADC 引脚（间隔采样）→ DNT 计算与校准 → Z/H 结果
```

- 本项目**没有**外部 DAC 芯片（不存在 PCM5102 等）；并行电阻网络即 DAC。
- 除 74HC595 外**没有**任何其它外部数字芯片；没有外部同步 ADC。
- 应用层不触碰上述任何 GPIO / 外设 —— 一切经 `DO_NOT_TOUCH_lcr_api.h`
  的公开 API（唯一入口 `lcr_api.cpp`，CI Gate C 强制）。

## 1. 测量链 GPIO（DNT 保留，应用层/CI 一律禁用）

| 功能 | GPIO | 定义处（不可修改） |
|---|---:|---|
| ADC 电压通道 | GPIO2 / ADC1_CH1 | `DO_NOT_TOUCH_lcr_adc.h`（LCR_ADC_CH_A） |
| ADC 电流通道 | GPIO1 / ADC1_CH0 | `DO_NOT_TOUCH_lcr_adc.h`（LCR_ADC_CH_B） |
| LCD_CAM D0 | GPIO18 | `DO_NOT_TOUCH_sinwave.h`（PIN_D0） |
| LCD_CAM D1 | GPIO8 | `DO_NOT_TOUCH_sinwave.h`（PIN_D1） |
| LCD_CAM D2 | GPIO9 | `DO_NOT_TOUCH_sinwave.h`（PIN_D2） |
| LCD_CAM D3 | GPIO10 | `DO_NOT_TOUCH_sinwave.h`（PIN_D3） |
| LCD_CAM D4 | GPIO11 | `DO_NOT_TOUCH_sinwave.h`（PIN_D4） |
| LCD_CAM D5 | GPIO12 | `DO_NOT_TOUCH_sinwave.h`（PIN_D5） |
| LCD_CAM D6 | GPIO13 | `DO_NOT_TOUCH_sinwave.h`（PIN_D6） |
| LCD_CAM D7 | GPIO14 | `DO_NOT_TOUCH_sinwave.h`（PIN_D7） |
| 74HC595 SRCLK | GPIO15 | `DO_NOT_TOUCH_lcr_measure.h`（HC595_PIN_SRCLK） |
| 74HC595 SER | GPIO16 | `DO_NOT_TOUCH_lcr_measure.h`（HC595_PIN_SER） |
| 74HC595 RCLK | GPIO17 | `DO_NOT_TOUCH_lcr_measure.h`（HC595_PIN_RCLK） |

合计保留集：**{1, 2, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18}**。
`tools/static_check.sh` Gate F 自动检查 UI 引脚不得落入该集合与受限集。
DNT 分析频段 10 Hz–20.8 kHz（`DO_NOT_TOUCH_EXAMPLE` FREQ_MAX_HZ=20800）；
产品声明频段 10 Hz–10 kHz（更高频段属独立测试，不自动扩大声明）。

ADC task-affinity 约束（重要）：DNT 的 ADC ISR 通知 `lcr_api_init()` 时
保存的 FreeRTOS task —— 因此 **init 与全部测量必须在同一个 Worker task**
（`lcr_api.cpp`；详见 `lcr_api.h` 文件头）。

## 2. 其它不占用引脚（ESP32-S3 / N16R8 限制）

| 引脚 | 原因 |
|---|---|
| GPIO0 / 3 / 45 / 46 | strapping（BOOT 键 / JTAG 源 / 启动模式 / ROM 日志） |
| GPIO19 / 20 | USB D-/D+ 网络（本板 Micro USB 经 CH340X 隔离，仍属 USB 专用域） |
| GPIO26–32 | 模组内 Octal Flash/PSRAM，未引出 |
| GPIO33–37 | N16R8 八线 PSRAM 高 4 位占用（手册虽标"空闲"，以数据手册为准） |
| GPIO43 / 44 | UART0 → 隔离 → CH340X（烧录/日志；Arduino 须 CDCOnBoot=Disabled） |

## 3. UI 外设 — 显示（ST7735S 4-wire SPI，经 H4/H5 排针外接）

**v4.1.0 起 TFT 完全移出 GPIO10–14**（旧映射 SCK12/MOSI11/CS10/DC14/RST13
与 LCD_CAM 并行 DAC 总线直接硬件冲突，不可使用）。

新映射（`plan.md` §10.4 候选接法，**待实物 continuity 确认**）：

| 信号 | GPIO | 排针 | 备注 |
|---|---:|---|---|
| `spiSck` | GPIO4 | H4-4 | 旧 ADC 计划已废止（测量 ADC 固定 GPIO1/2） |
| `spiMosi` | GPIO5 | H4-5 | 同上 |
| `tftCs` | GPIO6 | H4-6 | |
| `tftDc` | GPIO7 | H4-7 | |
| `tftRst` | GPIO21 | H5-18 | |
| `spiMiso` | — | — | 屏幕只写，不接 |

- ST7735S 数据手册：4-wire 串行写时钟周期 ≥ 66 ns（上限 ~15.15 MHz），
  首版 `tftSpiHz = 10 MHz`；升频前必须逻辑分析仪 + 实屏压力测试。
- `tftXOffset/tftYOffset/tftInvert` 属实板面板属性，color-bar 实测后只改
  `board_profile.cpp`。
- 编译期引脚同步注入 `tools/build_check.sh` 的 TFT_FLAGS（与 kBoard 一致）。

## 4. UI 外设 — 人机输入（4 按键 + EC11 编码器，经 H5 外接）

| 信号 | GPIO | 排针 | 备注 |
|---|---:|---|---|
| `keyUp` | GPIO47 | H5-17 | 纯数字脚，按下接地 + 内部上拉 |
| `keyDown` | GPIO48 | H5-16 | |
| `keyBack` | GPIO41 | H5-7 | JTAG 组（MTMS），本固件不启用 JTAG |
| `keyOk` | GPIO42 | H5-6 | JTAG 组（MTDI） |
| `encA` | GPIO38 | H5-10 | A 相双边沿中断 |
| `encB` | GPIO39 | H5-9 | JTAG 组（MTCK） |
| `encSw` | GPIO40 | H5-8 | JTAG 组（MTDO） |

与测量 GPIO 无冲突（软件上可保留原接线）。GPIO39–42 属 JTAG 默认组：
**产品固件不能同时依赖 pad JTAG 调试**（`plan.md` §10.3）。

## 5. 实板验收 checklist（软件侧已验收，以下待实测）

软件侧（host 单测 + ESP32-S3 生产编译 + 静态门禁 A–H）已在 CI 全部通过。
以下条目需要真实硬件，逐项执行后在本文打勾（`plan.md` §17.3 / Phase D）：

- [ ] TFT 新映射 GPIO4/5/6/7/21 逐脚 continuity（表笔核对排针到屏）；
- [ ] 上电 smoke：屏点亮、color-bar 无偏移（否则只改 board_profile 偏移字段）；
- [ ] 按键/编码器逐脚 continuity + 功能确认；
- [ ] Worker 初始化 DNT 成功不死锁（`LCR CORE INIT...` -> 主菜单 <15 s）；
- [ ] normal mode 串口测量路径静默（diagnostics off）；
- [ ] 标准 R / C / L（含 L+DCR、负 DCR 告警路径）重复测量；
- [ ] 10 Hz–10 kHz 单端口产品扫频 → seal → BLE → 网站拟合全链路；
- [ ] 双端口扫频 → BLE → Bode/Nyquist；已知网络 H 趋势正确；
- [ ] LCD_CAM 工作时 TFT 不乱屏（测量期间无大块刷新）；
- [ ] 测量期间输入仍响应；取消在当前 2/3 点块后停止并显示 STOPPING；
- [ ] cancel / 正常结束均确认 tone stop（示波器看 DAC 输出归零）；
- [ ] 测量全过程 BLE Off；seal 后才能 advertising；
- [ ] 网站 CRC 与设备 CRC 完全一致；≥20 次连接/断开无泄漏；
- [ ] 校准状态显示与 `lcr_api_cal_status` 真实一致。

## 6. 修改规则

1. 测量链 GPIO 永远不改（DO_NOT_TOUCH 文件 + Gate A manifest 锁定）；
2. UI 接线变化只改 `ino/LCR_UI/board_profile.cpp` + 本文档 +
   `tools/build_check.sh` 的 TFT_FLAGS；
3. 任何新 UI 引脚不得落入 §1 保留集与 §2 受限集（Gate F 自动拦截）。
