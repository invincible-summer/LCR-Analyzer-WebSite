// ============================================================================
// test_dsp.cpp —— 三参数正弦拟合 + 通道 skew 补偿（plan.md §9.4）
// ============================================================================
#include "check.h"
#include "dsp_fit.h"

#include <math.h>

// 合成正弦并填充 buffer（相位单位 rad，时间基准 t0）
static void synth(int16_t* buf, int n, double amp, double phaseRad, double dc,
                  double f, double rate, double t0)
{
    for (int k = 0; k < n; ++k) {
        const double t = t0 + (double)k / rate;
        buf[k] = (int16_t)lround(dc + amp * sin(2.0 * M_PI * f * t + phaseRad));
    }
}

int main()
{
    // ---- 1. 基本拟合：幅度/相位/直流/残差 ----------------------------------
    {
        static int16_t x[2048];
        const double f = 1000.0, rate = 50000.0;
        synth(x, 2048, 800.0, 0.7, 1500.0, f, rate, 0.0);
        const SineFitResult r = sineFit3(x, 2048, f, rate);
        CHECK(r.ok);
        CHECK_NEAR(r.amp, 800.0, 1.0);
        CHECK_NEAR(r.phaseRad, 0.7, 0.002);
        CHECK_NEAR(r.dc, 1500.0, 0.5);
        CHECK(r.residRms < 1.0);          // 量化噪声 ~0.3 LSB
    }

    // ---- 2. 通道 skew：t0 补偿恢复真实相位差 --------------------------------
    // plan.md §9.4：interleaved skew 合成数据在补偿后恢复正确 phase；
    // 关闭补偿必须能观察到预期 2πfΔt 偏差（测试不形同虚设）。
    {
        static int16_t va[4096], vb[4096];
        const double f = 1000.0, rate = 41666.0;
        const double skew = 1.0 / (rate * 2.0);        // 交错：半 pattern 间隔
        const double truePhaseDiff = 0.0;              // 两路同相
        synth(va, 4096, 1000.0, 0.0, 1500.0, f, rate, 0.0);
        synth(vb, 4096, 900.0, truePhaseDiff, 1500.0, f, rate, skew);

        // 补偿开启：B 用真实 t0 → 相位差 = 0
        const SineFitResult fa = sineFitTimed(SampleSeries{va, 4096, 1.0 / rate, 0.0}, f);
        const SineFitResult fb = sineFitTimed(SampleSeries{vb, 4096, 1.0 / rate, skew}, f);
        CHECK(fa.ok && fb.ok);
        const double diffComp = fb.phaseRad - fa.phaseRad;
        CHECK_NEAR(diffComp, truePhaseDiff, 0.005);

        // 补偿关闭：B 用 t0=0 → 相位差 ≈ +ωΔt（skew 引起的 2πfΔt 偏差可见）
        const SineFitResult fbWrong =
            sineFitTimed(SampleSeries{vb, 4096, 1.0 / rate, 0.0}, f);
        const double diffWrong = fbWrong.phaseRad - fa.phaseRad;
        const double expected = 2.0 * M_PI * f * skew;
        CHECK_NEAR(diffWrong, expected, 0.005);
        CHECK(fabs(diffWrong - diffComp) > fabs(expected) * 0.5);  // 偏差确实可见
    }

    // ---- 3. 频率误差的相位漂移（actualHz 必须进拟合的理由）------------------
    {
        static int16_t x[2048];
        const double fTrue = 1000.0, rate = 50000.0;
        synth(x, 2048, 800.0, 0.7, 1500.0, fTrue, rate, 0.0);
        const SineFitResult rOk = sineFit3(x, 2048, fTrue, rate);
        const SineFitResult rBad = sineFit3(x, 2048, fTrue * 1.001, rate);
        CHECK(rOk.residRms < 1.0);
        CHECK(rBad.residRms > 10.0 * rOk.residRms);   // 0.1% 频率误差即可见
    }

    // ---- 4. 边界：n<8 / 非法频率 -------------------------------------------
    {
        static int16_t x[16];
        CHECK(!sineFit3(x, 4, 1000.0, 50000.0).ok);
        CHECK(!sineFit3(x, 16, 0.0, 50000.0).ok);
        CHECK(!sineFit3(x, 16, 1000.0, 0.0).ok);
        CHECK(!sineFit3(nullptr, 16, 1000.0, 50000.0).ok);
    }

    // ---- 5. wrapDeg180 ------------------------------------------------------
    CHECK_NEAR(wrapDeg180(190.0), -170.0, 1e-12);
    CHECK_NEAR(wrapDeg180(-190.0), 170.0, 1e-12);
    CHECK_NEAR(wrapDeg180(180.0), 180.0, 1e-12);
    CHECK_NEAR(wrapDeg180(-180.0), 180.0, 1e-12);
    CHECK_NEAR(wrapDeg180(720.5), 0.5, 1e-12);

    return testSummary("test_dsp");
}
