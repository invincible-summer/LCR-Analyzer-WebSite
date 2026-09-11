# LCR Analyzer ESP32-S3 + BLE + Web 完整实施计划

> **实施基线**：`invincible-summer/LCR-Analyzer-WebSite`，`main@b2b6c6b9c1e0670cd7c22c8380df19a2a268a89c`（2026-09-11）。后续开发只在 `main` 的新提交上推进；实施前若 `main` 再发生变化，先做一次基线 diff，再按本计划重新确认受影响接口。
>
> **目标**：把当前 `ino/LCR_UI` 从“UI + DSP + 合成 stub + Classic Bluetooth 原型”收敛为可直接烧录到 ESP32-S3 的完整测量固件，并与现有 Vue/WASM 网站形成一套确定的数据契约。最终 TFT 顶层只有三个用户模式：**单元件 R/C/L 自动识别与测量**、**单端口阻抗扫频并在采样完成后 BLE 上传网站拟合**、**双端口扫频并在网站显示曲线**。

## 1. 当前代码审计结论与修改边界

当前代码里有一部分基础设施已经足够好，应保留而不是重写；真正需要重构的是硬件采集、测量状态机、蓝牙协议与三个产品模式的语义。

### 1.1 保留并继续使用的实现

- `ino/LCR_UI/input.*`：按键/编码器轮询和去抖是非阻塞的，继续作为唯一输入事件源；不把长任务塞进输入回调。
- `ino/LCR_UI/screens.*` / `ScreenManager`：当前 screen stack/事件分发框架可继续使用。只替换顶层菜单和三个业务 screen，不重做 UI 框架。
- `ino/LCR_UI/display.*`：保留作为屏幕抽象层，但硬件配置从旧 ILI9341 改成实际 ST7735S 板卡 profile；业务代码不得直接散落 `TFT_eSPI` 初始化参数。
- `ino/LCR_UI/dsp_fit.cpp`：已知频率三参数正弦拟合的核心数学方向正确，继续作为首版波形到 phasor 的核心；后续增加质量指标、时间戳/通道时差支持和校准，而不是另写一套 FFT 路径。
- `frontend/src/lib/csv.ts` 和 `ZPoint`：网站现有拟合真源已经是 `f,re,im`，BLE 导入必须复用这条解析链路，不能再创建第二套“蓝牙专用拟合格式”。
- 现有 WASM Try1/Try2/Try3 拟合链路：本次不改变算法语义。ESP32 负责采集和生成规范测量点，网站继续负责拟合。

### 1.2 必须替换或重构的实现

- `bt_link.*` 当前使用 `BluetoothSerial`/Classic SPP；ESP32-S3 目标必须替换为 **BLE GATT**。旧 `bt_bridge.py -> HTTP` 不再作为正常路径。
- `LCR_UI.ino` 当前启动时立即 `bt.begin()`；这与“测量期间关闭蓝牙，扫频全部结束后才开启 BLE”冲突。最终固件启动时不初始化 BLE。
- `partner_api.h` 当前 `measureImpedanceAtFreq()` 是“一次调用阻塞到采样结束”的接口；必须改成可取消、可逐 tick 推进的异步测量引擎。
- `partner_stubs.cpp` 当前是 `generateWave()`/`measureImpedanceAtFreq()` 的唯一实现之一，且产生合成数据。生产构建必须保证这些 weak stub 不参与链接；它们只保留给 PC/unit/simulator 测试。
- `screen_sweep.cpp` 当前在 `startSweep()` 时就发送蓝牙 start，并在每个频点 `sendPoint()`；最终实现必须改成 **完整 sweep -> 停止采集/激励 -> 封存 dataset/CSV -> 才初始化 BLE -> 再传输**。
- 当前主菜单“Signal Generator / Single Freq / Sweep”改为三个产品模式。信号发生器若仍有调试价值，只能放到隐藏 diagnostics/developer 页面，不占顶层入口。
- `hw_config.h` 当前默认 classic ESP32 DevKit + ILI9341 + 老 GPIO。它不能继续作为真实硬件真源；改成 ESP32-S3 自制板的 `BoardProfile`，所有 GPIO/ADC/SPI/前端量程参数必须由原理图和实板确认后落地。
- `build_check.sh` 当前目标是 `esp32:esp32:esp32`；生产编译目标改为 ESP32-S3 FQBN，并固定 Arduino-ESP32/TFT 库版本。

### 1.3 当前尚不存在、必须新增的底层能力

当前 `main` 没有真正的 ADC continuous/DMA 采集、真实激励源驱动和前端校准实现。最终工程至少新增：

```text
ino/LCR_UI/
  board_profile.h/.cpp
  measurement_types.h
  excitation_driver.h/.cpp
  adc_capture.h/.cpp
  calibration.h/.cpp
  measurement_engine.h/.cpp
  component_meter.h/.cpp
  sweep_engine.h/.cpp
  dataset.h/.cpp
  csv_export.h/.cpp
  ble_protocol.h
  ble_transfer.h/.cpp
  screen_component.h/.cpp
  screen_oneport.h/.cpp
  screen_twoport.h/.cpp
```

`partner_api.h` 可以在迁移期保留为 compatibility facade，但最终 screen 层只能调用 `MeasurementEngine`/`SweepEngine`，不能直接操作 ADC、BLE 或正弦拟合器。

## 2. 硬件真源、ESP32-S3 和 ST7735S 的实现约束

