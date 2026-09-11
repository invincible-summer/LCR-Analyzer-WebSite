# LCR ESP32 / 网站交互重构实施方案

**仓库：** `invincible-summer/LCR-Analyzer-WebSite`  
**审计基线：** `main @ d1e0f1f182eb960aa2c3adc09f1b7b85b76d23ed`  
**方案日期：** 2026-09-12  
**状态：** 完整仓库审计、DO_NOT_TOUCH 边界复核、ESP32-S3/74HC595/ST7735S/Web Bluetooth 资料复核后的实施规范

---

## 0. 最终结论

当前 v4.1 非 `DO_NOT_TOUCH` 的 ESP32 测量链不能继续“修补”或与硬件验证代码逐段合并。它建立在另一套硬件假设上，并且重新实现了 `DO_NOT_TOUCH_lcr_api.h` 已经提供的频率生成、ADC 采集、自动量程、校准、阻抗/传递函数计算等能力。

最终架构必须固定为：

```text
UI / 菜单 / 扫频编排 / Dataset / BLE
                    │
                    ▼
          新建 lcr_api.h
       （应用层唯一测量接口）
                    │
                    ▼
          新建 lcr_api.cpp
  （唯一允许 include DO_NOT_TOUCH_lcr_api.h
      的非 DO_NOT_TOUCH 编译单元）
                    │
                    ▼
      独立 FreeRTOS 测量 Worker
                    │
                    ▼
      DO_NOT_TOUCH_lcr_api.h
                    │
                    ▼
 已验证硬件链：
 LCD_CAM 8bit → 电阻网络 DAC
 74HC595 → TIA/V/I 增益/双端口控制
 两路 ADC → 计算与校准
```

这是最终软件边界，不是过渡方案。

网站侧 BLE GATT v1、CRC32 分帧、单端口 `parseZCsv → C++/WASM Try1/Try2/Try3`、双端口 `parseHCsv → Bode/Nyquist` 已经具备正确的职责分离，原则上保留。主要修改集中在：

1. ESP32 测量后端；
2. GPIO/硬件文档；
3. 数据集元数据真实性；
4. 测量期间 BLE 静默；
5. 构建/CI 对 DO_NOT_TOUCH 边界的强制保护；
6. 端到端固件→BLE→网站契约测试。

---

## 1. 不可违反的源代码与硬件规则

以下规则是后续实现的最高优先级约束，必须同时写进代码注释、CI 和 review checklist。

1. **任何文件名以 `DO_NOT_TOUCH` 开头的文件均不得修改。**
2. `DO_NOT_TOUCH_hong.h` 不得被 include、引用、链接或作为实现依据；它只作为未来预留文件存在。
3. 凡 `DO_NOT_TOUCH_lcr_api.h` 已经提供的操作，包括但不限于：
   - 频率输出；
   - 单口阻抗测量；
   - 双口传递测量；
   - 扫频；
   - ADC；
   - 自动量程；
   - TIA/电压/电流增益；
   - 校准；
   - 校准状态；
   - 量程状态；
   - LCR 数学换算；

   应用层一律只通过 `DO_NOT_TOUCH_lcr_api.h` 的公开 API 间接调用。
4. 非 `DO_NOT_TOUCH` 生产代码不得直接调用或引用：
   - `out_freq`
   - `out_sin`
   - `stop_sin`
   - `lcr_adc_*`
   - `lcr_measure_*`
   - `adc_continuous_*`
   - LCD_CAM/GDMA 波形底层函数
   - 74HC595 底层移位/锁存函数
   - 其它已经被 API 包装的内部实现符号。
5. `DO_NOT_TOUCH_EXAMPLE.ino.example` 只读，不修改。若需要演示新包装层，新增独立 example。
6. 已验证硬件中：
   - 不存在 PCM5102A；
   - 不存在其它外部 DAC 芯片；
   - 不存在外部同步 ADC；
   - 除 74HC595 外不假设任何其它外部数字芯片。
7. 已验证测量硬件职责为：
   - LCD_CAM 在 8 个 GPIO 上输出并行正弦码；
   - 外部已有电阻网络将并行码转换为模拟正弦；
   - 3 GPIO 驱动 74HC595；
   - 74HC595 的输出负责 TIA、电压增益、电流增益、双端口模式等控制；
   - 外部模拟链处理后进入两个 ADC 引脚；
   - 测量核心完成分析、自动量程与校准。
8. 应用代码只允许做“编排和 API 结果后处理”，例如：
   - 排队；
   - 进度；
   - Dataset；
   - `H_mag/phase → Re(H)/Im(H)`；
   - CSV；
   - 对已经由 API 算出的 R/C/L 数值做中位数聚合。

   不允许再实现第二套测量算法。

---

## 2. 当前仓库审计结论

### 2.1 当前 `measurement_engine` 与硬约束冲突

现有：

```text
measurement_engine.cpp
adc_capture.*
excitation_driver.*
dsp_fit.*
calibration.*
sine_plan.*
```

形成了独立的：

```text
激励
→ settle
→ ADC capture
→ sine fit
→ calibration
→ Z/H
```

链路。

这不是简单“接口不一样”，而是重新实现了现有 DO_NOT_TOUCH 核心承担的物理测量职责。

**处理决定：这些模块不进入最终生产固件。**

不建议尝试让 `MeasurementEngine` 内部改成“有时走旧代码，有时走 DO_NOT_TOUCH”，否则项目将长期保留两套物理真源。

---

### 2.2 当前 `board_profile` 与真实测量引脚直接冲突

当前旧 profile/documentation 包含：

- TFT 使用 GPIO10–14；
- 自定义 ADC 使用 GPIO4–7；
- I2S/PCM5102A 激励使用 GPIO17/18/21。

而 DO_NOT_TOUCH 已验证实现实际占用：

| 功能 | GPIO |
|---|---:|
| ADC 电压通道 | GPIO2 / ADC1_CH1 |
| ADC 电流通道 | GPIO1 / ADC1_CH0 |
| 74HC595 SRCLK | GPIO15 |
| 74HC595 SER | GPIO16 |
| 74HC595 RCLK | GPIO17 |
| LCD_CAM D0 | GPIO18 |
| LCD_CAM D1 | GPIO8 |
| LCD_CAM D2 | GPIO9 |
| LCD_CAM D3 | GPIO10 |
| LCD_CAM D4 | GPIO11 |
| LCD_CAM D5 | GPIO12 |
| LCD_CAM D6 | GPIO13 |
| LCD_CAM D7 | GPIO14 |

