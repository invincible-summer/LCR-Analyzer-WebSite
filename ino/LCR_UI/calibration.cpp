// ============================================================================
// calibration.cpp —— 前端复校准查表/插值实现（host 可编译，单测覆盖）
// ============================================================================

#include "calibration.h"

#include <math.h>

// log(f) 线性插值幅度与（已 unwrap 的）相位；带外拒绝
CalPathStatus pathLookup(const CalPathTable& t, double f, ComplexCorrection& out)
{
    if (!(f > 0.0)) return CalPathStatus::OutOfRange;
    if (t.n == 0) {
        out.gain = 1.0;
        out.phaseRad = 0.0;
        return CalPathStatus::Ok;              // 空表 = 恒等（factory-none）
    }
    if (t.n == 1 || !(t.f[0] > 0.0)) return CalPathStatus::MalformedTable;
    for (uint8_t i = 1; i < t.n; ++i)
        if (!(t.f[i] > t.f[i - 1])) return CalPathStatus::MalformedTable;

    if (f < t.f[0] || f > t.f[t.n - 1]) return CalPathStatus::OutOfRange;

    // 定位区间
    uint8_t i = 0;
    while (i + 1 < t.n && f > t.f[i + 1]) ++i;

    if (f == t.f[i]) {                          // 恰在节点
        out.gain = t.gain[i];
        out.phaseRad = t.phaseRad[i];
        return CalPathStatus::Ok;
    }
    const double lf0 = log(t.f[i]);
    const double lf1 = log(t.f[i + 1]);
    const double u = (log(f) - lf0) / (lf1 - lf0);
    out.gain = t.gain[i] + (t.gain[i + 1] - t.gain[i]) * u;
    out.phaseRad = t.phaseRad[i] + (t.phaseRad[i + 1] - t.phaseRad[i]) * u;
    return CalPathStatus::Ok;
}

// ---------------------------------------------------------------------------
bool CalibrationProfile::isIdentity() const
{
    return voltage.n == 0 && current.n == 0 && port2Input.n == 0 &&
           port2Output.n == 0;
}

CalPathStatus CalibrationProfile::voltagePath(double f, ComplexCorrection& c) const
{ return pathLookup(voltage, f, c); }

CalPathStatus CalibrationProfile::currentPath(double f, ComplexCorrection& c) const
{ return pathLookup(current, f, c); }

CalPathStatus CalibrationProfile::port2InputPath(double f, ComplexCorrection& c) const
{ return pathLookup(port2Input, f, c); }

CalPathStatus CalibrationProfile::port2OutputPath(double f, ComplexCorrection& c) const
{ return pathLookup(port2Output, f, c); }

// ---------------------------------------------------------------------------
// 出厂恒等 profile：无表 → 所有路径恒等；频段 [10, 10000] Hz 声明为
// "nominal"（未做前端校准时的保守默认，与硬件频率约束一致）
// ---------------------------------------------------------------------------
const CalibrationProfile& factoryCalibration()
{
    static const CalibrationProfile kFactory = []() {
        CalibrationProfile p{};
        p.schemaVersion = 1;
        // id 字符串拷贝（constexpr 环境无 strncpy 保证，手写循环）
        const char src[] = "factory-none";
        for (unsigned i = 0; i < sizeof(src); ++i) p.id[i] = src[i];
        p.createdUnix = 0;
        p.validFMinHz = 10.0;
        p.validFMaxHz = 10000.0;
        return p;
    }();
    return kFactory;
}