本仓库已经加入 ESP32-S3 datasheet/TRM、ST7735S datasheet、自制开发板原理图/PCB/Altium 资料。实现阶段第一件事不是写 GPIO，而是建立一张 **BoardProfile pin/function matrix**，逐条从原理图核对。当前 GitHub 文本接口无法可靠解析二进制原理图，所以本计划不会把旧 `hw_config.h` 中的 GPIO 5/2/4、18/19/23、32/33/25/26 等错误地当成新板事实。

### 2.1 `BoardProfile` 是唯一硬件配置入口

定义：

```cpp
struct BoardProfile {
  // Display
  int tftCs;
  int tftDc;
  int tftRst;
  int spiSck;
  int spiMosi;
  int spiMiso;      // 若屏幕只写则允许 PIN_UNUSED
  uint32_t tftSpiHz;
  uint16_t tftWidth;
  uint16_t tftHeight;
  int16_t tftXOffset;
  int16_t tftYOffset;
  uint8_t tftRotation;

  // Human input
  int keyUp, keyDown, keyBack, keyOk;
  int encA, encB, encSw;

  // Analog paths
  int adcVoltageGpio;
  int adcCurrentGpio;
  int adcPort2InputGpio;
  int adcPort2OutputGpio;
  adc_unit_t ...;
  adc_channel_t ...;

  // Excitation/front-end control
  int excitationOutPin;     // 只在硬件确实直接由 MCU 输出时存在
  int rangeSelectPins[...] ;
  int muxSelectPins[...] ;
  int frontEndEnablePin;

  // Electrical constants that are hardware facts, not fit parameters
  double nominalCurrentSenseOhm;
  double nominalTransimpedanceGain;
};

extern const BoardProfile kBoard;
```

实施约束：

1. 所有 GPIO 都必须在 `board_profile.cpp` 集中定义；业务文件不得出现裸 GPIO 数字。
2. CI 加静态检查/grep：除 `board_profile.cpp` 和测试 fixture 外，不允许新的 `pinMode(<number>)`/`digitalWrite(<number>)`。
3. 原理图中若一个 GPIO 同时连接启动 strap、USB/JTAG、PSRAM/Flash 或其它板载器件，必须在 profile 旁注释原因和限制。
4. ADC GPIO 必须核对到 ESP32-S3 的 ADC unit/channel；不能只检查“这个脚能 analogRead”。

### 2.2 ADC 路径

ESP32-S3 的连续 ADC 驱动采用 DMA frame；首版硬件采集优先使用 ESP-IDF `adc_continuous_*` API，而不是在循环里逐点 `analogRead()`。驱动生命周期固定为：

```text
adc_continuous_new_handle
  -> adc_continuous_config
  -> register_event_callbacks
  -> adc_continuous_start
  -> 非阻塞 drain/read
  -> adc_continuous_stop
  -> adc_continuous_deinit
```

关键硬件限制必须进入代码设计：ESP32-S3 的 ADC2 DMA continuous 受硬件 errata/稳定性限制，因此**禁止把“有 ADC1 + ADC2”推导成“可以稳定双 ADC 同步 DMA”**。如果两路 V/I 或 Vin/Vout 都落在同一 ADC continuous pattern 中，它们本质上是交错采样，必须保存/推导每个样本的有效时间；正弦拟合必须使用真实时间基准或显式补偿已知 channel skew。

接口：

```cpp
enum class CaptureChannel : uint8_t {
  VoltageDut,
  CurrentSense,
  Port2Input,
  Port2Output,
};

struct CaptureRequest {
  CaptureChannel channels[2];
  uint8_t channelCount;          // 本项目业务目前固定 2
  double excitationHz;
  uint32_t targetSampleRateHz;
  uint16_t captureCycles;
  uint16_t minSamplesPerChannel;
};

struct TimedSamples {
  const int16_t* samples;
  const uint32_t* sampleTimeTicks; // 或 start + deterministic interval/skew
  size_t count;
  double tickSeconds;
};

class AdcCapture {
 public:
  CaptureStatus start(const CaptureRequest&);
  void poll();
  bool done() const;
  CaptureStatus status() const;
  TimedSamples channel(CaptureChannel) const;
  void cancel();
  void stopAndRelease();
};
```

ISR/callback 只做 buffer index/flag 更新，不得做 `String`、TFT、BLE、拟合、动态内存分配或 `Serial.printf`。DMA overflow 必须变成显式 `ADC_OVERRUN`，不能继续用残缺 buffer 做拟合。

ADC raw code 到电压先经过 ESP-IDF ADC calibration（ESP32-S3 支持 curve-fitting calibration），但这一层只修正 MCU ADC transfer；它不能代替仪器前端的复增益/相位校准。

如果实板验证发现内部 ADC 的 ENOB、采样时差或相位稳定性不足以满足双端口/阻抗相位指标，则保持上层接口不变，把 `AdcCapture` 后端换成外部同步 ADC。也就是说，**是否升级 ADC 不得迫使重写 DSP/UI/BLE**。

### 2.3 激励源

ESP32-S3 方案不应假设旧 ESP32 的片上 DAC 接口存在。激励路径由原理图决定：若是 PWM/RMT/I2S/外部 DDS/DAC，则分别实现到同一接口：

```cpp
struct ExcitationConfig {
  double requestedHz;
  double requestedVrms;
  Waveform waveform;       // 本项目测量固定 Sine；其它波形只留 diagnostics
};

struct ExcitationState {
  double actualHz;
  double actualVrms;
  bool amplitudeCalibrated;
};

class ExcitationDriver {
 public:
  ExcitationStatus begin(const ExcitationConfig&);
  void poll(uint64_t nowUs);
  ExcitationState state() const;
  void stop();
};
```

单端口“固定电压”语义必须严格：