因此旧 TFT 的 GPIO10–14 与正弦 DAC 总线发生直接硬件冲突。

这意味着旧 profile 不能继续作为“硬件真源”，必须拆掉其中的测量硬件部分。

---

### 2.3 当前文档也需要同步废止旧测量模型

以下资料仍含有 PCM5102/I2S/自定义 ADC 路径：

- `docs/HARDWARE_MAPPING.md`
- `ino/README.md`
- 根目录 `README.md`
- `LCR_UI.ino` 文件头
- `board_profile.*`
- `screen_siggen.cpp`
- `build_check.sh` 的 TFT pin flags
- 旧 `plan.md` 的部分硬件约束

后续不能只改代码不改文档，否则下一轮维护很容易再次恢复错误硬件路径。

---

### 2.4 当前 UI 结构基本合理，应保留

顶层三个用户模式与需求一致：

1. 单元件 R/C/L 自动判断与数值测量；
2. 单端口扫频 → 全部采样完成 → BLE → 网站拟合；
3. 双端口扫频 → 全部采样完成 → BLE → 网站画曲线。

现有以下交互设计可以保留：

- Config / Run / Result；
- 频率范围编辑；
- PTS/DEC；
- 扫频进度；
- Dataset seal；
- BLE 在测量后启动；
- 单端口和双端口分开处理；
- 失败点不伪造为 0；
- CRC32 数据封存。

需要替换的是它们背后的“测量提供者”。

---

### 2.5 网站交互链路总体正确

当前网站已有：

```text
浏览器用户点击
→ navigator.bluetooth.requestDevice()
→ GATT connect
→ Metadata
→ Data notify
→ seq / byte_count / CRC32
→ CSV
```

单端口：

```text
ONE_PORT_Z
→ parseZCsv()
→ ZPoint[]
→ C++/WASM Try1 / Try2 / Try3
```

双端口：

```text
TWO_PORT_H
→ parseHCsv()
→ HPoint[]
→ Bode / Nyquist
```

这套协议不需要重做。

---

## 3. 一个必须优先修正的隐藏问题：DNT ADC 的任务亲和性

`DO_NOT_TOUCH_lcr_adc.h` 在 ADC 初始化阶段保存：

```text
xTaskGetCurrentTaskHandle()
```

后续 ADC ISR 会通知这个保存的 FreeRTOS task；测量调用内部再等待 task notification。

因此：

> **`lcr_api_init()` 必须在未来真正执行测量 API 的同一个 FreeRTOS Worker 中调用。**

以下做法不允许：

```text
Arduino setup()
  └─ lcr_api_init()     // Task A

以后：
MeasurementWorker
  └─ lcr_api_measure_z() // Task B
```

因为 ISR 仍可能通知 Task A，而 Task B 在等待，从而死锁。

正确方式：

```text
setup()
  └─ 创建 LcrWorker

LcrWorker task entry
  ├─ lcr_api_set_diagnostics(false)
  ├─ lcr_api_init()
  ├─ 发布 READY
  └─ 后续全部 lcr_api_measure/sweep/cal/... 也在本 Task 执行
```

这个约束应当写在 `lcr_api.h` 顶部中文注释中，并做代码 review gate。

---

## 4. 新建应用层 `lcr_api.h/.cpp`

### 4.1 文件职责

新增：

```text
ino/LCR_UI/lcr_api.h
ino/LCR_UI/lcr_api.cpp
```

#### `lcr_api.h`

这是 UI/业务层唯一看到的接口。

要求：

- 不 include 任何 `DO_NOT_TOUCH_*`；
- 不暴露 DNT 内部类型；
- 用中文详细解释：
  - 为什么有这层 wrapper；
  - 为什么必须独立 Worker；
  - 为什么初始化与测量必须同 Task；
  - 为什么取消只能发生在 DNT 调用边界；
  - 哪些字段是 DNT 原样返回；
  - 哪些字段只是纯数学转换。

#### `lcr_api.cpp`

这是唯一允许：

```cpp
#include "DO_NOT_TOUCH_lcr_api.h"
```

的非 DO_NOT_TOUCH 生产文件。

所有 DNT API 均从这里调用。

这样还可以避免 `DO_NOT_TOUCH_lcr_api.h` 连带包含的底层头文件在多个 Arduino `.cpp` translation unit 中被重复展开，降低全局对象/函数重复定义风险。

---

### 4.2 应用层结果类型

建议：

```cpp
enum class AppLcrStatus : int {
    Ok = 0,
    Busy,
    NotReady,
    QueueFull,
    Cancelled,
    BackendError
};

struct AppZPoint {
    double fReq;
    double fAct;
    double reOhm;
    double imOhm;
    double magOhm;
    double phaseDeg;
    double D;
    double Q;
    char apiType;       // 'R' / 'C' / 'L'
    int apiStatus;      // 原始 LcrApiStatus
};

struct AppWPoint {
    double fReq;
    double fAct;
    double hMag;
    double hDb;
    double phaseDeg;

    // 只由 API 的 magnitude/phase 纯数学换算
    double reH;
    double imH;

    int apiStatus;
};

struct AppCalcResult {
    char type;
    double rs;
    double cs;
    double ls;
    double rp;
    double cp;
    double lp;
    double D;
    double Q;
    int apiStatus;
};
```

`reH/imH` 的来源严格固定：

```text
phaseRad = phaseDeg * π / 180
reH = hMag * cos(phaseRad)
imH = hMag * sin(phaseRad)
```

这不是新测量，只是数据表示转换。

---

### 4.3 Worker Job/Event 契约

建议：

