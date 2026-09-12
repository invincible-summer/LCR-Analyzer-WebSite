// ============================================================================
// component_meter.h —— 未知单元件 R/C/L/有源判型与结果聚合（host 可编译）
// ----------------------------------------------------------------------------
// 输入仍只来自 DNT API：AppZPoint + AppCalcResult。本层不采 ADC、不生成
// 激励、不重新拟合阻抗，只做 API 结果一致性、异常诊断与鲁棒聚合。
// ============================================================================

#pragma once

#include "lcr_api.h"

#include <stdint.h>

enum class ImpedanceNature : uint8_t {
    Invalid = 0,
    Resistive,
    Capacitive,
    Inductive,
    NegativeResistive,
};

// 与 DNT 的 1 degree 阻性窗口一致。只有相位接近 +/-180 degree 才返回
// NegativeResistive；其它负实部但明显带电抗的点仍按电抗符号显示 C/L。
ImpedanceNature classifyImpedanceNature(const AppZPoint& p,
                                        double resistiveTolDeg = 1.0);
const char* impedanceNatureText(ImpedanceNature n);

struct ComponentEstimate {
    enum class Type : uint8_t { Resistor, Capacitor, Inductor, Active, Unknown };
    Type type = Type::Unknown;

    double rOhm = 0.0;     // Resistor: median(rs)
    double cFarad = 0.0;   // Capacitor: median(cs)
    double lHenry = 0.0;   // Inductor: median(ls)
    double dcrOhm = 0.0;   // Inductor: median(rs), negative -> dcrWarn

    uint8_t nMeasured = 0;     // 真正得到有限 Z 的点；不要求 calc/type 正确
    uint8_t nCalcValid = 0;    // calc 成功且 rs 有限
    uint8_t nTypeMismatch = 0; // Z apiType 与 calc.type 不一致
    uint8_t nValid = 0;        // 判型使用的 Z+calc 一致有效点
    uint8_t nConsistent = 0;   // 与最终被动判型一致的点数
    uint8_t nR = 0, nC = 0, nL = 0;
    uint8_t nNegativeReal = 0; // 明确负实部证据点数

    // recognized: 与中位聚合值最接近的真实测点；其 fAct 是 UI 显示
    // “当前数值对应频率”的真源。unknown/active: 使用 detailIndex。
    int8_t representativeIndex = -1;
    double representativeFreqHz = 0.0;

    // UNKNOWN 详情点：优先原计划的中位数频率；只要该频点真正测得 Z，
    // 即使 calc/type 数据错误也保留。只有根本未测得才从低频向高频找首个测得点。
    int8_t detailIndex = -1;

    bool dcrWarn = false;
    const char* reason = "";
};

ComponentEstimate summarizeComponent(const AppZPoint* z, const AppCalcResult* calc,
                                     uint16_t n);
const char* componentTypeText(ComponentEstimate::Type t);