- 若硬件具有闭环或已标定、可设置幅度的激励源，UI 允许设 `driveVrms`，每点记录实际/校准后的 `actualVrms`。
- 若硬件实际上只有固定幅度或幅度随频率明显变化，则 UI 只显示“calibrated nominal drive”，禁止提供虚假的“固定电压设置”。
- 任何频率点如果 `actualHz` 与 requested 偏差过大，都使用 **actualHz** 进入 sine fit 和 CSV，requestedHz 仅用于诊断。

### 2.4 ST7735S

ST7735S 4-wire serial datasheet 给出写时钟周期最小 66 ns，等价理论上限约 15.15 MHz；因此首版 `tftSpiHz = 10 MHz`，不要沿用旧 ILI9341 的 40 MHz 假设。若之后要提高，必须用逻辑分析仪和实屏做压力测试后再升。

ST7735S 控制器内部 RAM 尺寸和实际模块可见区/offset 未必一致。`width/height/xOffset/yOffset/rotation` 全部属于 board profile，并通过实屏 color-bar/边界矩形测试确认；禁止仅凭“ST7735S”字符串写死 128x160 偏移。

测量期间，特别是 ADC `CAPTURING` 状态，不做 TFT SPI 大块刷新。进度更新放在频点间隙；这样既减少电源/地噪声，也避免无谓抢总线。

## 3. 统一测量语义与非阻塞状态机

数学真源保持与项目理论一致：

- 单端口：`Z(f) = V_DUT(f) / I_DUT(f)`。
- 电阻：`Z=R`。
- 电容：`Z=1/(jωC)`。
- 物理电感：`Z=R_d + jωL`，必须同时报告 L 和 DCR。
- 双端口：统一采用标准传递函数 `H(f)=Vout(f)/Vin(f)`；`gainDb=20 log10 |H|`，`phase=arg(H)=arg(Vout)-arg(Vin)`。

当前 `calculateGainPhase()` 中“`-20log10(out/in)` + `φin-φout`”的倒数语义不再作为最终协议。所有新代码、网站和测试统一按 `H=Vout/Vin`；旧 helper 若保留只能加 `legacy` 标记，不能在业务路径使用。

### 3.1 类型化结果，彻底移除 `bool isOnePort`

```cpp
enum class MeasurementKind : uint8_t {
  OnePortImpedance,
  TwoPortTransfer,
};

enum class MeasurementStatus : uint8_t {
  Ok,
  InvalidConfig,
  ExcitationFail,
  AdcFail,
  AdcOverrun,
  Clipped,
  SignalTooSmall,
  FitSingular,
  FrequencyMismatch,
  CalibrationMissing,
  CalibrationStale,
  Cancelled,
  InternalError,
};

struct MeasurementRequest {
  MeasurementKind kind;
  double requestedHz;
  double driveVrms;
  uint16_t settleCycles;
  uint16_t captureCycles;
  uint16_t minSamplesPerChannel;
};

struct MeasurementQuality {
  MeasurementStatus status;
  bool clippedChA;
  bool clippedChB;
  bool frequencyLocked;
  double residualRmsA;
  double residualRmsB;
  double amplitudeA;
  double amplitudeB;
  double channelSkewSeconds;
  uint32_t samplesA;
  uint32_t samplesB;
};

struct OnePortPoint {
  double requestedHz;
  double actualHz;
  double reOhm;
  double imOhm;
  double magOhm;
  double phaseDeg;
  MeasurementQuality quality;
};

struct TwoPortPoint {
  double requestedHz;
  double actualHz;
  double reH;
  double imH;
  double gainDb;
  double phaseDeg;
  MeasurementQuality quality;
};
```

### 3.2 `MeasurementEngine` 状态机

状态必须显式可观测：

```text
IDLE
 -> PREPARE
 -> EXCITATION_START
 -> SETTLING
 -> CAPTURE_ARM
 -> CAPTURING
 -> FITTING
 -> APPLY_CALIBRATION
 -> RESULT_READY
 -> IDLE

任何状态 -> CANCELLING -> SAFE_OFF -> IDLE
任何硬件错误 -> ERROR_SAFE_OFF
```

接口：

```cpp
class MeasurementEngine {
 public:
  MeasurementStatus start(const MeasurementRequest& request);
  void poll(uint64_t nowUs);
  MeasurementEngineState state() const;
  float progress() const;
  bool resultReady() const;
  bool takeResult(OnePortPoint&);
  bool takeResult(TwoPortPoint&);
  void cancel();
};
```

约束：

- `start()` 只做参数校验/资源申请，不允许等待若干周期。
- `poll()` 每次执行必须有明确上界；不得在一次调用内等待整个频点或整个 sweep。
- `SETTLING` 用时间/周期截止点推进，不使用 `delay()`。
- `CAPTURING` 由 DMA/缓冲推进；输入按键仍能被 `loop()` 轮询。
- Back/Cancel 从用户动作到进入 `SAFE_OFF` 的目标延迟 `<100 ms`；下一次 `poll()` 必须停止激励并请求停止 capture。
- 每次 ERROR/CANCEL 都执行同一 `safeOff()`：停止激励、停止 ADC、释放 DMA、关闭前端 enable/relay 到安全态。
- 顶层 `loop()` 可以保留极小的 scheduler yield，但业务逻辑禁止基于 `delay()` 做时序。

### 3.3 正弦拟合和相位

保留当前 known-frequency three-parameter fit：

`x(t)=a sin(ωt)+b cos(ωt)+c`，`A=sqrt(a²+b²)`，`φ=atan2(b,a)`。

但要扩展 API 使其不隐含“两通道样本在同一时刻”：

