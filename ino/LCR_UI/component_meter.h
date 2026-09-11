// ============================================================================
// component_meter.h —— 单元件 R/C/L 汇总判型（host 可编译）
// ----------------------------------------------------------------------------
// v4.1.0 重构后的职责（plan.md §6）：
//   * 输入是 DNT API 的逐点结果（AppZPoint = lcr_api_measure_z 原样，
//     AppCalcResult = lcr_api_calc(f_act, z_re, z_im, false) 原样）；
//   * 判型 = 各点 apiType 的一致性判据（DNT 已经在每个点给出 R/C/L），
//     本层不再做第二套三模型拟合，也不再使用旧链的 raw-ADC 质量权重
//     （residualRmsA/B、amplitudeA/B 属于已删除的自写 sine-fit 链，
//     DNT API 未暴露这些量，禁止填假值或用 0 代替）；
//   * 数值 = 只对 API calc 的有效结果做鲁棒中位数聚合：
//       R: median(rs)   C: median(cs)   L: median(ls)，DCR = median(rs)
//   * L 的 DCR < 0 时不做 max(0,dcr) 钳位 —— 项目物理模型要求 DCR>=0，
//     负值判为测量/校准/模型异常，置 dcrWarn 由 UI 显示 WARN。
// ============================================================================

#pragma once

#include "lcr_api.h"

#include <stdint.h>

struct ComponentEstimate {
    enum class Type : uint8_t { Resistor, Capacitor, Inductor, Unknown };
    Type type = Type::Unknown;

    double rOhm = 0.0;     // Resistor: median(rs)
    double cFarad = 0.0;   // Capacitor: median(cs)
    double lHenry = 0.0;   // Inductor: median(ls)
    double dcrOhm = 0.0;   // Inductor: median(rs)；负值 -> dcrWarn
    uint8_t nValid = 0;        // API 测量+换算都有效的点数
    uint8_t nConsistent = 0;   // 与最终判型一致的点数
    bool dcrWarn = false;      // DCR<0：测量/校准/模型异常（UI 显示 WARN）
    const char* reason = "";   // UNKNOWN 时的一句话原因
};

// pts/calc 一一对应（n 点）；只使用 apiStatus==0 且数值有限的点。
ComponentEstimate summarizeComponent(const AppZPoint* z, const AppCalcResult* calc,
                                     uint16_t n);

const char* componentTypeText(ComponentEstimate::Type t);
