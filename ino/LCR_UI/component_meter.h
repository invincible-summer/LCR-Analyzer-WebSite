// ============================================================================
// component_meter.h —— 单元件 R/C/L 自动识别与测量（host 可编译）
// ----------------------------------------------------------------------------
// 目标（plan.md §5.1）：不是单相位阈值判断，而是在三个 primitive 模型中
// 做多频点加权拟合比较；不确定时明确输出 UNKNOWN / OUT OF RANGE，
// 绝不为了给结果而强行判 R/C/L。
//
// 候选模型：
//   R:     Z_k = R
//   C:     Z_k = −j/(ω_k·C)
//   L+DCR: Z_k = Rd + j·ω_k·L     （物理电感必须同时报告 L 与 DCR）
//
// 拟合（线性闭式，避免在 ESP32 上引入重型非线性优化器）：
//   R  = weightedMean(Re(Z_k))
//   q = 1/C 由加权 LS 求 Im(Z_k) = −q/ω_k 的正解
//   Rd = weightedMean(Re(Z_k))，L = Σw²ω·Im(Z) / Σw²ω²，强制 Rd≥0、L>0
// 每个模型用完整复残差重算统一 WRMSE（不只比较虚部符号）。
//
// 分类条件（全部满足才给 R/C/L）：
//   N_valid ≥ 3；最优模型相对残差 ≤ 阈值；与第二名有足够 gap；参数在
//   声明可测范围内；无 clipping / signal-too-small / calibration invalid。
// ============================================================================

#pragma once

#include "measurement_types.h"

#include <stdint.h>

struct ComponentEstimate {
    enum class Type : uint8_t { Resistor, Capacitor, Inductor, Unknown, OutOfRange };
    Type type = Type::Unknown;

    double rOhm = 0.0;     // Resistor
    double cFarad = 0.0;   // Capacitor
    double lHenry = 0.0;   // Inductor
    double dcrOhm = 0.0;   // Inductor 串联电阻
    double q = 0.0;        // 拟合质量：1/C（诊断）

    double bestWrmse = 0.0;    // 最优模型统一相对 WRMSE
    double runnerUpWrmse = 0.0;
    double relGap = 0.0;       // (second−best)/second
    uint16_t nValid = 0;
    // 各模型相对 WRMSE（诊断显示用）
    double wrmseR = 0.0, wrmseC = 0.0, wrmseL = 0.0;
    // 拒绝原因（UNKNOWN/OUT OF RANGE 时给 UI 一句话）
    const char* reason = "";
};

// 分类门限（工程默认；实板标准件标定后冻结，见 plan.md §9.5：
// 达不到时不悄悄放宽代码阈值，而是记录 uncertainty/range 后由硬件指标决定）
struct ComponentGates {
    double maxRelWrmse;   // 最优模型相对残差上限
    double minRelGap;     // 第一名相对第二名的最小 gap
    double rMinOhm, rMaxOhm;
    double cMinF, cMaxF;
    double lMinH, lMaxH;
};

const ComponentGates& defaultComponentGates();

// pts：同一 MeasurementEngine（OnePortImpedance）在多个频点得到的结果；
//      只使用 status==Ok 且数值有限的点。
ComponentEstimate classifyComponent(const OnePortPoint* pts, uint16_t n);

const char* componentTypeText(ComponentEstimate::Type t);