```cpp
enum class LcrJobKind : uint8_t {
    SweepZChunk,
    SweepWChunk,
    MeasureAndCalcZ,
    SetTone,
    StopTone,
    Reset,
    SelfCheck,
    ReadCalibrationStatus
};

struct LcrJob {
    uint32_t id;
    LcrJobKind kind;

    // sweep chunk
    double fStartHz;
    double fStopHz;
    uint8_t pointCount;   // 只允许 2 或 3

    // 单频 / diagnostic tone
    double frequencyHz;
};

struct LcrEvent {
    uint32_t id;
    LcrJobKind kind;

    int backendStatus;
    uint8_t pointCount;

    AppZPoint z[3];
    AppWPoint w[3];
    AppCalcResult calc;
};
```

应用接口：

```cpp
bool lcrServiceBegin();
bool lcrServiceReady();

bool lcrServiceSubmit(const LcrJob& job);
bool lcrServiceTakeEvent(LcrEvent& event);

bool lcrServiceBusy();
void lcrServiceRequestCancel();
```

队列必须有固定上限；测量期间禁止无界 heap allocation。

---

### 4.4 Worker 初始化顺序

Worker task entry：

```text
1. lcr_api_set_diagnostics(false)
2. lcr_api_init()
3. 若成功，发布 READY
4. 若失败，发布 INIT_FAILED
5. 等待 job queue
6. 串行执行 job
7. 推送 event
```

`setup()` 可以在启动阶段等待 READY；这个一次性 boot wait 不影响“运行时 UI 非阻塞”的要求。

---

### 4.5 wrapper 内允许调用的 DNT API

只通过公开 API：

```text
lcr_api_init
lcr_api_reset
lcr_api_set_freq
lcr_api_measure_z
lcr_api_measure_w
lcr_api_sweep_z
lcr_api_sweep_w
lcr_api_calc
lcr_api_calibrate
lcr_api_cal_status
lcr_api_cal_dump
lcr_api_cal_restore
lcr_api_set_ranges
lcr_api_get_ranges
lcr_api_selfcheck
lcr_api_print_sample_rate
```

实际 UI 只实现当前需求所需要的子集。

禁止 wrapper 继续向下穿透调用 `lcr_measure_point`、`lcr_adc_capture` 等。

---

## 5. 在不改 DNT 的前提下实现 UI 非阻塞

DNT 测量 API 本身是同步函数，而且内部有 settle/capture 等等待。

不能为了“非阻塞”去改 DNT。

正确做法是：

```text
UI task / Arduino loop
      │
      ├── input
      ├── redraw
      ├── state machine
      │
      └── queue job
              │
              ▼
         LcrWorker
         同步调用 DNT
```

这样用户界面本身不会卡在一次完整测量调用中。

---

### 5.1 为什么完整 N 点 `lcr_api_sweep_z/w` 不适合直接一次调用

如果一次执行：

```cpp
lcr_api_sweep_z(f0, f1, 257, ...)
```

UI 虽然仍能运行，但：

- 无法得到逐点/短周期进度；
- 用户取消后无法在 257 点完成前停止；
- BLE/radio lock 会长时间无法得到安全结束确认。

因此使用官方 sweep API 的**小块调用**。

---

### 5.2 2/3 点 Chunk 方案

原则：

- 普通 chunk：2 点；
- 总点数为奇数时，最后一块 3 点。

例如：

```text
N=8:
[0,1] [2,3] [4,5] [6,7]

N=9:
[0,1] [2,3] [4,5] [6,7,8]
```

全局请求频率是几何序列。

连续 2 点显然等于 DNT 2 点 log sweep 的两个端点。

连续 3 个几何点满足：

```text
f_mid = sqrt(f_start * f_end)
```

因此 DNT 的 3 点 log sweep 会得到同一请求序列。

应用层可以计算**请求频率表用于调度和进度显示**，但实际频率输出仍全部由：

```text
lcr_api_sweep_z
lcr_api_sweep_w
```

执行。

---

### 5.3 取消语义必须真实

旧注释中的“Back 立即 cancel → safe-off”在 DNT 同步 API 下无法保证。

新语义：

```text
用户 Back
→ 标记 cancelRequested
→ 当前 2/3 点 DNT chunk 完成
→ 不提交下一 chunk
→ Worker 调 lcr_api_set_freq(0)
→ tone stop 完成
→ 解除 measurement lock
→ Cancelled
```

UI 显示：

```text
正在停止
当前测量块完成后停止
```

而不是声称瞬间停止。

---

### 5.4 Dataset seal 的必要条件

只有同时满足：

1. 最后一块 DNT 测量已返回；
2. 不再有 pending measurement job；
3. `lcr_api_set_freq(0)` 已完成；
4. measurement lock 已结束；
5. 数据已经固定不可变；

才允许：

```text
TransferReady
```

以及启动 BLE。

---

## 6. 模式 1：单元件 R/C/L

目标：

- 输入未知单元件；
- 判断 R/C/L；
- R 显示 R；
- C 显示 C；
- L 显示 L + DCR；
- 不启动 BLE。

---

### 6.1 测量只使用 API

使用 3–5 个几何分布频率点。

每点的：

```text
fAct
Re(Z)
Im(Z)
|Z|
phase
D
Q
type
```

直接来自 `LcrZPoint`。

对于每个有效 Z 点，可进一步通过 Worker 调用：

```cpp
lcr_api_calc(
    fAct,
    z_re,
    z_im,
    false,
    &result
);
```

这里 `apply_calib=false` 是强约束。

原因：

`lcr_api_measure_z()` 已经完成测量链校准，再用 `true` 会二次校准。

---

### 6.2 不再使用旧 `component_meter` 的 raw ADC quality 权重

旧 `component_meter.cpp` 依赖：

```text
residualRmsA
residualRmsB
amplitudeA
amplitudeB
```

这些属于旧自定义 sine-fit 链。

DNT API 没有暴露它们，所以禁止：

- 填假值；
- 用 0 代替；
- 再去读 DNT 内部 ADC buffer。

---

### 6.3 分类规则

首版用保守规则：

- 有效点 < 3 → `UNKNOWN`；
- 各点 `apiType` 应一致；
- 若 5 点中只有 1 点不一致，可以允许配置成“4/5 一致才通过”；
- 若类型不稳定 → `UNKNOWN`；
- 不强行“猜一个最接近”。

这属于应用层可靠性判据，不是重新测量。

---

