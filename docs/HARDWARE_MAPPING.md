# 硬件映射 — ESP32-S3 自制板（BoardProfile 真源文档）

> 状态：**v4.1.0 按「原理图 + 开发板手册 + 数据手册」逐条核对完成；连续性 /
> 上电 smoke test 待实板执行**（清单见 §6）。若实测与本文冲突，先改本文，
> 再改 `ino/LCR_UI/board_profile.cpp`（唯一允许出现裸 GPIO 的业务文件）。
>
> 本文档依据的仓库内真源：
> - `ino/databook/自制开发板资料/.../开发板原理图.pdf`（3 页）
> - `ino/databook/自制开发板资料/.../开发板手册.pdf`（排针表 / 受限引脚）
> - `ino/databook/esp32-s3_datasheet_cn.pdf`、`esp32-s3-wroom-1_wroom-1u_datasheet_cn.pdf`
> - `ino/databook/TFT_ST7735S_Sitronix_datasheet_mirror (1).pdf`
> - `ino/databook/编码器数据手册 (1)/(2).PDF`（EC11 类旋转编码器）
> - `ino/databook/Button_6x6x5_B3F_Omron_datasheet.pdf`（轻触按键）

## 1. 板卡概述

课程自制 ESP32-S3 开发板（9.61 × 9.79 cm），三区域结构：

| 区域 | 内容 | 与本仪器的关系 |
|---|---|---|
| 电源 | Type-C（PD）/ DC5521 双输入，JW5357 降压 → 5V/3.3V，AMS1117-3.3 供核心板 | 仪器供电 |
| 电机驱动 | L298N 双 H 桥（IN1-4/EA/EB 经 H6/H7/H8 引出） | **不使用** |
| 核心板 | ESP32-S3-WROOM-1-**N16R8**（16MB Flash / 8MB **八线** PSRAM） | 主控 |
| 隔离区 | Micro USB → CH340X → ISO7722/ISO7720 → UART0，一键下载 | 烧录/日志，**无 JTAG** |

**关键事实：本板没有板载 TFT、按键（仅 RST/BOOT）、编码器或 LCR 模拟前端。**
仪器的全部外设（ST7735S 屏、4 按键、EC11 编码器、激励 DAC、LCR 前端）
都通过 H4/H5 排针外接；`BoardProfile` 声明的就是这套外接布线。

Micro USB 的 D+/D- 接 CH340X（不直连 GPIO19/20），因此：
- **板载 JTAG 不可用**；GPIO19/20 虽引出（H5-20/19）但网络属 USB 专用，不用。
- Arduino 烧录参数须 **USB CDC On Boot = Disabled**（否则 Serial 走
  GPIO19/20 而本板串口在 GPIO43/44），见 `ino/tools/build_check.sh`。

## 2. 排针引脚表（开发板手册 §3.3.4，与原理图核对一致）

### H4（左排）

| 脚 | 网络 | 板上用途 | 备注 |
|---|---|---|---|
| 1/2 | 3V3 | 3.3V 输出 | 外设供电 |
| 3 | CHIP_PU (EN) | RST 键 SW2 | 不用 |
| 4 | GPIO4 | 空闲 | ADC1_CH3 |
| 5 | GPIO5 | 空闲 | ADC1_CH4 |
| 6 | GPIO6 | 空闲 | ADC1_CH5 |
| 7 | GPIO7 | 空闲 | ADC1_CH6 |
| 8 | GPIO15 | 空闲 | ADC2_CH4 |
| 9 | GPIO16 | 空闲 | ADC2_CH5 |
| 10 | GPIO17 | 空闲 | ADC2_CH6 / U1TXD 备用 |
| 11 | GPIO18 | 空闲 | ADC2_CH7 / U1RXD 备用 |
| 12 | GPIO8 | 空闲 | ADC1_CH7 |
| 13 | GPIO3 | 空闲 | **strap（JTAG 源）** |
| 14 | GPIO46 | 空闲 | **strap（启动模式）** |
| 15 | GPIO9 | 空闲 | ADC1_CH8 |
| 16 | GPIO10 | 空闲 | ADC1_CH9 / FSPICS0 |
| 17 | GPIO11 | 空闲 | ADC2_CH0 / FSPID |
| 18 | GPIO12 | 空闲 | ADC2_CH1 / FSPICLK |
| 19 | GPIO13 | 空闲 | ADC2_CH2 / FSPIQ |
| 20 | GPIO14 | 空闲 | ADC2_CH3 / FSPIWP |
| 21 | VCC_5V | 5V 输出 | — |
| 22 | GND | 地 | — |