```cpp
SineFitResult sineFit3(
  const SampleSeries& samples,
  double actualFreqHz);
```

`SampleSeries` 要么包含每个样本时间戳，要么包含已验证的 `t0 + n*dt + channelSkew` 模型。若仍使用旧等间隔版本，则 `AdcCapture` 必须先做 channel skew 相位修正：`Δφ = 2πfΔt`。

质量门槛至少包含：输入幅度过小、ADC clipping、残差 RMS、有效样本数、DMA overflow、拟合矩阵失败。质量失败的点不能静默进入 CSV 拟合数据。

## 4. 仪器校准：ADC 校准与复数前端校准分层

把校准分成两个绝不能混淆的层级：

1. **MCU ADC calibration**：用 Espressif `adc_cali_*` 把 raw code 转成更可靠的电压尺度。
2. **仪器前端复校准**：补偿电压路径、电流感测/跨阻路径、Port2 输入/输出路径在频率上的增益和相位误差。

建议数据模型：

```cpp
struct ComplexCorrection {
  double gain;       // multiplicative magnitude
  double phaseRad;   // additive phase
};

struct CalibrationProfile {
  uint32_t schemaVersion;
  char id[32];
  uint32_t createdUnix;
  double validFMinHz;
  double validFMaxHz;

  ComplexCorrection voltagePath(double f) const;
  ComplexCorrection currentPath(double f) const;
  ComplexCorrection port2InputPath(double f) const;
  ComplexCorrection port2OutputPath(double f) const;
};
```

频率间 correction 用明确定义的插值方式（建议对 log-frequency 线性插值 magnitude/phase；phase 先 unwrap），超出 calibrated band 返回 `CalibrationStale/OutOfRange`，不能无提示外推。

单端口可实现为：

```text
V = C_v(f) * V_raw
I = C_i(f) * I_raw / R_sense-or-transimpedance-scale
Z = V / I
```

双端口：

```text
Vin  = C_in(f)  * Vin_raw
Vout = C_out(f) * Vout_raw
H = Vout / Vin
```

理论文档已经指出现有 OSL 只是 scaffolding，因此本次不要把“校准”写成“一端口 VNA OSL 已完成”。只有硬件最终采用反射系数架构并真正实现三项误差模型时，才使用该表述。

## 5. 三个 TFT 顶层模式的确定行为

### 5.1 模式 1：单元件自动 R/C/L 识别

目的不是调用 Try1 做拓扑搜索，而是在三个 primitive 模型中选择：`R`、`C`、`L+DCR`。为了抗噪和避免在单个频点误判，默认使用 3~5 个几何分布的有效频点，全部通过同一个 `MeasurementEngine::OnePortImpedance` 获得。

候选模型：

```text
R:      Z_k = R
C:      Z_k = -j / (ω_k C)
L+DCR:  Z_k = Rd + j ω_k L
```

首版可用线性闭式拟合，避免在 ESP32 上引入重型非线性优化器。若权重 `w_k` 来自测量质量：

- `R = weightedMean(Re(Z_k))`，并检查 `Im(Z)` 是否与 0 兼容。
- 令 `q=1/C`，`Im(Z_k)=-q/ω_k`；按加权 LS 求正的 `q` 后 `C=1/q`。
- `Rd=weightedMean(Re(Z_k))`，`L = Σ w² ω Im(Z) / Σ w² ω²`，强制 `Rd>=0, L>0`。

每个模型都用完整复残差重新计算统一 `WRMSE`，不能只比较虚部符号。分类条件同时要求：

- 最优模型绝对残差低于通过标准件实验标定的阈值；
- 第一名相对第二名有足够 gap；
- 参数在声明可测范围内；
- 至少 `N_valid >= 3`；
- 没有 clipping / signal-too-small / calibration invalid。

否则显示 `UNKNOWN` 或 `OUT OF RANGE`，不能为了给结果而强行判 R/C/L。

TFT 最终显示：

```text
RESISTOR
R = ... ohm
quality: GOOD/WARN

CAPACITOR
C = ... F
quality: GOOD/WARN

INDUCTOR
L   = ... H
DCR = ... ohm
quality: GOOD/WARN
```

此模式 **不启动 BLE**。

### 5.2 模式 2：单端口固定电压扫频 -> 完成后 BLE -> 网站拟合

配置项：

```cpp
struct SweepConfig {
  MeasurementKind kind;       // OnePortImpedance
  double driveVrms;
  double fStartHz;
  double fStopHz;
  uint16_t pointsPerDecade;
  uint16_t maxPoints;
  bool logSpacing;
};
```

合法性：`0 < fStart < fStop`，两端都在 hardware/calibration declared band 内；计算后的 point count 不得超过编译期上限。当前 257 点上限可以先保留，后续由 RAM 预算调整。

`SweepEngine`：

```cpp
class SweepEngine {
 public:
  SweepStatus start(const SweepConfig&);
  void poll(uint64_t nowUs);
  void cancel();
  SweepState state() const;
  size_t completedPoints() const;
  size_t totalPoints() const;
  const OnePortDataset* sealedOnePortDataset() const;
};
```

流程硬约束：

```text
配置 -> 生成频率表
 -> BLE/RF 必须为 OFF
 -> 每个频点：settle -> async capture -> fit -> calibration -> store
 -> 全部频点结束
 -> stop excitation
 -> stop/deinit ADC/DMA
 -> quiet guard
 -> dataset seal
 -> 生成 canonical CSV + metadata + CRC32
 -> 状态变为 TRANSFER_READY
 -> 此时才允许初始化 BLE 和 advertising
```

