// ============================================================================
// measurement_types.h —— 测量语义的统一类型层（host 可编译，无 Arduino 依赖）
// ----------------------------------------------------------------------------
// 数学真源（与项目理论文档一致，plan.md §3）：
//   单端口：Z(f) = V_DUT(f) / I_DUT(f)
//   电阻：  Z = R
//   电容：  Z = 1/(jωC)
//   电感：  Z = R_d + jωL（物理电感必须同时报告 L 与 DCR）
//   双端口：H(f) = Vout(f) / Vin(f)；gainDb = 20·log10|H|；
//           phase = arg(H) = arg(Vout) − arg(Vin)
// 旧的「-20·log10(out/in) + φin−φout」倒数语义只存在于 legacy 标注下，
// 业务路径禁止使用。
// ============================================================================

#pragma once

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// 编译期上限（仪器级约束；RAM 预算见 ino/README.md）
// ---------------------------------------------------------------------------
static constexpr uint16_t SWEEP_MAX_POINTS = 257;   // 50 点/十倍频 × 3 十倍频 + 1
static constexpr double INSTRUMENT_F_MIN_HZ = 10.0; // 任务书：10 ~ 10000 Hz
static constexpr double INSTRUMENT_F_MAX_HZ = 10000.0;
// 内部 ADC 双通道交错的质量保证频段上限（~41.7 ksps/ch → ≥16 点/周期）；
// 高于此频率仍可测但质量下降。换外部同步 ADC 后只改这里（plan.md §2.2）。
static constexpr double DUAL_CHANNEL_QUALITY_FMAX_HZ = 2000.0;

// ---------------------------------------------------------------------------
// 测量类别（彻底移除 bool isOnePort）
// ---------------------------------------------------------------------------
enum class MeasurementKind : uint8_t {
    OnePortImpedance,   // Z = V/I
    TwoPortTransfer,    // H = Vout/Vin
};

// ---------------------------------------------------------------------------
// 状态码（一次测量的全生命周期结果）
// ---------------------------------------------------------------------------
enum class MeasurementStatus : uint8_t {
    Ok = 0,
    InvalidConfig,
    ExcitationFail,
    AdcFail,
    AdcOverrun,          // DMA overflow → 拒绝用残缺 buffer 拟合
    Clipped,             // ADC 削顶
    SignalTooSmall,
    FitSingular,         // 正弦拟合法方程病态
    FrequencyMismatch,   // actualHz 与 requested 偏差过大
    CalibrationMissing,
    CalibrationStale,    // 频点在已校准频段之外
    Cancelled,
    InternalError,
};

const char* measurementStatusText(MeasurementStatus s);

// ---------------------------------------------------------------------------
// 采样通道
// ---------------------------------------------------------------------------
enum class CaptureChannel : uint8_t {
    VoltageDut,          // 单端口 V
    CurrentSense,        // 单端口 I（采样电阻电压）
    Port2Input,          // 双端口 Vin
    Port2Output,         // 双端口 Vout
};

enum class CaptureStatus : uint8_t {
    Ok = 0,
    InvalidRequest,
    Busy,
    UnsupportedChannel,
    DmaOverrun,
    DriverError,
};

// ---------------------------------------------------------------------------
// 正弦拟合的样本序列与拟合器声明在 dsp_fit.h（SampleSeries / sineFitTimed），
// 本文件不重复定义，避免第二份拟合契约。相位差按 Δφ = 2πf·Δt 显式补偿
// （plan.md §3.3：拟合 API 不隐含两通道样本同时采样）。
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 采集请求 / 结果（AdcCapture 接口，plan.md §2.2）
// ---------------------------------------------------------------------------
struct CaptureRequest {
    CaptureChannel channels[2];
    uint8_t channelCount;          // 本项目业务固定 2
    double excitationHz;           // 用于选择采样率（每周期目标点数）
    uint32_t targetSampleRateHz;   // 每通道目标采样率
    uint16_t captureCycles;        // 捕获的信号周期数
    uint16_t minSamplesPerChannel;
};

struct TimedSamples {
    int16_t* samples;              // 已校准（adc_cali）前的原始码？——否：
                                   // samples 为 raw code；电压换算层见下
    size_t count;
    double dt;                     // 秒
    double t0;                     // 相对 capture 起点偏移（通道 skew）
};

// ---------------------------------------------------------------------------
// 激励源（plan.md §2.3）
// ---------------------------------------------------------------------------
enum class Waveform : uint8_t { Sine };   // 测量固定 Sine；其它只留诊断

enum class ExcitationStatus : uint8_t {
    Ok = 0, Invalid, DriverError, Stopped,
};

struct ExcitationConfig {
    double requestedHz;
    double requestedVrms;    // 仅为意向；v1 硬件幅度固定时被忽略并如实上报
    Waveform waveform;
};

struct ExcitationState {
    double actualHz;         // 有理数构造的精确频率（I2S Fs·K/L）
    double actualVrms;       // calibrated nominal drive（见 board_profile）
    bool amplitudeCalibrated;
};

// ---------------------------------------------------------------------------
// 单点测量请求 / 质量 / 结果（plan.md §3.1）
// ---------------------------------------------------------------------------
struct MeasurementRequest {
    MeasurementKind kind;
    double requestedHz;
    double driveVrms;              // 意向值；实际语义见 ExcitationState
    uint16_t settleCycles;         // 建立期以信号周期计
    uint16_t captureCycles;
    uint16_t minSamplesPerChannel;
};

struct MeasurementQuality {
    MeasurementStatus status;
    bool clippedChA;
    bool clippedChB;
    bool frequencyLocked;          // |actualHz-requestedHz| 在容差内
    double residualRmsA;
    double residualRmsB;
    double amplitudeA;
    double amplitudeB;
    double channelSkewSeconds;     // 本点使用的 V/I 时差补偿值
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

// ---------------------------------------------------------------------------
// 扫频（plan.md §5.2）
// ---------------------------------------------------------------------------
struct SweepConfig {
    MeasurementKind kind;
    double driveVrms;
    double fStartHz;
    double fStopHz;
    uint16_t pointsPerDecade;
    uint16_t maxPoints;            // 编译期上限（当前 257）
    bool logSpacing;
};

enum class SweepStatus : uint8_t {
    Ok = 0, InvalidConfig, Busy, EngineError, Cancelled,
};

enum class SweepState : uint8_t {
    Idle,
    Measuring,        // 含每个频点的 settle→capture→fit→store
    Sealing,          // 停激励/停 ADC/静默保护/封存
    TransferReady,    // dataset 已 seal + CSV 已生成，此时才允许开 BLE
    Error,
    Cancelled,
};

// 频率表生成（对数/线性几何分布；log 下首尾精确 fStart/fStop）
// 返回写入个数；buffers 太小返回 0。host 可测。
size_t buildFrequencyPlan(const SweepConfig& cfg, double* freqs, size_t cap);

// ---------------------------------------------------------------------------
// 状态文本（UI/日志用）
// ---------------------------------------------------------------------------
const char* sweepStateText(SweepState s);
const char* measurementKindText(MeasurementKind k);
