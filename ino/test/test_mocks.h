// ============================================================================
// test_mocks.h —— MeasurementEngine 的 host 端 mock 驱动（ino/test 专用）
// ----------------------------------------------------------------------------
// MockCapture 按「已知 DUT」合成双通道 mV 波形：
//   * 单端口：V = A_v·sin(ωt)，I_sense = A_i·sin(ωt − ∠Z)（串联模型
//     Z = Rd + jωL + 1/(jωC)；C=0 表示无电容、L=0 表示无电感）；
//   * 双端口：一阶 RC 低通 H(f) = 1/(1 + jω/ωc)；
//   * 自动量程思想：A_i 目标 ~500 mV，A_v = A_i·|Z|/Rsense（钳位），
//     与真实前端「固定幅度激励 + 合适量程」的幅度关系一致；
//   * 交错 skew：t0A = 0，t0B = 1/patternRate（与 AdcCapture 相同模型）；
//   * 可注入：噪声、DMA overrun、指定频点失败、零幅度（SignalTooSmall）。
// ============================================================================
#pragma once

#include "measurement_engine.h"

#include <math.h>
#include <string.h>

class MockExcitation : public IExcitationSource {
public:
    ExcitationStatus excitationBegin(const ExcitationConfig& cfg) override
    {
        m_cfg = cfg;
        m_state.actualHz = m_freqError + cfg.requestedHz;
        m_state.actualVrms = 1.05;
        m_state.amplitudeCalibrated = false;
        m_stopped = false;
        return ExcitationStatus::Ok;
    }
    ExcitationState excitationState() const override { return m_state; }
    void excitationStop() override { m_stopped = true; m_state = ExcitationState{}; }
    bool stopped() const { return m_stopped; }

    double m_freqError = 0.0;     // 模拟 actualHz 与 requested 的偏差
    bool m_stopped = true;
    ExcitationState m_state{};
    ExcitationConfig m_cfg{};
};

class MockFrontEnd : public IFrontEnd {
public:
    void frontEndEnable(uint8_t r) override { enabled = true; range = r; }
    void frontEndDisable() override { enabled = false; }
    bool enabled = false;
    uint8_t range = 0;
};

class MockCapture : public ICaptureDevice {
public:
    // ---- DUT 配置（单端口串联模型；C<=0 无电容）---------------------------
    double dutR = 1000.0, dutL = 0.0, dutC = 0.0;    // Z = R + jωL + 1/(jωC)
    double fc = 1591.5;                              // 双端口 RC 低通拐点
    double rSense = 100.0;
    double noiseMv = 0.0;
    double failBelowHz = -1.0;        // 该频点以下强制 SignalTooSmall（幅度置 0）
    bool injectOverrun = false;

    // ---- ICaptureDevice -----------------------------------------------------
    CaptureStatus captureStart(const CaptureRequest& req) override
    {
        m_req = req;
        m_overrun = injectOverrun;
        m_done = false;
        m_cancelled = false;
        m_failThis = (req.excitationHz < failBelowHz);
        // 合成样本（模拟一次 DMA 完成后全部到达）
        const double f = req.excitationHz;
        const uint32_t rate = req.targetSampleRateHz;
        uint32_t n = (uint32_t)((double)rate * req.captureCycles / f) + 1;
        if (n > kCap) n = kCap;
        if (n < req.minSamplesPerChannel) n = req.minSamplesPerChannel;
        if (n > kCap) return CaptureStatus::InvalidRequest;
        const double patternRate = (double)rate * 2.0;
        const double skew = 1.0 / patternRate;

        // DUT 阻抗（单端口）
        const double w = 2.0 * M_PI * f;
        double zRe = dutR, zIm = w * dutL;
        if (dutC > 0.0) { zRe += 0.0; zIm += -1.0 / (w * dutC); }
        const double zMag = hypot(zRe, zIm);
        const double zPh = atan2(zIm, zRe);

        const bool onePort = (req.channels[0] == CaptureChannel::VoltageDut);
        double av = 1000.0, ai = 1000.0, phB = 0.0;
        if (onePort) {
            ai = 500.0;
            av = ai * zMag / rSense;                  // 自动量程幅度关系
            if (av > 1400.0) { av = 1400.0; ai = av * rSense / zMag; }
            phB = -zPh;                               // I 超前 V 为 −∠Z
        } else {
            const double x = f / fc;
            const double g = 1.0 / sqrt(1.0 + x * x);
            av = 1000.0;
            ai = 1000.0 * g;
            phB = -atan(x);                           // 输出滞后（H 相位）
        }
        if (m_failThis) { av = ai = 0.0; }

        uint32_t seed = 12345u;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u;
                           return (double)((int32_t)(seed >> 8) % 1000) / 1000.0 - 0.5; };

        for (uint32_t k = 0; k < n; ++k) {
            const double tA = (double)k / (double)rate;
            const double tB = (double)k / (double)rate + skew;
            // 直流偏置 1600 mV，幅度上限 1400 mV：避开 0/满量程削顶判据
            m_a[k] = (int16_t)lround(1600.0 + av * sin(2 * M_PI * f * tA) +
                                      rnd() * noiseMv);
            m_b[k] = (int16_t)lround(1600.0 + ai * sin(2 * M_PI * f * tB + phB) +
                                      rnd() * noiseMv);
        }
        mA.samples = m_a;
        mB.samples = m_b;
        mA.count = mB.count = n;
        mA.dt = mB.dt = 1.0 / (double)rate;
        mA.t0 = 0.0;
        mB.t0 = skew;                                  // 通道 skew（真实时间基准）
        m_done = true;
        return CaptureStatus::Ok;
    }
    void capturePoll() override {}
    bool captureDone() const override { return m_done; }
    CaptureStatus captureStatus() const override
    {
        if (m_overrun) return CaptureStatus::DmaOverrun;
        return CaptureStatus::Ok;
    }
    TimedSamples captureChannel(CaptureChannel ch) override
    {
        return (ch == m_req.channels[0]) ? mA : mB;
    }
    uint32_t captureMaxPatternRateHz() const override { return 83333; }
    uint16_t captureFullScaleMv() const override { return 3100; }
    void captureCancel() override { m_cancelled = true; }
    void captureRelease() override { m_released = true; m_done = false; }
    bool released() const { return m_released; }

    static constexpr uint32_t kCap = 8192;
    int16_t m_a[kCap];
    int16_t m_b[kCap];
    TimedSamples mA{}, mB{};
    CaptureRequest m_req{};
    bool m_overrun = false, m_done = false, m_cancelled = false;
    bool m_released = true;
    bool m_failThis = false;
};