测量期间绝不调用 BLE `notify()`、不 advertising、不保持 GATT connection。如果用户在 BLE 已连接状态下开始新的 sweep，顺序必须是 `disconnect -> BLE deinit -> radio-off confirmed -> measurement start`。

最终上传的不是原始波形，而是网站拟合真正需要的复阻抗点：

```csv
# lcr-dataset=one-port-z
# schema=lcr-z-csv-v1
# protocol=1
# firmware=<git/version>
# calibration_id=<id>
# drive_vrms=<value>
f,re,im
100.0,12.34,-45.67
...
```

`f` 必须使用 `actualHz`；`re/im` 单位为 ohm。首版只传 3 列，后续若测量协方差链完成，可升级为现有 parser 支持的 6 列形式，但 schema version 必须改变/声明。

网站接收后必须调用现有 `parseZCsv(text)`，得到同一 `ZPoint[]`，再进入现有 `loadPoints()`/WASM 拟合。因此手工文件导入和 BLE 导入在拟合层没有任何语义差异。

### 5.3 模式 3：双端口扫频 -> 网站曲线

采集引擎仍然相同，但通道为 `Vin/Vout`，内部 canonical 数据是复数传递函数：

```text
H = Vout / Vin
reH = Re(H)
imH = Im(H)
gainDb = 20 log10 |H|
phaseDeg = wrap180(arg(H))
```

为了保留全部信息，建议双端口 CSV v1 用：

```csv
# lcr-dataset=two-port-h
# schema=lcr-h-csv-v1
f,re_h,im_h
100.0,0.923,-0.146
...
```

网站由 `re_h/im_h` 统一推导 Bode gain、phase 和 Nyquist，不再传一组可能符号不一致的派生量。前端若要导出用户熟悉的 Bode CSV，可再导出 `f,gain_db,phase_deg`，但设备到网站的真源保持复数 H。

TFT 在测量时显示进度、当前频率和错误计数；扫频后可以复用现有轻量曲线绘制代码给出 preview，但网站是完整曲线展示真源。双端口同样遵循 **采样完成前 BLE OFF**。

## 6. BLE GATT 协议和“测量/无线互斥”架构

### 6.1 生命周期

新增全局 `RadioManager`，而不是让各 screen 自己 `BLEDevice::init()`：

```cpp
enum class RadioState {
  Off,
  StartingBle,
  Advertising,
  Connected,
  Sending,
  StoppingBle,
  Error,
};

class RadioManager {
 public:
  bool startBleForSealedDataset(const Dataset&);
  void poll();
  void stopBle();
  RadioState state() const;
};
```

强 invariant：

```text
MeasurementEngine.state in {PREPARE..RESULT_READY}
    => RadioState == Off

RadioState in {StartingBle,Advertising,Connected,Sending}
    => MeasurementEngine == IDLE
       && AdcCapture released
       && Excitation stopped
```

将该 invariant 写成运行时 assert（debug build）和 host state-machine unit test。

Wi-Fi 本项目首版不使用；固件不要初始化 Wi-Fi。这样采集期间整个 RF 子系统保持静默。

### 6.2 GATT v1

使用自定义 128-bit UUID，固定后不随文件/分支改变：

```text
Service:  6e6f0001-5f31-4c43-a001-6c63722d7631
Control:  6e6f0002-5f31-4c43-a001-6c63722d7631  WRITE WITH RESPONSE
Status:   6e6f0003-5f31-4c43-a001-6c63722d7631  READ + NOTIFY
Metadata: 6e6f0004-5f31-4c43-a001-6c63722d7631  READ
Data:     6e6f0005-5f31-4c43-a001-6c63722d7631  NOTIFY
```

`Metadata` 是 UTF-8 JSON，字段固定：

```json
{
  "protocol": 1,
  "firmware": "...",
  "session_id": 12345678,
  "dataset_kind": "ONE_PORT_Z",
  "schema": "lcr-z-csv-v1",
  "point_count": 121,
  "byte_count": 4812,
  "crc32": "A1B2C3D4",
  "calibration_id": "cal-..."
}
```

`Control` v1 用定长命令字节，避免在 MCU 上解析 JSON：

```text
0x01 START_TRANSFER
0x02 RESTART_TRANSFER
0x03 ABORT_TRANSFER
0x04 GET_STATUS
```

`Status` 至少包含：protocol、state、error code、session id、bytes sent/total。

`Data` 帧：

```text
offset size  field
0      2     magic = ASCII 'L','C'
2      1     protocol = 1
3      1     dataset kind
4      2     seq, uint16 little-endian
6      2     payload_len, uint16 little-endian
8      N     CSV bytes
```

不要假设 ATT MTU 一定为 185/247。发送层根据 stack 能确认的 ATT payload 决定 notification 大小；如果无法可靠取得 negotiated payload，使用兼容性保守值。浏览器重组时检查 seq、总 byte_count 和 whole-file CRC32。v1 不实现复杂随机重传；断线、seq gap 或 CRC 错误时发送 `RESTART_TRANSFER`，整份 sealed dataset 从 seq 0 重发。

传输完成后设备保持 dataset 不变，直到：用户明确开始新测量、超时清理、或重启。BLE 传输失败绝不回头修改测量值。

### 6.3 固件 BLE 实现选择

首选 Arduino-ESP32 3.x 自带 BLE wrapper（`BLEDevice/BLEServer/BLECharacteristic`），因为仓库当前是 Arduino 工程且官方有 BLE UART/notify 示例。若在实际长 CSV 通知吞吐/内存上发现 wrapper 不稳定，再切 ESP-IDF/NimBLE；GATT 协议和 `RadioManager` 接口保持不变。