### 6.4 数值汇总

只对 API `calc` 的有效结果做鲁棒聚合：

```text
R:
  median(rs)

C:
  median(cs)

L:
  median(ls)
  DCR = median(rs)
```

过滤 NaN/Inf。

L 的 DCR 若为负数：

- 不允许 `max(0, dcr)`；
- 应判为测量/校准/模型异常；
- UI 给 WARN/UNKNOWN。

因为项目物理模型中 DCR 应 `>= 0`。

---

### 6.5 UI 质量显示

删除旧的伪 `wRMSE` 质量表达。

改成可以被数据真实支撑的字段，例如：

```text
INDUCTOR
L    12.4 mH
DCR   8.3 Ω

valid      5/5
consistent 5/5
Q          ...
```

---

## 7. 模式 2：单端口扫频

### 7.1 保留页面流程

```text
Config
→ Measuring
→ Stopping/Sealing
→ TransferReady
→ BLE
```

UI 不再知道 ADC、sine fit、calibration。

它只与 `SweepEngine` 交互。

---

### 7.2 `SweepEngine` 新职责

`SweepEngine` 只负责：

- 构造 requested frequency plan；
- 把计划切成 2/3 点 chunk；
- 给 `LcrService` 提交 `SweepZChunk`；
- 收 event；
- 记录成功/失败；
- 处理 cancel；
- 触发 StopTone；
- seal Dataset；
- 生成 CSV/CRC。

它不负责：

- 产生波形；
- ADC；
- fit；
- calibration；
- 计算阻抗。

---

### 7.3 单端口 Dataset 真源

每个有效点：

```text
CSV f  = API f_act
CSV re = API z_re
CSV im = API z_im
```

禁止改用 requestedHz 作为拟合频率。

失败点：

- 保存在 diagnostics；
- 不写 0；
- 不进入拟合 CSV。

---

### 7.4 最低有效点数

当前网页 `parseZCsv()` / FitView 至少需要 4 个有效点。

因此：

```text
validPoints < 4
```

时不应生成“可供网站拟合”的 TransferReady Dataset。

本机显示：

```text
DATA INSUFFICIENT
```

并允许重新测量。

---

## 8. 模式 3：双端口扫频

只用：

```text
lcr_api_measure_w
lcr_api_sweep_w
```

DNT 当前定义：

```text
H = Vout / Vin
h_db = 20 log10 |H|
phase = arg(H)
```

应用层只做：

```text
rad = phase_deg * π / 180
re_h = h_mag cos(rad)
im_h = h_mag sin(rad)
```

CSV：

```csv
f,re_h,im_h
```

网站继续：

```text
Re/Im
→ |H|
→ dB
→ phase
→ Bode/Nyquist
```

---

### 8.1 双端口校准声明必须诚实

当前 DNT W path 的源码注释明确说明其为 raw chain / no calib。

因此首版：

- 不得复用单端口 calibration id 来暗示双端口已校准；
- 不得在应用层自行补一套 H 校准；
- 网站/UI 应标识当前 H 为 API raw W-path 结果。

若未来要增加双端口校准，应先由硬件测量核心提供正式 API，再升级应用层。

---

## 9. Dataset / BLE 协议

### 9.1 BLE GATT v1 保持不变

以下不需要重做：

- Service UUID；
- Control characteristic；
- Status characteristic；
- Metadata characteristic；
- Data characteristic；
- seq；
- byte_count；
- CRC32；
- START_TRANSFER；
- RESTART_TRANSFER；
- ABORT_TRANSFER。

这些与测量硬件无关，当前设计合理。

---

### 9.2 当前假元数据必须删除

旧 `SweepEngine` 会写：

```text
driveVrms = 1.05
calibrationId = "factory-none"
```

它们来自旧 PCM5102A / custom calibration 架构。

在新架构中：

> DNT API 没有暴露的硬件量，应用层不得杜撰。

因此：

- 不再硬编码 1.05 Vrms；
- 不伪造“唯一校准 ID”；
- 用 `lcr_api_cal_status()` 能实际得到的状态进行诚实描述。

---

### 9.3 建议 CSV schema 升级到 v2

当前 `CSV_SCHEMA_V1.md` 已声明“字段语义改变需 bump schema”。

因此建议：

```text
lcr-z-csv-v2
lcr-h-csv-v2
```

数据列仍保持 3 列，避免影响拟合核心。

#### one-port v2

```csv
# lcr-dataset=one-port-z
# schema=lcr-z-csv-v2
# protocol=1
# firmware=...
# measurement_backend=DO_NOT_TOUCH_lcr_api
# calibration_state=...
f,re,im
...
```

#### two-port v2

```csv
# lcr-dataset=two-port-h
# schema=lcr-h-csv-v2
# protocol=1
# firmware=...
# measurement_backend=DO_NOT_TOUCH_lcr_api
# calibration_state=raw_w_path
f,re_h,im_h
...
```

如果未来 DNT API 提供真正的 calibration profile ID，再新增真实 ID。

---

### 9.4 网站兼容策略

过渡期 parser 同时接受：

```text
v1
v2
```

v2 的数字数据依然转换成：

```text
ZPoint { f, re, im }
```

所以 Try1/Try2/Try3 C++/WASM 不需要因为硬件改造而修改。

---

## 10. GPIO 重新分区

### 10.1 DNT 测量 GPIO 一律保留

UI/业务层禁止占用：

```text
GPIO1
GPIO2

GPIO8
GPIO9
GPIO10
GPIO11
GPIO12
GPIO13
GPIO14
GPIO18

GPIO15
GPIO16
GPIO17
```

---

### 10.2 ESP32-S3/N16R8 其它限制

同时遵循 ESP32-S3 官方限制：

- GPIO0/3/45/46：strapping，需要谨慎；
- GPIO19/20：USB；
- GPIO39–42：JTAG 默认功能；
- GPIO43/44：UART0；
- N16R8 octal PSRAM 使用 GPIO33–37；
- GPIO26–32 属于 Flash/PSRAM 域，不分配给 UI。

---

### 10.3 当前按键/编码器可保留，但要做实物连续性确认

当前：

