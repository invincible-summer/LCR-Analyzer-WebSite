// ============================================================================
// test_component.cpp —— 单元件三模型分类器（plan.md §5.1/§9.5 的软件侧）
// ============================================================================
#include "check.h"
#include "component_meter.h"
#include "measurement_types.h"

#include <initializer_list>
#include <math.h>

// 由标称 R/C/L 生成 n 个频点的 OnePortPoint（加可控相对噪声）
static uint16_t synthPoints(double R, double L, double C, const double* freqs,
                            int n, double noiseRel, OnePortPoint* out)
{
    uint32_t seed = 7u;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u;
                       return (double)((int32_t)(seed >> 9) % 2000) / 1000.0 - 1.0; };
    for (int k = 0; k < n; ++k) {
        const double w = 2.0 * M_PI * freqs[k];
        double zRe = R, zIm = w * L;
        if (C > 0.0) zIm -= 1.0 / (w * C);
        zRe *= 1.0 + rnd() * noiseRel;
        zIm *= 1.0 + rnd() * noiseRel;
        OnePortPoint& p = out[k];
        p.requestedHz = p.actualHz = freqs[k];
        p.reOhm = zRe;
        p.imOhm = zIm;
        p.magOhm = hypot(zRe, zIm);
        p.phaseDeg = atan2(zIm, zRe) * 180.0 / M_PI;
        p.quality.status = MeasurementStatus::Ok;
        p.quality.amplitudeA = p.quality.amplitudeB = 800.0;
        p.quality.residualRmsA = p.quality.residualRmsB = 4.0;
        p.quality.clippedChA = p.quality.clippedChB = false;
    }
    return (uint16_t)n;
}

int main()
{
    const double f5[5] = {100.0, 223.6, 500.0, 1118.0, 2000.0};

    // ---- 1. 纯 R -------------------------------------------------------------
    {
        OnePortPoint pts[5];
        synthPoints(1000.0, 0.0, 0.0, f5, 5, 0.002, pts);
        const ComponentEstimate e = classifyComponent(pts, 5);
        CHECK(e.type == ComponentEstimate::Type::Resistor);
        CHECK_NEAR(e.rOhm, 1000.0, 15.0);
        CHECK(e.nValid == 5);
    }
    {
        OnePortPoint pts[5];
        synthPoints(100.0, 0.0, 0.0, f5, 5, 0.002, pts);
        CHECK(classifyComponent(pts, 5).type == ComponentEstimate::Type::Resistor);
    }
    {
        OnePortPoint pts[5];
        synthPoints(10000.0, 0.0, 0.0, f5, 5, 0.002, pts);
        const ComponentEstimate e = classifyComponent(pts, 5);
        CHECK(e.type == ComponentEstimate::Type::Resistor);
        CHECK_NEAR(e.rOhm, 10000.0, 150.0);
    }

    // ---- 2. 纯 C（1n/10n/100n）-------------------------------------------------
    for (double c : {1e-9, 10e-9, 100e-9}) {
        OnePortPoint pts[5];
        synthPoints(1.0, 0.0, c, f5, 5, 0.003, pts);
        const ComponentEstimate e = classifyComponent(pts, 5);
        CHECK(e.type == ComponentEstimate::Type::Capacitor);
        CHECK(fabs(e.cFarad - c) / c < 0.02);
    }

    // ---- 3. L+DCR（100µ/1m/10m + DCR）------------------------------------------
    for (double l : {100e-6, 1e-3, 10e-3}) {
        OnePortPoint pts[5];
        synthPoints(5.0, l, 0.0, f5, 5, 0.003, pts);
        const ComponentEstimate e = classifyComponent(pts, 5);
        CHECK(e.type == ComponentEstimate::Type::Inductor);
        CHECK(fabs(e.lHenry - l) / l < 0.03);
        CHECK(e.dcrOhm >= 0.0);
        CHECK_NEAR(e.dcrOhm, 5.0, 1.0);
    }

    // ---- 4. N_valid < 3 → UNKNOWN（宁缺毋滥）-----------------------------------
    {
        OnePortPoint pts[5];
        synthPoints(1000.0, 0.0, 0.0, f5, 2, 0.0, pts);   // 只有 2 个有效点
        const ComponentEstimate e = classifyComponent(pts, 5);
        CHECK(e.type == ComponentEstimate::Type::Unknown);
    }

    // ---- 5. 质量红旗点被剔除 ----------------------------------------------------
    {
        OnePortPoint pts[5];
        synthPoints(1000.0, 0.0, 0.0, f5, 5, 0.002, pts);
        pts[0].quality.status = MeasurementStatus::AdcOverrun;
        pts[1].quality.clippedChA = true;
        const ComponentEstimate e = classifyComponent(pts, 5);
        CHECK(e.nValid == 3);
        CHECK(e.type == ComponentEstimate::Type::Resistor);
    }

    // ---- 6. 超量程 → OUT OF RANGE（参数在声明范围外）----------------------------
    {
        OnePortPoint pts[5];
        synthPoints(50e6, 0.0, 0.0, f5, 5, 0.0, pts);     // R = 50MΩ > 1e7
        const ComponentEstimate e = classifyComponent(pts, 5);
        CHECK(e.type == ComponentEstimate::Type::OutOfRange ||
              e.type == ComponentEstimate::Type::Unknown);
    }

    // ---- 7. 残差过大 → UNKNOWN（不属于任何 primitive 模型）----------------------
    {
        OnePortPoint pts[5];
        // 串联 RLC（谐振在带内）：三个 primitive 模型都不该赢
        for (int k = 0; k < 5; ++k) {
            const double w = 2.0 * M_PI * f5[k];
            const double zRe = 100.0;
            const double zIm = w * 1e-3 - 1.0 / (w * 1e-6);   // 强容性→感性扫描
            OnePortPoint& p = pts[k];
            p.requestedHz = p.actualHz = f5[k];
            p.reOhm = zRe; p.imOhm = zIm;
            p.quality.status = MeasurementStatus::Ok;
            p.quality.amplitudeA = p.quality.amplitudeB = 800.0;
            p.quality.residualRmsA = p.quality.residualRmsB = 4.0;
        }
        const ComponentEstimate e = classifyComponent(pts, 5);
        CHECK(e.type != ComponentEstimate::Type::Resistor);   // 不能强行判 R
        // 容性占优（谐振 >2kHz）时 C 模型残差也大 → UNKNOWN；或判 C 也须残差达标
        if (e.type == ComponentEstimate::Type::Capacitor)
            CHECK(e.bestWrmse > defaultComponentGates().maxRelWrmse);
    }

    return testSummary("test_component");
}