`BluetoothSerial.h` 在生产路径中必须为 0 引用。CI 加 grep gate。

## 7. 网站侧耦合改造

新增：

```text
frontend/src/lib/ble/protocol.ts
frontend/src/lib/ble/lcrDevice.ts
frontend/src/lib/ble/crc32.ts
frontend/src/lib/twoPortCsv.ts
frontend/src/store/device.ts
```

如 TypeScript 当前 lib 缺 Web Bluetooth 类型，加入 `@types/web-bluetooth` dev dependency；不要到处写 `any`。

### 7.1 `lcrDevice.ts`

职责仅限 BLE：

```ts
export type DeviceDataset =
  | { kind: 'ONE_PORT_Z'; csvText: string; metadata: ... }
  | { kind: 'TWO_PORT_H'; csvText: string; metadata: ... };

export async function chooseAndConnect(): Promise<LcrDeviceSession>;
export async function receiveDataset(session: LcrDeviceSession): Promise<DeviceDataset>;
```

行为：

1. 从用户点击触发 `navigator.bluetooth.requestDevice()`；service filter 使用 v1 service UUID。
2. GATT connect。
3. 读取 metadata/status。
4. 订阅 status/data notifications (`startNotifications`)。
5. `START_TRANSFER`。
6. 按 seq 重组 byte stream。
7. 校验 byte_count + CRC32 + protocol/schema。
8. `TextDecoder('utf-8')` 得到 CSV。
9. 返回 dataset；BLE 层本身不调用拟合器。

Web Bluetooth 必须在支持的浏览器和 secure context 中使用。前端在页面加载时检测 `navigator.bluetooth`；不支持时按钮禁用并给出明确提示。不能把“不支持”表现成普通“连接失败”。

### 7.2 单端口拟合页

现有 file-upload：

```text
file -> text -> parseZCsv -> loadPoints -> WASM fit
```

新增 BLE 后必须变成：

```text
BLE -> receiveDataset
    -> assert kind == ONE_PORT_Z
    -> parseZCsv(csvText)
    -> loadPoints
    -> 与文件上传完全相同的 WASM fit
```

不能写第二个 `parseBlePoints()` 复制 CSV 规则。页面可提供“保存收到的 CSV”按钮，下载的就是通过 CRC 校验后的原始 bytes，便于复现实验。

### 7.3 双端口曲线页

`twoPortCsv.ts` 只解析 `lcr-h-csv-v1`，得到：

```ts
interface HPoint {
  f: number;
  re: number;
  im: number;
  gainDb: number;
  phaseDeg: number;
}
```

`gainDb/phaseDeg` 在 parser 中从复 H 推导。现有 Bode/phase/Nyquist chart 尽量复用，不把 BLE 数据伪装成后端 scan id。为此 `device.ts` 建立本地设备 session/store，和现有 server scan store 分离。

### 7.4 前端设备状态

```text
unsupported
idle
chooser
connecting
ready
receiving
validating
complete
error
```

错误信息要区分：用户取消 chooser、GATT 断线、protocol mismatch、wrong dataset kind、seq gap、CRC mismatch、invalid CSV、unsupported browser。

## 8. 文件级实施顺序与接口迁移

实施不能一次把所有旧代码删除；按以下依赖顺序落地，每一步都保持 `main` 可编译：

### 阶段 A：硬件与构建真源

- 从 `ino/databook/.../开发板原理图.pdf`、PCB/Altium 和 ESP32-S3 datasheet 建立 `docs/HARDWARE_MAPPING.md`。
- 明确每个 GPIO、ADC unit/channel、前端开关、激励器件、TFT 接口和供电限制。
- 新建 `board_profile.*`。
- `build_check.sh` 切 ESP32-S3；固定库版本；ST7735S 10 MHz 起步。
- 实屏 smoke test + 按键/编码器 smoke test。

**阶段 A 未通过，不允许开始“猜 GPIO”的测量代码。**

### 阶段 B：真实采集 + 激励 + 校准骨架

- 实现 `ExcitationDriver`、`AdcCapture`。
- 将合成 stub 移到 `ino/test` 或 `#if LCR_ENABLE_SYNTHETIC_STUBS`，生产配置强制 0。
- 把 `dsp_fit` 接到真实 TimedSamples。
- 写 `MeasurementEngine` 状态机和 safe-off。
- 先对已知外部正弦源做双通道幅相测量，不急着做 RLC UI。

### 阶段 C：单端口物理量

- 连接前端尺度/复校准。
- 输出 `OnePortPoint`；用单 R/C/L 和系列 RLC reference 验证 `Re/Im/phase` 符号。
- 明确 valid band 和量程切换策略。

### 阶段 D：单元件模式

- 实现三模型 fit/classifier。
- 改顶层菜单第 1 项和 TFT result screen。
- 加标准件 validation suite。

### 阶段 E：单端口 sweep + sealed CSV

- 实现 `SweepEngine`、frequency plan、dataset store、CSV exporter、CRC。
- 确认 sweep 全程 radio-off。
- 先通过串口/host test 验证 CSV 与 `frontend/src/lib/csv.ts` 完全兼容，再接 BLE。

### 阶段 F：BLE + FitView

- 替换 `bt_link.*` 为 `RadioManager/BleTransfer`。
- 实现 GATT v1。
- 前端实现 Web Bluetooth receiver。
- FitView BLE 导入复用 `parseZCsv()`。
- 删除正常路径对 `bt_bridge.py` 的依赖；legacy 文件可暂时保留并标 Deprecated。

