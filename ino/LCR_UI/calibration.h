// ============================================================================
// calibration.h —— 仪器校准的两层模型之上层：前端复增益/相位校准（host 可编译）
// ----------------------------------------------------------------------------
// 分层（plan.md §4，两层绝不能混淆）：
//   1. MCU ADC calibration：Espressif adc_cali_*（curve fitting），在
//      AdcCapture 内把 raw code → mV。只修正 MCU ADC 传递函数。
//   2. 仪器前端复校准（本文件）：补偿电压路径、电流感测路径、Port2
//      输入/输出路径随频率的增益与相位误差。
//
// 应用公式：
//   单端口：V = Cv(f)·V_raw；I = Ci(f)·I_raw / Rsense(或跨阻折算)；Z = V/I
//   双端口：Vin = Cin(f)·Vin_raw；Vout = Cout(f)·Vout_raw；H = Vout/Vin
//
// 本层不是 OSL/VNA 三项误差模型——理论文档已指出现有 OSL 只是 scaffolding；
// 只有硬件最终采用反射系数架构并真正实现三项误差模型时才使用该表述。
//
// 频率间插值：对 log(f) 线性插值 magnitude 与 unwrap 后的 phase；
// 超出已校准频段返回OutOfRange，不无提示外推。
// ============================================================================

#pragma once

#include <stdint.h>

struct ComplexCorrection {
    double gain;       // multiplicative magnitude
    double phaseRad;   // additive phase
};

enum class CalPathStatus : uint8_t {
    Ok = 0,
    OutOfRange,        // 频点在已校准频段之外（含表为空的情形）
    MalformedTable,    // 频率非升序 / 单点表非法
};

// 单条路径的校准表（按频率升序；gain>0）
struct CalPathTable {
    static constexpr uint8_t MAX_KNOTS = 16;
    uint8_t n = 0;
    double f[MAX_KNOTS];        // Hz，升序
    double gain[MAX_KNOTS];     // 幅度修正因子
    double phaseRad[MAX_KNOTS]; // 相位修正（弧度，存储时已 unwrap）
};

CalPathStatus pathLookup(const CalPathTable& t, double f, ComplexCorrection& out);

// ---------------------------------------------------------------------------
// CalibrationProfile：一次仪器校准会话的产物
// ---------------------------------------------------------------------------
struct CalibrationProfile {
    static constexpr uint8_t ID_LEN = 32;

    uint32_t schemaVersion;
    char id[ID_LEN];         // "cal-..." 或 "factory-none"
    uint32_t createdUnix;    // 0 = 无时间戳（factory）
    double validFMinHz;      // 已校准频段（表首/末频率；空表 = 全恒等）
    double validFMaxHz;

    CalPathTable voltage;      // 单端口 V 路径
    CalPathTable current;      // 单端口 I 路径（不含 Rsense 折算，那是常数）
    CalPathTable port2Input;   // 双端口 Vin 路径
    CalPathTable port2Output;  // 双端口 Vout 路径

    bool isIdentity() const;               // 无任何有效表（factory-none）
    CalPathStatus voltagePath(double f, ComplexCorrection& c) const;
    CalPathStatus currentPath(double f, ComplexCorrection& c) const;
    CalPathStatus port2InputPath(double f, ComplexCorrection& c) const;
    CalPathStatus port2OutputPath(double f, ComplexCorrection& c) const;
};

// 出厂恒等 profile（无频段限制；任何频点 Ok、修正 = 1∠0）
const CalibrationProfile& factoryCalibration();