### H5（右排）

| 脚 | 网络 | 板上用途 | 备注 |
|---|---|---|---|
| 1 | GND | 地 | — |
| 2 | GPIO43 (U0TXD) | 串口 TX → 隔离 → CH340X | 烧录/日志 |
| 3 | GPIO44 (U0RXD) | 串口 RX | 同上 |
| 4 | GPIO1 | 空闲 | ADC1_CH0（预留 I2C） |
| 5 | GPIO2 | 空闲 | ADC1_CH1（预留 I2C） |
| 6 | GPIO42 | 空闲（JTAG 组） | 默认 MTDI |
| 7 | GPIO41 | 空闲（JTAG 组） | 默认 MTMS |
| 8 | GPIO40 | 空闲（JTAG 组） | 默认 MTDO |
| 9 | GPIO39 | 空闲（JTAG 组） | 默认 MTCK |
| 10 | GPIO38 | 空闲 | FSPIWP 备用 |
| 11 | GPIO37 | 空闲 | **N16R8 八线 PSRAM 占用，不可用** |
| 12 | GPIO36 | 空闲 | **N16R8 八线 PSRAM 占用，不可用** |
| 13 | GPIO35 | 空闲 | **N16R8 八线 PSRAM 占用，不可用** |
| 14 | GPIO0 | BOOT 键 SW1 | **strap** |
| 15 | GPIO45 | 空闲 | **strap（VDD_SPI）** |
| 16 | GPIO48 | 空闲 | 纯数字 |
| 17 | GPIO47 | 空闲 | 纯数字 |
| 18 | GPIO21 | 空闲 | 纯数字 |
| 19 | GPIO20 | USB_D+ | USB 专用网络，不用 |
| 20 | GPIO19 | USB_D- | 同上 |
| 21/22 | GND | 地 | — |

> 注：手册 H5 表把 GPIO35/36/37 标为「空闲」，但手册 §3.3.2 与模组数据手册
> 均说明八线 PSRAM（N16R8 的 R8）模式下 GPIO33–37 被占用。以数据手册为准，
> 本仪器**不使用**这三脚。同理 GPIO26–32（模组内 Flash/PSRAM）未引出。

## 3. BoardProfile 交叉核对表（仪器外设接线）

每行都给出：排针位 → GPIO → 数据手册能力 → 用途与理由。

### 3.1 模拟通道（4 路全部 ADC1）

| 信号 | GPIO | 排针 | ADC | 核对 |
|---|---|---|---|---|
| `adcVoltageGpio`（DUT 电压 V） | GPIO4 | H4-4 | ADC1_CH3 | ✅ 手册 ADC 表 |
| `adcCurrentGpio`（电流感测电压） | GPIO5 | H4-5 | ADC1_CH4 | ✅ |
| `adcPort2InputGpio`（双端口 Vin） | GPIO6 | H4-6 | ADC1_CH5 | ✅ |
| `adcPort2OutputGpio`（双端口 Vout） | GPIO7 | H4-7 | ADC1_CH6 | ✅ |

- **全部落在 ADC1**：ESP32-S3 ADC2 的 DMA continuous 模式受稳定性限制，
  且 Wi-Fi 开启即失效；本项目测量期间射频静默但仍不依赖 ADC2
  （plan.md §2.2 明令禁止「有 ADC1+ADC2 ⇒ 双 ADC 同步 DMA」的推导）。
- 同一 ADC1 pattern 内两路是**交错采样**：V/I 相邻样本存在确定性时差
  （= 1/总采样率），正弦拟合按 `Δφ = 2πfΔt` 显式补偿（见
  `measurement_types.h` 的 `SampleSeries` 通道时差模型）。