```text
UP       GPIO47
DOWN     GPIO48
BACK     GPIO41
OK       GPIO42
ENC A    GPIO38
ENC B    GPIO39
ENC SW   GPIO40
```

没有与 DNT GPIO 冲突。

因此**软件上可保留**，前提是实际设备接线确实如此。

GPIO39–42 属 JTAG 相关管脚，因此产品固件不能同时依赖 pad JTAG。

---

### 10.4 TFT 必须离开 GPIO10–14

旧 TFT：

```text
SCK  12
MOSI 11
CS   10
DC   14
RST  13
```

全部与 LCD_CAM D3–D7 冲突，不可使用。

若 TFT 是外接线，可采用一个干净的候选映射：

```text
SCK   GPIO4
MOSI  GPIO5
CS    GPIO6
DC    GPIO7
RST   GPIO21
MISO  不使用
```

**注意：这是一组推荐的“待实物确认”接法，不是声称当前 PCB 已经这样连接。**

落地前必须：

1. 看真实 PCB/线束；
2. 做 continuity；
3. 若当前 TFT 固定在 10–14，则必须改线/改板；
4. 实测通过后再把 GPIO4/5/6/7/21 固化进 `board_profile`。

---

### 10.5 TFT SPI 时钟

ST7735S 4-wire Serial Interface 的 write serial clock cycle 最小值为 66 ns。

10 MHz：

```text
T = 100 ns
```

有保守余量。

因此首版仍用 10 MHz。

后续若升频，必须：

- 实屏长时间刷新；
- 边界/色块压力测试；
- 最好使用逻辑分析仪确认波形。

---

### 10.6 `board_profile` 最终职责

它只描述 UI 外设：

```cpp
struct BoardProfile {
    // TFT
    int spiSck;
    int spiMosi;
    int spiMiso;
    int tftCs;
    int tftDc;
    int tftRst;
    uint32_t tftSpiHz;

    // input
    int keyUp;
    int keyDown;
    int keyBack;
    int keyOk;
    int encA;
    int encB;
    int encSw;
};
```

从 profile 中删除：

- ADC pin；
- excitation pin；
- I2S；
- TIA；
- frontEndEnable；
- rangeSelect；
- 采样率；
- 采样电阻；
- 任何测量链参数。

这些全部属于 DNT。

---

## 11. 文件级改造矩阵

| 文件/目录 | 操作 | 最终语义 |
|---|---|---|
| `ino/LCR_UI/DO_NOT_TOUCH_*` | **绝对不改** | 硬件验证核心 |
| `DO_NOT_TOUCH_EXAMPLE.ino.example` | **绝对不改** | API 参考 |
| `ino/LCR_UI/lcr_api.h` | **新增** | 应用层测量接口，中文说明 |
| `ino/LCR_UI/lcr_api.cpp` | **新增** | 唯一 include DNT API，Worker |
| `measurement_engine.*` | **退出生产固件** | 不再存在第二套测量 |
| `adc_capture.*` | **退出生产固件** | 不再自写 ADC |
| `excitation_driver.*` | **退出生产固件** | 不再 I2S/PCM5102 |
| `dsp_fit.*` | **退出生产固件** | 不再自写 ADC sine fit |
| `calibration.*` | **退出生产固件** | 不再第二套校准 |
| `sine_plan.*` | **退出生产固件** | 不再第二套物理激励 |
| `measurement_types.*` | **重写/精简** | Dataset/任务/状态类型 |
| `sweep_engine.*` | **重写** | DNT chunk 编排/取消/seal |
| `component_meter.*` | **重写** | 只汇总 API type/calc |
| `screens.h` | **修改** | 删除 MeasurementEngine 依赖 |
| `screen_component.cpp` | **修改** | 调 service |
| `screen_oneport.cpp` | **修改** | UI 保留，后端换 service |
| `screen_twoport.cpp` | **修改** | UI 保留，后端换 service |
| `screen_siggen.cpp` | **修改** | 只通过 wrapper 调 set_freq |
| `LCR_UI.ino` | **重新装配** | 只负责 setup/loop/UI/service |
| `board_profile.*` | **重写** | UI-only |
| `display.*` | **尽量保留** | 当前显示抽象可继续用 |
| `input.*` | **尽量保留** | 非阻塞轮询逻辑合理 |
| `plot.*` | **保留** | 只负责图形 |
| `ble_protocol.h` | **保留** | GATT v1 framing |
| `radio_manager.*` | **保留/小改** | 与新 measurement lock 对接 |
| `radio_lock.*` | **改状态语义** | LcrService vs Radio |
| `dataset.*` | **修改** | v2 metadata，CRC 保留 |
| `fw_version.h` | **升级** | 新固件版本 |
| `docs/HARDWARE_MAPPING.md` | **重写** | DNT 真硬件 + UI 真接线 |
| `ino/README.md` | **更新** | 删除旧硬件链 |
| 根 `README.md` | **更新** | 同步 |
| `frontend/src/docs/esp32.md` | **更新** | 新固件/Schema |
| `protocol/CSV_SCHEMA_V1.md` | **保留** | 历史兼容 |
| `protocol/CSV_SCHEMA_V2.md` | **新增** | 新元数据契约 |
| `frontend/src/lib/ble/*` | **小改** | progress + v2 |
| `frontend/src/lib/csv.ts` | **兼容/测试** | v1/v2 → 同 ZPoint |
| `frontend/src/lib/twoPortCsv.ts` | **兼容/测试** | v1/v2 |
| Try1/Try2/Try3 C++/WASM | **保留** | 不因硬件层改造重写 |

---

## 12. `LCR_UI.ino` 最终只做装配

### setup

```text
Serial/log policy
→ board UI pins
→ display
→ input
→ lcrServiceBegin()
    └─ Worker 内执行 lcr_api_init()
→ 等待 READY / 显示 init error
→ 确认 BLE Off
→ main menu
```

### loop

```text
input.poll()
→ dispatch event
→ current screen onTick()
→ 消费 LcrService event
→ BLE active 时 radio.poll()
→ 短 yield
```

`LCR_UI.ino` 不得：

- 配 ADC；
- 配 LCD_CAM；
- 直接写 74HC595；
- 创建 sine buffer；
- 从 raw ADC 算 Z；
- 自己校准；
- 自己 autorange。

