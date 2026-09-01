// ============================================================================
// test_dsp.cpp —— dsp_fit / analysis 的 PC 端单元测试（g++ 原生编译，无 Arduino 依赖）
// ----------------------------------------------------------------------------
// 运行方式：见 ino/tools/run_tests.sh（编译 dsp_fit.cpp + analysis.cpp 后执行）
// 覆盖：
//   1. 无噪声正弦的三参数拟合（幅度/相位/直流精确恢复）
//   2. 含噪正弦（σ=20 码）下的估计精度
//   3. wrapDeg180 的角度折算
//   4. computeRatioPhase 的幅度比/相位差方向约定（单端口=V/I、φI−φV）
//   5. classifyFilter：低通/高通/带通的一阶/二阶识别
//   6. guessEquivalentCircuit：并联 RC / 串联 RL 的模型判别
// ============================================================================

#include "../LCR_UI/analysis.h"
#include "../LCR_UI/dsp_fit.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int g_fail = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) { printf("  PASS  %s\n", msg); }                           \
        else      { printf("  FAIL  %s\n", msg); ++g_fail; }                 \
    } while (0)

static bool closeTo(double a, double b, double tol)
{
    return std::fabs(a - b) <= tol;
}

// 合成两路正弦（含直流与可选噪声）填入 AdcSampleResult
static AdcSampleResult synth(double f, double fs, int n, double aOut,
                             double phOut, double aIn, double phIn,
                             double noiseSigma, bool onePort)
{
    static std::vector<short> vo, vi;
    vo.resize(n);
    vi.resize(n);
    unsigned seed = 12345;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u;
                       return ((double)((int)(seed >> 8) % 1000) / 1000.0 - 0.5); };
    const double w = 2 * M_PI * f;
    for (int k = 0; k < n; ++k) {
        vo[k] = (short)std::lround(2048 + aOut * std::sin(w * k / fs + phOut)
                                   + rnd() * 2 * noiseSigma);
        vi[k] = (short)std::lround(2048 + aIn * std::sin(w * k / fs + phIn)
                                   + rnd() * 2 * noiseSigma);
    }
    AdcSampleResult s{};
    s.sampleLength = n;
    s.outBuffer = vo.data();
    s.inBuffer = vi.data();
    s.transimpedanceGain = onePort ? 1000 : 1;
    s.voltageGain = 2;
    s.currentGain = 2;
    s.actualFreq = f;
    s.samplingFreq = fs;
    return s;
}