- 内部 ADC continuous 模式总采样率上限约 83.3 ksps（数据手册 SAR ADC
  转换率），双通道交错即每通道约 41.7 ksps → **双通道测量质量保证频段
  上限约 2 kHz（≥16 点/周期）**；更高频率仍可测但质量下降并在 UI 标注。
  若实板验证不足，按 plan.md §2.2 换外部同步 ADC，上层接口不变。
- 原始码 → 电压经 ESP-IDF curve-fitting 校准（`adc_cali_*`）；该层只修正
  MCU ADC 传递，不替代前端复增益/相位校准（分层见 `calibration.h`）。

### 3.2 显示（ST7735S 4-wire SPI）

| 信号 | GPIO | 排针 | 备用功能 | 核对 |
|---|---|---|---|---|
| `spiSck` | GPIO12 | H4-18 | FSPICLK | ✅ |
| `spiMosi` | GPIO11 | H4-17 | FSPID | ✅ |
| `tftCs` | GPIO10 | H4-16 | FSPICS0 | ✅ |
| `tftDc` | GPIO14 | H4-20 | FSPIWP | ✅ |
| `tftRst` | GPIO13 | H4-19 | FSPIQ | ✅ |
| `spiMiso` | PIN_UNUSED | — | — | 屏幕只写 |

- ST7735S 数据手册串行接口写时钟周期最小 **66 ns**（理论 ≈15.15 MHz），
  首版 `tftSpiHz = 10 MHz`；升频前必须逻辑分析仪 + 实屏压力测试。
- 128×160 RAM 与模块可见区未必一致：`tftXOffset/tftYOffset/tftInvert`
  属实板属性，默认按常见 BLACKTAB（0,0/不反转）起步，实屏 color-bar /
  边界矩形测试后只在 `board_profile.cpp` 修改（plan.md §2.4 禁止凭
  「ST7735S」字符串写死偏移）。
- 测量 `CAPTURING` 期间不做 TFT 大块刷新，进度更新放在频点间隙。

### 3.3 人机输入（外接，按下接地 + 内部上拉）

| 信号 | GPIO | 排针 | 核对 |
|---|---|---|---|
| `keyUp` | GPIO47 | H5-17 | ✅ 纯数字脚，支持上拉输入 |
| `keyDown` | GPIO48 | H5-16 | ✅ |
| `keyBack` | GPIO41 | H5-7 | ✅ JTAG 组脚，固件未启用 JTAG |
| `keyOk` | GPIO42 | H5-6 | ✅ |
| `encA` | GPIO38 | H5-10 | ✅ |
| `encB` | GPIO39 | H5-9 | ✅ |
| `encSw` | GPIO40 | H5-8 | ✅ |

- 4 按键为 Omron B3F 类 6×6 轻触开关（另一端 GND）；EC11 编码器
  A/B/SW（C 与开关公共端接 GND）。去抖/长按连发由 `input.cpp` 的
  非阻塞扫描处理，固件不把长任务放进输入回调。
- GPIO39–42 上电默认为 JTAG 功能输入；Arduino-ESP32 固件不使能 JTAG，
  直接作 GPIO 输入。烧录后若需 JTAG 调试，必须先改 `board_profile`。

### 3.4 激励源（I2S → 外部 PCM5102A DAC）

| 信号 | GPIO | 排针 | 核对 |
|---|---|---|---|
| `excitationBck` | GPIO17 | H4-10 | ✅ |
| `excitationLrck` | GPIO18 | H4-11 | ✅ |
| `excitationData` | GPIO21 | H5-18 | ✅ 纯数字脚 |
| `excitationMclk` | PIN_UNUSED | — | PCM5102 SCK 接地（内部 PLL） |

- ESP32-S3 **无片上 DAC**（与经典 ESP32 不同）；激励路径选 I2S 标准模式
  → PCM5102A：全 DMA 硬件驱动、频率由 `Fs·K/L` 有理数构造**精确可知**
  （`actualHz` 按构造回读，非估计），每点 sine fit 与 CSV 均用 actualHz。