---

## 13. BLE 与测量的互斥规则

保留“测量期间无线静默”。

新 invariant：

```text
LcrSessionState ∈ {
    Starting,
    Measuring,
    Stopping
}
    ⇒ RadioState == Off
```

以及：

```text
RadioState != Off
    ⇒ LcrSessionState ∈ {
        Idle,
        TransferReady
    }
```

关键点：

- chunk 之间也不能开 BLE；
- stop-tone 完成前不能 seal；
- seal 后才允许 BLE init。

---

## 14. 网站侧具体改动

### 14.1 单端口

保持：

```text
receiveDataset()
→ ONE_PORT_Z
→ parseZCsv()
→ loadPoints()
→ runFitJob()
```

不能再加一套“ESP32 专用拟合数据格式”。

---

### 14.2 双端口

保持：

```text
receiveDataset()
→ TWO_PORT_H
→ parseHCsv()
→ measured H
→ Bode/Nyquist
```

---

### 14.3 修复当前 BLE progress

当前 store 中有：

```text
progress: number
```

但接收过程中并没有逐帧更新。

改：

```ts
export async function receiveDataset(
  session: LcrDeviceSession,
  onProgress?: (
    received: number,
    total: number
  ) => void,
): Promise<DeviceDataset>
```

每次 assembler 收到新 payload 后：

```ts
onProgress?.(
  assembler.bytesReceived,
  meta.byte_count,
)
```

Store：

```ts
this.progress = received / total
```

这样屏幕才能真正显示 0→100%。

---

### 14.4 Web Bluetooth 部署约束

保持现有 feature gate。

必须在用户文档中写明：

- Web Bluetooth 需要 secure context；
- 正式部署使用 HTTPS；
- 设备 chooser 需要用户动作/权限；
- 浏览器兼容性并非全平台一致。

不为了兼容性去增加无必要的服务器 BLE 代理。

---

## 15. 强制 CI：让以后无法再次误改硬件核心

重写 `ino/tools/static_check.sh`。

---

### Gate A — DO_NOT_TOUCH SHA-256 manifest

对用户明确列出的文件建立 hash manifest：

```text
DO_NOT_TOUCH_freq_calc.h
DO_NOT_TOUCH_hong.h
DO_NOT_TOUCH_lcr_adc.h
DO_NOT_TOUCH_lcr_api.h
DO_NOT_TOUCH_lcr_calib_core.h
DO_NOT_TOUCH_lcr_calib.h
DO_NOT_TOUCH_lcr_diag.h
DO_NOT_TOUCH_lcr_measure.h
DO_NOT_TOUCH_lcr_tone.h
DO_NOT_TOUCH_sinwave.h
DO_NOT_TOUCH_EXAMPLE.ino.example
```

任何 hash 变化立即失败。

如果将来硬件团队真的要更新这些文件，应走单独明确流程并同步更新 manifest，而不是应用提交顺手修改。

---

### Gate B — 禁止引用 `DO_NOT_TOUCH_hong.h`

CI 搜索：

```text
DO_NOT_TOUCH_hong.h
```

除：

- manifest；
- ATTENTION/说明性文档；

之外，生产代码/测试出现即失败。

---

### Gate C — DNT API 只能有一个入口

CI 断言：

```text
非 DO_NOT_TOUCH 生产源码
#include "DO_NOT_TOUCH_lcr_api.h"
```

只能出现一次：

```text
ino/LCR_UI/lcr_api.cpp
```

---

### Gate D — 禁止绕过 API

在非 DNT 生产源码中禁止：

```text
lcr_adc_
lcr_measure_
out_freq
out_sin
stop_sin
adc_continuous_
lcd_cam
gdma_
HC595_PIN_
```

注意 gate 要排除：

- DNT 文件本身；
- 文档；
- manifest。

---

### Gate E — 禁止恢复错误硬件假设

迁移完成后生产源码出现下列词直接失败：

```text
PCM5102
PCM5102A
excitation_driver
adc_capture
I2S excitation
```

---

### Gate F — UI GPIO collision

自动检查 `board_profile` 的 UI pin 不得出现在：

```text
{1,2,8,9,10,11,12,13,14,15,16,17,18}
```

并检查 N16R8 受限引脚。

---

### Gate G — BLE/measurement lock

静态检查 + host state-machine test 双重保证：

```text
measurement active && radio active
```

永远不可成立。

---

### Gate H — schema/version

统一检查：

- firmware；
- BLE protocol；
- Z schema；
- H schema；
- frontend accepted schema。

---

## 16. Build 规则

`ino/tools/build_check.sh`：

1. 保留 ESP32-S3 N16R8 FQBN/PSRAM 配置；
2. TFT flag 改为最终实物确认后的非冲突 GPIO；
3. 编译整个生产 sketch；
4. 确保只出现一个 `lcr_api.cpp` DNT include；
5. 不再 inject TFT GPIO10–14；
6. 任何 multiple definition / driver conflict 都视为架构错误，而不是通过 linker workaround 掩盖。

Arduino sketch 目录会自动编译其中 `.cpp`。

因此旧物理驱动文件不能只是“不调用”，而应：

- 删除；或
- 移出生产 sketch 源目录。

否则它们仍可能参与编译、占资源或再次被误引用。

---

## 17. 测试方案

### 17.1 Host tests

Host 测试不再模拟另一套 ADC。

只 mock “LcrService backend result”，测试编排层。

必须覆盖：

1. 全局 log requested grid；
2. 偶数 N 的 2 点 chunk；
3. 奇数 N 的 2+3 chunk；
4. chunk endpoint 与全局几何频率一致；
5. cancel 后不再提交新 chunk；
6. StopTone 未完成不能 TransferReady；
7. Z API 失败点不进入 CSV；
8. NaN/Inf 不进入 CSV；
9. 少于 4 个 Z 点不可进入网站拟合状态；
10. H magnitude/phase → Re/Im；
11. component API type 一致性；
12. R `rs` 聚合；
13. C `cs` 聚合；
14. L `ls` + `rs(DCR)` 聚合；
15. 负 DCR 不 clamp；
16. CRC32；
17. BLE frame seq；
18. Metadata v2。