int main()
{
    // ---- 1. 无噪声正弦拟合 -------------------------------------------------
    printf("[1] sineFit3 clean sine\n");
    {
        const int N = 1024;
        static short x[N];
        const double A = 1234.5, ph = 0.7, dc = 2048, f = 1000, fs = 64000;
        for (int k = 0; k < N; ++k)
            x[k] = (short)std::lround(dc + A * std::sin(2 * M_PI * f * k / fs + ph));
        const SineFitResult r = sineFit3(x, N, f, fs);
        // 输入被量化为整数（±0.5LSB），残差 ~ 1/sqrt(12)≈0.29LSB，
        // 幅度/相位误差按量化噪声的统计上界放宽
        CHECK(r.ok, "fit ok");
        CHECK(closeTo(r.amp, A, 0.05), "amplitude recovered");
        CHECK(closeTo(r.phaseRad, ph, 2e-4), "phase recovered");
        CHECK(closeTo(r.dc, dc, 0.05), "dc recovered");
        CHECK(r.residRms < 0.4, "residual ~ quantization noise");
    }

    // ---- 2. 含噪正弦 -------------------------------------------------------
    printf("[2] sineFit3 noisy sine (sigma=20 codes)\n");
    {
        const int N = 1024;
        static short x[N];
        const double A = 800, ph = -1.2, dc = 2048, f = 100, fs = 6400;
        unsigned seed = 7;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u;
                           return ((double)((int)(seed >> 8) % 1000) / 1000.0 - 0.5); };
        for (int k = 0; k < N; ++k)
            x[k] = (short)std::lround(dc + A * std::sin(2 * M_PI * f * k / fs + ph)
                                      + rnd() * 40);
        const SineFitResult r = sineFit3(x, N, f, fs);
        CHECK(r.ok, "fit ok");
        CHECK(std::fabs(r.amp - A) / A < 0.01, "amplitude err < 1%");
        CHECK(std::fabs(r.phaseRad - ph) < 0.02, "phase err < ~1 deg");
        CHECK(std::fabs(r.dc - dc) < 1.5, "dc err < 1.5 codes");
    }

    // ---- 3. 角度折算 -------------------------------------------------------
    printf("[3] wrapDeg180\n");
    {
        CHECK(closeTo(wrapDeg180(190), -170, 1e-9), "190 -> -170");
        CHECK(closeTo(wrapDeg180(-190), 170, 1e-9), "-190 -> 170");
        CHECK(closeTo(wrapDeg180(180), 180, 1e-9), "180 -> 180");
        CHECK(closeTo(wrapDeg180(0.5), 0.5, 1e-9), "0.5 -> 0.5");
    }

    // ---- 4. 幅度比 / 相位差约定 --------------------------------------------
    printf("[4] computeRatioPhase conventions\n");
    {
        // 单端口：V=1000∠0°，I=500∠+40°（电流超前 40°）
        //   期望 ratio = 1000/500 = 2.0，phaseDiff = φI−φV = +40°
        AdcSampleResult s = synth(1000, 64000, 1024, 1000, 0, 500,
                                  40 * M_PI / 180, 0, true);
        RatioPhaseResult r = computeRatioPhase(s);
        CHECK(r.ok, "fit ok");
        CHECK(closeTo(r.amplitudeRatio, 2.0, 0.01), "ratio = V/I = 2.0");
        CHECK(closeTo(r.phaseDiffDeg, 40.0, 0.5), "phaseDiff = +40 (current leads)");

        // 双端口：Vin=1500∠0°，Vout=750∠−60°（输出滞后）
        //   期望 ratio = Vin/Vout = 2.0 = 1/|H|，phaseDiff = φVout−φVin = −60°
        s = synth(1000, 64000, 1024, 1500, 0, 750, -60 * M_PI / 180, 0, false);
        r = computeRatioPhase(s);
        CHECK(r.ok, "fit ok");
        CHECK(closeTo(r.amplitudeRatio, 2.0, 0.01), "ratio = Vin/Vout = 2.0");
        CHECK(closeTo(r.phaseDiffDeg, -60.0, 0.5),
              "phaseDiff = -60 (output lags)");
    }

    // ---- 5. 滤波器类型识别 -------------------------------------------------
    printf("[5] classifyFilter\n");
    {
        std::vector<double> f, db;
        for (int i = 0; i <= 60; ++i)
            f.push_back(10.0 * std::pow(1000.0, i / 60.0));   // 10Hz..10kHz
        // 一阶低通 fc=1k：gain = -10 log10(1+(f/fc)^2)
        db.clear();
        for (double x : f)
            db.push_back(-10.0 * std::log10(1.0 + (x / 1000.0) * (x / 1000.0)));
        FilterGuess g = classifyFilter(f.data(), db.data(), (int)f.size());
        CHECK(g.ok && std::string(g.type) == "LOW-PASS", "1st-order LP -> LOW-PASS");
        CHECK(g.order == 1, "order ~ 1");

        // 二阶低通：-40dB/dec 渐近
        db.clear();
        for (double x : f) {
            const double u = x / 1000.0;
            db.push_back(u < 1 ? 0.0 : -40.0 * std::log10(u));
        }
        g = classifyFilter(f.data(), db.data(), (int)f.size());
        CHECK(g.ok && std::string(g.type) == "LOW-PASS", "2nd-order LP -> LOW-PASS");
        CHECK(g.order == 2, "order ~ 2");

        // 一阶高通 fc=1k
        db.clear();
        for (double x : f)
            db.push_back(-10.0 * std::log10(1.0 + (1000.0 / x) * (1000.0 / x)));
        g = classifyFilter(f.data(), db.data(), (int)f.size());
        CHECK(g.ok && std::string(g.type) == "HIGH-PASS", "1st-order HP -> HIGH-PASS");

        // 带通（LC 谐振式峰值，Q~5，中心 1kHz）
        db.clear();
        for (double x : f) {
            const double u = x / 1000.0;
            db.push_back(-10.0 * std::log10(1.0 + 25.0 * (u - 1.0 / u) * (u - 1.0 / u)));
        }
        g = classifyFilter(f.data(), db.data(), (int)f.size());
        CHECK(g.ok && std::string(g.type) == "BAND-PASS", "resonator -> BAND-PASS");
    }

    // ---- 6. 等效电路估计 ---------------------------------------------------
    printf("[6] guessEquivalentCircuit\n");
    {
        std::vector<double> f, re, im;
        for (int i = 0; i <= 40; ++i)
            f.push_back(10.0 * std::pow(1000.0, i / 40.0));
        // 并联 RC：Z = 1/(1/R + jwC)，R=1k，C=1µF
        re.clear(); im.clear();
        for (double x : f) {
            const double w = 2 * M_PI * x;
            const double den = 1.0 / 1000.0 * (1.0 / 1000.0) + (w * 1e-6) * (w * 1e-6) + 1e-9;
            re.push_back((1.0 / 1000.0) / den);
            im.push_back(-(w * 1e-6) / den);
        }
        EqCircuitGuess g = guessEquivalentCircuit(f.data(), re.data(), im.data(),
                                                  (int)f.size());
        CHECK(g.ok && std::string(g.model) == "R||C", "parallel RC detected");
        CHECK(std::fabs(g.C - 1e-6) / 1e-6 < 0.05, "C within 5%");

        // 串联 RL：Z = R + jwL，R=100，L=10mH
        re.clear(); im.clear();
        for (double x : f) {
            const double w = 2 * M_PI * x;
            re.push_back(100.0);
            im.push_back(w * 0.01);
        }
        g = guessEquivalentCircuit(f.data(), re.data(), im.data(), (int)f.size());
        CHECK(g.ok && std::string(g.model) == "R+L(s)", "series RL detected");
        CHECK(std::fabs(g.L - 0.01) / 0.01 < 0.05, "L within 5%");
    }

    printf(g_fail == 0 ? "\nALL TESTS PASSED\n" : "\n%d TEST(S) FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