- 幅度固定（DAC 满量程一半），属 plan.md §2.3 的「硬件只有固定幅度」
  情形：UI 只显示 **calibrated nominal drive**，不提供虚假的电压设定。
  闭环幅度控制留待前端硬件升级（接口已预留 `ExcitationConfig`）。
- Sine 是唯一测量波形；方波/三角只存在于隐藏诊断页（若接入）。

### 3.5 前端控制与电气常数

| 信号 | GPIO | 排针 | 说明 |
|---|---|---|---|
| `frontEndEnablePin` | GPIO8 | H4-12 | 高 = 前端上电；错误/取消路径统一拉低（safe-off） |
| `rangeSelectPins[0]` | GPIO15 | H4-8 | 量程位 0 |
| `rangeSelectPins[1]` | GPIO16 | H4-9 | 量程位 1 |

- 电气常数（硬件事实，非拟合参数）：`nominalCurrentSenseOhm = 100 Ω`
  （v1 参考前端的精密采样电阻）、`nominalTransimpedanceGain = 1.0`
  （无跨阻级）。**必须与实际搭建的前端一致**；残余增益/相位误差由
  `CalibrationProfile` 复数校准层吸收，不改这两个数来"凑"读数。

### 3.6 引脚冲突总检（同一 GPIO 不重复使用）

```
GPIO4,5,6,7        模拟 ADC1 专用        GPIO11,12    TFT SPI
GPIO8,15,16        前端控制              GPIO10,13,14 TFT 控制
GPIO17,18,21       I2S 激励              GPIO38..42   编码器/按键
GPIO47,48          按键                  其余         禁用/预留（见 §2）
```

## 4. 烧录与构建

- FQBN：`esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=default,
  PartitionScheme=app3M_fat9M_16MB`（CDCOnBoot=default = Disabled，本板
  串口在 GPIO43/44；PSRAM=opi 对应 N16R8 八线 PSRAM）。
- TFT_eSPI 引脚经编译期 `-D` 注入（`USER_SETUP_LOADED` + `ST7735_DRIVER` +
  引脚/偏移/频率宏），见 `ino/tools/build_check.sh`；与 `kBoard` 一致。
- 上传速率 115200；一键下载电路兼容 Arduino 默认 RTS/DTR 时序。

## 5. 供电与接地注意（测量质量相关）

- 5V/3.3V 由 JW5357 开关降压产生；模拟前端建议独立 LDO 供电并在
  `frontEndEnable` 控制下上电，避免开关噪声直接进 ADC 通道。
- ADC 通道走线（H4-4..7 相邻四脚）尽量短、远离 I2S/TFT 时钟线。
- 测量期间（CAPTURING）BLE/Wi-Fi 射频完全关闭（RadioState=Off 是
  硬 invariant），TFT 刷新只发生在频点间隙。

## 6. 实板验证清单（冻结 BoardProfile 前必须完成）

1. **连续性**：H4/H5 每个使用脚 ↔ 外设模块对应脚通断；GND 共地。
2. **上电 smoke**：3V3/5V 电压；前端 enable 前/后电流合理。
3. **TFT**：color-bar 与边界矩形全屏可见（确认 offset/inversion/rotation，
   只改 board_profile.cpp）；10 MHz 下长时间无花屏。
4. **按键/编码器**：每个键按下事件、编码器双向与按压；无抖动误触发。
5. **I2S DAC**：示波器看 BCK/LRCK/DATA；DAC 输出正弦频率/幅度与
   `ExcitationState` 一致；stop 后输出静音。
6. **ADC**：已知直流电平双通道读数；1 kHz 已知正弦的双通道幅相
   （与台式表/示波器对比）；故意 DMA overflow → `ADC_OVERRUN`。
7. **射频隔离**：BLE 广播/连接窗口内 ADC 静默；测量窗口内电流无射频尖峰。
8. ** straps**：确认外设接线不拉低 GPIO0/3/45/46（否则无法启动/烧录）。