### 阶段 G：双端口

- MeasurementEngine 的 TwoPort path 输出 complex H。
- 双端口 sweep CSV v1。
- 网站曲线 parser/store/chart 接入。
- 全工程统一 `H=Vout/Vin`，删除旧倒数 gain/phase 的业务引用。

### 阶段 H：硬化与清理

- 完成校准工具/校准 profile 持久化。
- 协方差可选升级；先不阻塞首版。
- 运行长时间 sweep、取消、掉线、重传、浏览器兼容测试。
- 删除无用 legacy macro/旧 menu/旧 Classic BT。
- 更新 README、ESP32 文档、BLE protocol、CSV schema 文档。

## 9. 测试与验收标准

下面的阈值分为“必须为真”的软件/协议约束，以及需要通过实板标定最终冻结的工程精度目标。后者不是数学定理。

### 9.1 构建/静态验收（必须）

- `arduino-cli compile` 对目标 ESP32-S3 FQBN 成功；0 linker error/warning（允许白名单第三方 warning）。
- production binary 不链接 synthetic `partner_stubs`。
- production source 中 `BluetoothSerial` 引用数 = 0。
- 所有顶层业务 GPIO 仅由 `BoardProfile` 提供。
- frontend `typecheck + unit tests + build` 全过。
- 现有 WASM fitting regression 不因 BLE 改动失败。

### 9.2 非阻塞/安全验收（必须）

- 一次 100+ 点 sweep 中，顶层 `loop()` 持续运行，watchdog 0 reset。
- 用户按 Back 后 `<100 ms` 进入 cancel/safe-off 请求；示波器确认激励在下一状态推进周期内关闭。
- ADC callback/ISR 不做 heap/BLE/TFT/日志重活。
- 故意制造 DMA overflow 后返回 `ADC_OVERRUN`，不产生“看似正常”的 Z 点。
- 任一错误路径最终 excitation=OFF、ADC=STOPPED/DEINIT。

### 9.3 BLE 隔离验收（必须）

用调试日志和 RF/电流/状态断言证明：

- 开机后未进行测量上传时 BLE 不初始化/不 advertising。
- `SweepState=Measuring` 的整个时间窗口内 `RadioState=Off`。
- 最后一频点完成、ADC/DMA release、dataset seal 之前没有任何 GATT notify/advertising。
- 已连接 BLE 时开始新测量，会先 disconnect/deinit 后再启动采集。

这项验收优先级高于 BLE 传输速度。

### 9.4 DSP/物理符号验收（必须）

合成/标准件：

- 纯 R：`Im(Z)≈0`。
- 纯 C：`Im(Z)<0`。
- `L+DCR`：`Re(Z)≈DCR >=0`、`Im(Z)>0` 且随 f 近似线性。
- 双端口 RC low-pass：低频 `|H|≈1`，超过拐点 gain 下降，phase 为负（按 `Vout/Vin` 约定）。
- interleaved channel skew 的合成数据在补偿后恢复正确 phase；关闭补偿测试必须能观察到预期 `2πfΔt` 偏差，防止测试形同虚设。

### 9.5 单元件模式工程目标

在明确声明的量程/频段内，用至少以下 standards 做 validation campaign：R 100 Ω / 1 kΩ / 10 kΩ，C 1 nF / 10 nF / 100 nF，L 100 µH / 1 mH / 10 mH（若硬件带宽不覆盖则替换为实际可测档）。

首轮 acceptance target：

- 分类正确率：验证集 100%；边界/超量程必须允许 `UNKNOWN` 而不是误分。
- R：相对误差 <= 2%。
- C：相对误差 <= 3%。
- L：相对误差 <= 5%。
- DCR：`max(5%, 0.2 Ω)` 以内。

这些数值先作为工程门槛；若实板前端硬件客观限制达不到，不能偷偷放宽代码阈值，而应记录测得 uncertainty/range 后由硬件指标决定最终冻结值。

### 9.6 单端口 sweep/CSV 验收（必须）

- 每个有效 row `f>0` 且 `re/im` finite。
- `f` 使用实际激励频率。
- 失败点不伪造为 0；在 dataset diagnostics 中保留错误，但不进入拟合 CSV。
- firmware 生成的 CSV 喂给网站现有 `parseZCsv()` 后 point count/value 与 firmware dataset bit-for-semantic 一致。
- 参考 R/C/L/series RLC 的 sweep 与离线理论值/可信台式 LCR 对比并形成误差图。

### 9.7 双端口工程目标

先用已知 RC low-pass/high-pass 与直通 fixture：

- complex H 定义一致，不发生 gain 倒数或 phase 符号翻转。
- 首轮目标：可用频段内 gain error <= 0.5 dB，phase error <= 3°。
- 网站由 complex H 计算的 Bode/Nyquist 与离线 reference 一致。

### 9.8 BLE/网站端到端验收（必须）

对最大 257 点 one-port CSV 和同规模 two-port CSV：

- 连续完整传输 20 次，CRC mismatch = 0。
- 中途断开一次，重新连接 + `RESTART_TRANSFER` 后能得到完全相同 CRC。
- 浏览器收到 one-port 后能直接进入现有 WASM fitting；无需 Python bridge/backend 中转。
- two-port 收到后曲线 point count/frequency order 与设备 dataset 一致。
- protocol version 不支持时前端拒绝并给明确 upgrade message。
- HTTP 非 secure context/不支持 Web Bluetooth 的浏览器显示“功能不可用”的专门提示，而不是 generic error。

## 10. 文档、版本与长期兼容规则

新增/更新：