旧以下测试职责删除：

- application sine fit；
- custom ADC timing；
- custom calibration；
- PCM5102 sine planning。

---

### 17.2 Frontend tests

至少：

- v1 one-port fixture 继续可读；
- v2 one-port fixture 可读；
- 两者转成相同 `ZPoint[]`；
- v1 two-port fixture；
- v2 two-port fixture；
- CRC error；
- seq gap；
- one-port BLE → parser → fit input；
- two-port BLE → parser → gain/phase；
- progress 单调从 0 到 1。

---

### 17.3 实板硬件验收

必须执行：

1. Worker 初始化 DNT 成功，不死锁；
2. normal mode diagnostics off；
3. 标准 R 重复测量；
4. 标准 C 重复测量；
5. 标准 L，显示 L + DCR；
6. 10 Hz–10 kHz 单端口产品扫频；
7. 如果要使用 DNT 更高频能力，作为独立测试，不自动扩大产品频段声明；
8. LCD_CAM 工作时 TFT 不乱屏；
9. 测量期间输入仍响应；
10. cancel 在当前 2/3 点 chunk 后停止；
11. cancel/final 均确认 tone stop；
12. 测量全过程 BLE Off；
13. seal 后才能 advertising；
14. 网站收到的 CSV CRC 完全一致；
15. one-port 连续 ≥20 次 measure→BLE→disconnect；
16. two-port 连续重复；
17. 已知双端口网络的 H 变化趋势正确；
18. 反复连接无 task leak / heap leak / stale GATT；
19. calibration status 显示真实；
20. TFT/按键/编码器逐脚 continuity 记录进硬件文档。

---

## 18. 理论与测量的边界

项目理论已经明确：

> 未建模的前端频率相关系统误差不能可靠地由拓扑拟合“补偿”。

因此最终数据流必须是：

```text
已验证硬件测量/校准 API
          ↓
   calibrated complex Z
          ↓
         CSV
          ↓
 Try1 / Try2 / Try3
```

而不是：

```text
未验证的新 ESP32 测量算法
          ↓
     有系统误差的 Z
          ↓
  让 Try1/2/3 想办法拟合
```

这也是这次必须彻底删除第二套测量实现，而不是继续修改它的根本原因。

双端口当前 W path 既然是 raw/no-calib，就应明确标注 raw；不能用拟合算法掩盖这个事实。

---

## 19. 推荐实施顺序

### Phase A — 先锁死 DO_NOT_TOUCH 边界

- 增加 hash manifest；
- 禁止 hong；
- 禁止低层 DNT 符号；
- 明确 reserved GPIO；
- 将旧硬件映射标为失效。

**退出条件：** CI 已经能阻止未来误改/绕过 DNT。

---

### Phase B — 新建 `lcr_api` wrapper/service

- `lcr_api.h/.cpp`；
- dedicated Worker；
- Worker 内 init；
- Z/W/calc/stop jobs；
- 基础服务测试。

**退出条件：** 新 wrapper 能在实板上单独完成 API 测量，应用层没有任何低层调用。

---

### Phase C — 清除旧物理测量栈

- measurement_engine；
- adc_capture；
- excitation_driver；
- dsp_fit；
- calibration；
- sine_plan；

退出生产固件。

**退出条件：** production grep 不再出现 PCM5102/I2S/custom ADC 测量链。

---

### Phase D — 重新冻结 UI GPIO

- 实物检查 TFT；
- 必要时改线；
- 冻结 TFT 最终 pin；
- 更新 `board_profile`；
- 更新 `build_check.sh`；
- 更新 `HARDWARE_MAPPING.md`。

**退出条件：** GPIO 无冲突，LCD_CAM + TFT + input 同时 smoke test 通过。

---

### Phase E — 完成单元件模式

- API Z；
- API calc；
- type consistency；
- R/C/L；
- L+DCR。

**退出条件：** 标准 R/C/L 实板通过。

---

### Phase F — 完成单端口扫频

- 2/3 chunk；
- progress；
- bounded cancel；
- stop；
- seal；
- v2 CSV；
- BLE。

**退出条件：** ESP32 → BLE → FitView → WASM 完整打通。

---

### Phase G — 完成双端口

- W API；
- complex H；
- v2 CSV；
- BLE；
- SweepView。

**退出条件：** ESP32 → BLE → Bode/Nyquist 打通。

---

### Phase H — 网站/传输完善

- 实时 BLE progress；
- v1/v2 parser；
- golden fixture；
- 重复传输测试。

---

### Phase I — 发布前总验收

执行：

```text
static checks
ESP32 compile
host unit tests
frontend tests
WASM smoke/regression
real hardware tests
end-to-end BLE tests
```

全部通过后再进入 release。

---

## 20. 最终二次 Review 清单

方案落实后必须重新完整 review，以下每项都必须为 YES。

### DO_NOT_TOUCH

- [ ] 没有修改任何 `DO_NOT_TOUCH*` 文件。
- [ ] 没有引用 `DO_NOT_TOUCH_hong.h`。
- [ ] `DO_NOT_TOUCH_lcr_api.h` 只有一个非 DNT include 点。
- [ ] 应用层没有调用任何低层 DNT 硬件函数。

### 硬件

- [ ] 不再存在 PCM5102/DAC 芯片假设。
- [ ] 不再假设额外外部数字芯片。
- [ ] GPIO1/2、8–18 DNT 相关脚全部保留。
- [ ] TFT 已完全移出 GPIO10–14。
- [ ] `HARDWARE_MAPPING.md` 与真实 continuity 一致。

### 运行时

- [ ] DNT init 与全部测量在同一个 Worker task。
- [ ] UI loop 不执行同步测量。
- [ ] cancel 文案与真实 chunk-bounded 行为一致。
- [ ] StopTone 完成后才 seal。
- [ ] BLE 与测量绝不重叠。

### 数据

- [ ] one-port CSV 直接使用 API `f_act/z_re/z_im`。
- [ ] 没有二次校准。
- [ ] 失败点没有变成 0。
- [ ] two-port complex H 只来自 API magnitude/phase。
- [ ] 没有杜撰 drive voltage。
- [ ] 没有杜撰 calibration ID。
- [ ] byte_count + CRC32 对应 exact CSV bytes。

