# LCR_UI — ESP32-S3 LCR 仪表固件（v4.1.0）

面向自制 ESP32-S3 开发板（WROOM-1-**N16R8**）的完整测量固件：TFT 本地
仪表 + 三个产品模式 + BLE GATT v1 上传。硬件引脚真源见
`docs/HARDWARE_MAPPING.md`；协议契约见 `protocol/BLE_PROTOCOL_V1.md` 与
`protocol/CSV_SCHEMA_V1.md`。

## 顶层三个用户模式

| 模式 | 内容 | BLE |
|---|---|---|
| 1 Component R/C/L | 5 个几何频点 → R/C/(L+DCR) 三模型加权比较，不确定时明确 UNKNOWN / OUT OF RANGE | 不启动 |
| 2 One-Port Z Sweep | 单端口阻抗扫频 → 停激励/释放 ADC → dataset seal → `f,re,im` CSV | 封存后用户确认才开启 |
| 3 Two-Port H Sweep | 双端口扫频，真源为复数 H = Vout/Vin → `f,re_h,im_h` CSV | 同上 |

隐藏诊断页（信号发生器）：主菜单 3 秒内连按 3 次 `Up` 进入；仅调试用途。

## 目录

```
LCR_UI/
  fw_version.h           固件/协议/schema 版本（单一出处）
  board_profile.h/.cpp   引脚矩阵与电气常数（唯一允许裸 GPIO 的文件）
  measurement_types.*    测量类型层（host 可编译）
  dsp_fit.*              三参数正弦拟合（SampleSeries 真实时间基准）
  sine_plan.*            I2S 精确有理频率规划（actualHz 按构造回读）
  excitation_driver.*    I2S→PCM5102A 激励 + 前端使能/量程 GPIO
  adc_capture.*          ADC continuous(DMA) 采集：解交错 + raw→mV 校准
  calibration.*          前端复增益/相位校准（log-f 插值，带外拒绝）
  measurement_engine.*   非阻塞单点测量状态机（依赖注入，host 可测）
  sweep_engine.*         扫频状态机：逐点推进 → 静默保护 → seal
  component_meter.*      R/C/(L+DCR) 分类器（统一相对 WRMSE + gap 判据）
  dataset.*              封存数据集 + canonical CSV + CRC32 + metadata JSON
  ble_protocol.h         GATT v1 常量与帧编解码（host 可测）
  radio_manager.*        BLE 生命周期 + 分片发送（radio_lock 互斥）
  radio_lock.*           「测量窗口内射频静默」invariant
  input/display/screens/plot + screen_*.cpp   UI 框架与界面
test/                    host 单测（g++，无 Arduino 依赖）+ golden fixture 生成
tools/build_check.sh     arduino-cli ESP32-S3 编译门禁
tools/run_tests.sh       host 单测 + golden CSV 生成
tools/static_check.sh    grep 门禁（BluetoothSerial=0 / GPIO 集中 / 无 stubs）
tools/bt_bridge.py       （Deprecated）旧 Classic BT→HTTP 桥，正常路径不使用
```

## 构建与烧录

```sh
bash ino/tools/build_check.sh     # CI 式编译（不烧录）
bash ino/tools/static_check.sh    # 静态门禁
bash ino/tools/run_tests.sh       # host 单测（纯逻辑模块）
```

- FQBN：`esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=default,
  PartitionScheme=app3M_fat9M_16MB`（脚本内固定）。
- **USB CDC On Boot 必须 Disabled**：本板 Micro USB 经 CH340X 隔离接
  GPIO43/44；开启 CDC 会把 Serial 引到 GPIO19/20 导致串口无输出。
- TFT_eSPI 引脚经编译期 `-D` 注入（ST7735S，10 MHz，参数 = kBoard）。
- 烧录速率 115200；一键下载电路兼容 Arduino 默认 RTS/DTR 时序。

## 关键设计规则（违反即 bug）

1. **射频互斥**：启动不初始化 BLE；扫频全程 RadioState=Off；seal 后才
   `startBleForSealedDataset()`。已连接时开始新测量必须先 disconnect→
   deinit→确认 Off。debug 构建轮询断言（radio_lock）。
2. **非阻塞**：`MeasurementEngine`/`SweepEngine` 均为 poll-driven 状态机，
   `poll()` 有上界；SETTLING 用周期截止点，不用 `delay()`；Cancel 到
   safe-off 的目标延迟 <100 ms。ISR 只置 flag。
3. **actualHz 全链路**：激励频率由 `sine_plan` 有理构造（I2S Fs·K/L），
   sine fit 与 CSV 一律使用 actualHz；偏差 >1% 判 FrequencyMismatch。
4. **通道 skew**：同一 ADC1 pattern 交错采样，`SampleSeries.t0` 承载
   确定性 skew，拟合按真实时间基准（Δφ = 2πfΔt 显式补偿）。
5. **失败不伪造**：质量失败的点不进 CSV（诊断保留）；DMA overflow →
   AdcOverrun，残缺 buffer 不拟合。
6. **GPIO 集中**：业务文件禁止裸 GPIO 数字（static_check.sh 门禁）；
   修改接线只改 `board_profile.cpp`（同步 build_check.sh 的 TFT_FLAGS）。
7. **两层校准**：`adc_cali`（MCU ADC 传递）与 `CalibrationProfile`
   （前端复增益/相位）绝不能混淆；v1 出厂恒等 profile（`factory-none`）。

## 测量链路（模式 2/3 数据流）

```
I2S(PCM5102A) 激励 ──► 外接 LCR 前端 ──► ADC1 双通道交错采样(DMA)
   │ sine_plan 精确 f                     │ adc_cali raw→mV
   ▼                                      ▼
ExcitationState.actualHz ──────► sineFitTimed（真实时间基准）
                                          ▼
                       复校准 Cv/Ci 或 Cin/Cout → Z=V/I 或 H=Vout/Vin
                                          ▼
             SweepEngine 逐点 → dataset seal → canonical CSV + CRC32
                                          ▼（用户确认）
                  RadioManager/BLE GATT v1 ──► 浏览器 parseZCsv/parseHCsv
```

## RAM/Flash 预算（v1 实测）

- Sketch 698 KB / 3 MB APP 分区（22%）；
- 静态 BSS 224 KB / 320 KB（68%，主要是两份 dataset 静态存储 + ADC 缓冲），
  剩余堆供 NimBLE；257 点上限由此与 `SWEEP_MAX_POINTS` 约束。

## 已知限制（实板待冻结项，见 HARDWARE_MAPPING §6）

- 内部 ADC 双通道交错上限 ~41.7 ksps/ch → **双通道质量保证频段约
  10–2000 Hz**（`DUAL_CHANNEL_QUALITY_FMAX_HZ`）；更高频段可测但质量
  下降、UI 标注。若实板验证不足 → 换外部同步 ADC，上层接口不变。
- 激励幅度固定（calibrated nominal drive ≈1.05 Vrms @DAC 半幅），
  不提供虚假的“固定电压设定”。
- ST7735S 偏移/极性默认 BLACKTAB(0,0)；实屏 color-bar 测试后只在
  `board_profile.cpp` 修改。
- §9.5/§9.7 的工程精度目标（R≤2%/C≤3%/L≤5%、gain≤0.5dB、phase≤3°）
  需实板标准件标定后冻结；达不到时不放宽代码阈值，而记录 uncertainty
  后由硬件指标决定。