```text
docs/HARDWARE_MAPPING.md
frontend/src/docs/esp32.md
protocol/BLE_PROTOCOL_V1.md
protocol/CSV_SCHEMA_V1.md
ino/README.md
```

固件必须暴露：

```cpp
#define LCR_FW_VERSION "..."
#define LCR_BLE_PROTOCOL_VERSION 1
#define LCR_Z_CSV_SCHEMA_VERSION 1
#define LCR_H_CSV_SCHEMA_VERSION 1
```

任何改变以下语义的提交必须 bump protocol/schema，而不是悄悄改字段：

- `Z=V/I` 定义；
- `H=Vout/Vin` 定义；
- phase 正方向；
- CSV 单位；
- BLE frame layout；
- CRC 覆盖范围；
- 数据集完成/封存语义。

## 11. 最终 review：实施时禁止偏离的结论

在进入编码前再次检查，本计划最终架构满足用户目标且没有把已存在的好实现推倒重来：

1. **硬件层**：当前真正缺的是生产级 excitation/ADC/calibration；合成 stub 不能继续冒充测量代码。
2. **ESP32-S3**：Classic `BluetoothSerial` 必须消失；BLE 是唯一设备到浏览器的无线通道。
3. **抗干扰核心规则**：BLE 不是“扫频时少发一点”，而是 **整个采样期间 Radio Off，完整采样结束并释放 ADC 后才初始化 BLE**。
4. **非阻塞规则**：把“每个频点阻塞一次”也消除。MeasurementEngine 和 SweepEngine 都是 poll-driven state machine，UI/Cancel 始终活着。
5. **单元件**：不是单相位阈值判断，而是 R/C/(L+DCR) 多频点模型比较；不确定时允许 UNKNOWN。
6. **单端口**：设备只上传拟合所需 `f,ReZ,ImZ` CSV，不上传 raw waveform；网站继续使用现有 `parseZCsv -> ZPoint[] -> WASM fit`。
7. **双端口**：统一物理定义 `H=Vout/Vin`，设备传 complex H，网站派生 Bode/phase/Nyquist。
8. **前端**：BLE import 与 file import 在解析之后汇合成同一个数据路径；不复制拟合语义。
9. **校准**：ESP ADC eFuse/curve calibration 与仪器复增益/相位 calibration 分层；不把现有 scaffolding 夸大为完整 OSL/VNA 校准。
10. **显示与输入**：现有非阻塞 InputManager、ScreenManager 和 DSP 核心保留；只替换错误的硬件 profile 和产品业务层。
11. **可追溯性**：所有 dataset 带 firmware/protocol/schema/calibration id，传输带 byte_count + CRC；任何拟合结果可以追溯回一份保存的 CSV。
12. **硬件 pinout**：实现者不得复制旧 ESP32/ILI9341 GPIO；必须从仓库自制板原理图完成 `HARDWARE_MAPPING.md` 并通过实板 continuity/smoke test 后才能冻结 `BoardProfile`。

通过以上 review 后，本计划可直接作为接下来 main 分支实现的规范；如果实现与这里的接口/语义冲突，应先修改并 review 本计划，而不是在代码中默默引入另一套协议或定义。

## 12. 主要依据与参考资料

### 仓库/项目真源

- 当前实现基线：`invincible-summer/LCR-Analyzer-WebSite`, `main@b2b6c6b9c1e0670cd7c22c8380df19a2a268a89c`。
- `ino/LCR_UI/{LCR_UI.ino, hw_config.h, partner_api.h, partner_stubs.cpp, screen_measure.cpp, screen_sweep.cpp, bt_link.h, bt_link.cpp, dsp_fit.cpp, analysis.cpp, input.cpp, screens.*}`。
- `ino/README.md`, `ino/tools/build_check.sh`, `ino/test/test_dsp.cpp`。
- `frontend/src/lib/csv.ts`, `frontend/src/lib/fitTypes.ts`, `frontend/src/views/FitView.vue`, `frontend/src/views/SweepView.vue`, `frontend/src/store/scan.ts`, `frontend/src/docs/esp32.md`。
- 仓库硬件资料：ESP32-S3 datasheet/TRM、ST7735S datasheet、`ino/databook/自制开发板资料/.../开发板原理图.pdf`、PCB/Altium 资料。
- 项目理论文档：`LCRTheory_rendered.pdf/.md`，尤其 measurement sine fit、`Z=V/I`、L+DCR 模型、校准边界和 `f,re,im` 数据契约。

### 官方/高可信外部资料

- Espressif ESP32-S3 Series Datasheet: https://documentation.espressif.com/esp32_s3_datasheet_en.pdf
- ESP-IDF ESP32-S3 ADC Continuous Mode Driver: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/adc/adc_continuous.html
- ESP-IDF ESP32-S3 ADC Calibration Driver: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/adc_calibration.html
- Arduino-ESP32 Bluetooth documentation: https://docs.espressif.com/projects/arduino-esp32/en/latest/api/bluetooth.html
- Arduino-ESP32 BLE documentation/examples: https://docs.espressif.com/projects/arduino-esp32/en/latest/api/ble.html
- MDN Web Bluetooth `requestDevice`: https://developer.mozilla.org/en-US/docs/Web/API/Bluetooth/requestDevice
- MDN GATT `startNotifications`: https://developer.mozilla.org/en-US/docs/Web/API/BluetoothRemoteGATTCharacteristic/startNotifications
- Sitronix ST7735S v1.3 datasheet mirror (TI E2E; serial timing table): https://e2e.ti.com/cfs-file/__key/communityserver-discussions-components-files/908/ST7735S_5F00_v1.3.pdf