### 网站

- [ ] ONE_PORT_Z 仍进入 `parseZCsv → WASM`。
- [ ] TWO_PORT_H 仍进入 `parseHCsv → Bode/Nyquist`。
- [ ] v1 历史数据兼容。
- [ ] v2 新数据兼容。
- [ ] unsupported/insecure Web Bluetooth 有明确提示。

### 验证

- [ ] static gate 通过。
- [ ] ESP32-S3 production compile 通过。
- [ ] host test 通过。
- [ ] frontend test 通过。
- [ ] WASM regression 通过。
- [ ] R/C/L 实测通过。
- [ ] one-port end-to-end 通过。
- [ ] two-port end-to-end 通过。
- [ ] 连续重复运行稳定。

---

## 21. 本方案依据

### 仓库基线

审计基线：

```text
main @ d1e0f1f182eb960aa2c3adc09f1b7b85b76d23ed
```

重点审阅/交叉检索：

```text
ino/LCR_UI/DO_NOT_TOUCH_lcr_api.h
ino/LCR_UI/DO_NOT_TOUCH_lcr_adc.h
ino/LCR_UI/DO_NOT_TOUCH_lcr_measure.h
ino/LCR_UI/DO_NOT_TOUCH_sinwave.h
ino/LCR_UI/DO_NOT_TOUCH_EXAMPLE.ino.example

ino/LCR_UI/LCR_UI.ino
ino/LCR_UI/measurement_engine.cpp
ino/LCR_UI/sweep_engine.cpp
ino/LCR_UI/component_meter.cpp
ino/LCR_UI/measurement_types.h
ino/LCR_UI/board_profile.h
ino/LCR_UI/board_profile.cpp
ino/LCR_UI/radio_manager.cpp
ino/LCR_UI/radio_lock.h
ino/LCR_UI/dataset.h
ino/LCR_UI/dataset.cpp
ino/LCR_UI/screens.h
ino/LCR_UI/screen_component.cpp
ino/LCR_UI/screen_oneport.cpp
ino/LCR_UI/screen_twoport.cpp
ino/LCR_UI/screen_siggen.cpp

ino/tools/static_check.sh
ino/tools/build_check.sh
ino/tools/run_tests.sh

docs/HARDWARE_MAPPING.md
protocol/CSV_SCHEMA_V1.md

frontend/src/lib/ble/lcrDevice.ts
frontend/src/lib/ble/protocol.ts
frontend/src/store/device.ts
frontend/src/lib/csv.ts
frontend/src/lib/twoPortCsv.ts
frontend/src/views/FitView.vue
frontend/src/views/SweepView.vue
```

并对 `ino/` 做了硬件相关符号全局检索，包括：

```text
pinMode
PCM5102
DO_NOT_TOUCH_hong.h
```

用于确认残留旧硬件路径和低层 GPIO 使用位置。

---

### 项目理论资料

- `LCRTheory_rendered.pdf`
- `LCRTheory_rendered.md`

采用其中与本次集成直接相关的原则：

- 拟合输入应是可信的复阻抗点；
- 测量系统误差不能由拓扑优化器可靠替代校准；
- 实际频率应进入拟合数据；
- 端口数据和拟合模型职责分离。

---

### 官方/数据手册资料

#### ESP32-S3

Espressif ESP32-S3 Series Datasheet：

https://documentation.espressif.com/esp32_s3_datasheet_en.pdf

ESP32-S3 Technical Reference Manual / LCD_CAM：

https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf

ESP-IDF GPIO：

https://docs.espressif.com/projects/esp-idf/en/release-v5.3/esp32s3/api-reference/peripherals/gpio.html

ESP-IDF ADC Continuous：

https://docs.espressif.com/projects/esp-idf/en/release-v5.3/esp32s3/api-reference/peripherals/adc_continuous.html

ESP32-S3 Hardware Design Guidelines：

https://documentation.espressif.com/esp-hardware-design-guidelines/en/latest/esp32s3/index.html

关键用于确认：

- GPIO matrix；
- LCD_CAM 可映射 GPIO；
- strapping/USB/JTAG/UART 管脚注意事项；
- N16R8 octal PSRAM 相关 GPIO；
- ADC continuous + DMA 语义。

#### 74HC595

Nexperia：

https://www.nexperia.com/product/74HC595BZ

用于确认：

- SHCP 上升沿移位；
- STCP 上升沿把 shift register 内容锁存到 storage register；
- 独立 shift/storage clocks；
- Q7S 级联。

应用层不重新实现这些控制，仅用于核对 DO_NOT_TOUCH 硬件描述。

#### ST7735S

ST7735S V1.1：

https://files.waveshare.com/upload/e/e2/ST7735S_V1.1_20111121.pdf

用于确认 4-line serial write timing；write serial clock cycle 最小 66 ns。

#### Web Bluetooth

MDN Web Bluetooth：

https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API

MDN `Bluetooth.requestDevice()`：

https://developer.mozilla.org/en-US/docs/Web/API/Bluetooth/requestDevice

用于确认：

- secure context；
- 用户权限/chooser；
- 浏览器兼容性限制。

---

## 22. 最终规范性结论

本项目的测量核心应被视为：

> **已经过硬件验证的不可变依赖，而不是 UI 项目可以重构的内部模块。**

最终生产固件应当只有一条测量依赖边：

```text
应用层
   ↓
新建 lcr_api wrapper
   ↓
DO_NOT_TOUCH_lcr_api.h
```

这条边以上：

- 可以做 UI；
- 可以做状态机；
- 可以做任务队列；
- 可以做 Dataset；
- 可以做 BLE；
- 可以做 CSV；
- 可以做网站交互。

这条边以上不能：

- 自己采 ADC；
- 自己生成物理频率；
- 自己控制 74HC595；
- 自己做 autorange；
- 自己做校准；
- 自己从 raw sample 推阻抗；
- 调用任何被 `DO_NOT_TOUCH_lcr_api.h` 包装过的底层函数。

这是让现有 UI/BLE/WASM 优点保留下来，同时确保固件严格服从真实硬件结构的核心设计。
