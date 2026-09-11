// ============================================================================
// measurement_types.h —— 编排层共享的纯类型（host 可编译，无 Arduino 依赖）
// ----------------------------------------------------------------------------
// v4.1.0 重构后的职责边界（见 plan.md）：
//   * 一切物理测量（频率生成、ADC、自动量程、校准、Z/H 计算）都在
//     DO_NOT_TOUCH_lcr_api.h 内完成，本文件不再描述任何采集/激励细节；
//   * 本文件只保留：测量类别、扫频配置/状态、频率表规划、以及
//     H 幅度/相位 -> 复数直角坐标的纯数学换算声明；
//   * 旧的 ExcitationConfig / CaptureRequest / MeasurementQuality /
//     DUAL_CHANNEL_QUALITY_FMAX_HZ 等属于已删除的第二套测量链，不再存在。
// ============================================================================

#pragma once

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// 编译期上限（仪器级约束）
// ---------------------------------------------------------------------------
static constexpr uint16_t SWEEP_MAX_POINTS = 257;    // 50 点/十倍频 × 3 十倍频 + 1
static constexpr double INSTRUMENT_F_MIN_HZ = 10.0;  // 产品频段：10 Hz
static constexpr double INSTRUMENT_F_MAX_HZ = 10000.0; // 产品频段：10 kHz
// DNT API 硬件本身可到 ~20.8 kHz（见 DO_NOT_TOUCH_EXAMPLE）；使用更高频段
// 属于独立测试项，不自动扩大产品频段声明。

// ---------------------------------------------------------------------------
// 测量类别（彻底移除 bool isOnePort）
// ---------------------------------------------------------------------------
enum class MeasurementKind : uint8_t {
    OnePortImpedance,   // Z = V/I（DNT 单口链，含校准）
    TwoPortTransfer,    // H = Vout/Vin（DNT W 链，raw / no calib）
};

// ---------------------------------------------------------------------------
// 扫频配置：只描述“测什么频段、多少点”，不描述任何硬件参数
// ---------------------------------------------------------------------------
struct SweepConfig {
    MeasurementKind kind = MeasurementKind::OnePortImpedance;
    double fStartHz = 10.0;
    double fStopHz = 10000.0;
    uint16_t pointsPerDecade = 10;   // 对数密度
    uint16_t maxPoints = SWEEP_MAX_POINTS;
};

enum class SweepStatus : uint8_t {
    Ok = 0, InvalidConfig, Busy, NotReady, Cancelled, Error,
};

enum class SweepState : uint8_t {
    Idle,
    Measuring,       // 2/3 点 chunk 串行推进中（chunk 之间也可响应取消）
    Stopping,        // 已提交/执行 StopTone（停激励），完成前不得 seal
    TransferReady,   // StopTone 完成 + dataset 已 seal（此时才允许开 BLE）
    Insufficient,    // 已测完但有效点不足（单口<4 / 双口<2），不进 BLE
    Error,
    Cancelled,       // 用户取消：当前 chunk 完成后停止，不封存
};

// 频率表生成（对数几何分布；首尾精确 fStart/fStop）。
// 返回写入个数；参数非法或缓冲太小返回 0。host 可测。
size_t buildFrequencyPlan(const SweepConfig& cfg, double* freqs, size_t cap);

// H 幅度/相位 -> 复数直角坐标（纯数学换算，非新测量）：
//   phaseRad = phaseDeg * pi / 180;  re = hMag*cos;  im = hMag*sin
void wMagPhaseToComplex(double hMag, double phaseDeg, double* reH, double* imH);

// ---------------------------------------------------------------------------
// 状态文本（UI/日志用）
// ---------------------------------------------------------------------------
const char* sweepStateText(SweepState s);
const char* sweepStatusText(SweepStatus s);
const char* measurementKindText(MeasurementKind k);
