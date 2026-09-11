// ============================================================================
// test_misc.cpp —— sine_plan（精确有理频率）+ calibration（复校准查表）
// ============================================================================
#include "check.h"
#include "calibration.h"
#include "sine_plan.h"

#include <cstring>
#include <math.h>

int main()
{
    // ---- 1. sine_plan：精确性与约束 ------------------------------------------
    {
        const double freqs[] = {10, 50, 100, 123, 1000, 1234, 2000, 5000, 9999};
        for (double f : freqs) {
            const SinePlan p = planSineExact(f);
            CHECK(p.ok);
            // 误差上界：0.5%（远低于引擎 1% 频率门限）
            CHECK(fabs(p.actualHz - f) / f <= 0.005);
            // 有理重建：actualHz == Fs·K/L（浮点重算一致）
            CHECK_NEAR(p.actualHz,
                       (double)p.sampleRateHz * p.cyclesK / p.tableLenL, 1e-6);
            // DAC 重建质量：每周期样本数 ≥ 8
            CHECK((double)p.tableLenL / p.cyclesK >= 8.0);
            CHECK(p.tableLenL <= 4096);
            // Fs 必须属于精确分频档（128·Fs 整除 160 MHz）
            bool inSet = false;
            for (size_t i = 0; i < exactFsSetSize(); ++i)
                if (exactFsAt(i) == p.sampleRateHz) { inSet = true; break; }
            CHECK(inSet);
            CHECK(fmod(160000000.0 / (128.0 * p.sampleRateHz), 1.0) == 0.0);
        }
        // 非法输入
        CHECK(!planSineExact(0.0).ok);
        CHECK(!planSineExact(-5.0).ok);
    }

    // ---- 2. calibration：恒等/插值/带外/畸形表 ---------------------------------
    {
        const CalibrationProfile& fac = factoryCalibration();
        ComplexCorrection c;
        CHECK(fac.isIdentity());
        CHECK(fac.voltagePath(1234.0, c) == CalPathStatus::Ok);
        CHECK_NEAR(c.gain, 1.0, 1e-12);
        CHECK_NEAR(c.phaseRad, 0.0, 1e-12);
        CHECK(strcmp(fac.id, "factory-none") == 0);

        // 两节点表：log-f 中点插值
        CalPathTable t{};
        t.n = 2;
        t.f[0] = 100; t.gain[0] = 1.0; t.phaseRad[0] = 0.0;
        t.f[1] = 10000; t.gain[1] = 2.0; t.phaseRad[1] = 0.1;
        CHECK(pathLookup(t, 100.0, c) == CalPathStatus::Ok);
        CHECK_NEAR(c.gain, 1.0, 1e-12);
        CHECK(pathLookup(t, 10000.0, c) == CalPathStatus::Ok);
        CHECK_NEAR(c.gain, 2.0, 1e-12);
        CHECK(pathLookup(t, 1000.0, c) == CalPathStatus::Ok);    // log 中点
        CHECK_NEAR(c.gain, 1.5, 1e-9);
        CHECK_NEAR(c.phaseRad, 0.05, 1e-9);
        // 带外拒绝（不无提示外推）
        CHECK(pathLookup(t, 99.0, c) == CalPathStatus::OutOfRange);
        CHECK(pathLookup(t, 10001.0, c) == CalPathStatus::OutOfRange);
        CHECK(pathLookup(t, 0.0, c) == CalPathStatus::OutOfRange);

        // 频率非升序 → MalformedTable
        CalPathTable bad{};
        bad.n = 2;
        bad.f[0] = 1000; bad.f[1] = 100;
        CHECK(pathLookup(bad, 500.0, c) == CalPathStatus::MalformedTable);
        // 单点表 → MalformedTable（无法插值）
        CalPathTable one{};
        one.n = 1; one.f[0] = 1000;
        CHECK(pathLookup(one, 1000.0, c) == CalPathStatus::MalformedTable);
    }

    return testSummary("test_misc");
}
