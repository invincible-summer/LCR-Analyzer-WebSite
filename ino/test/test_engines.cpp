// ============================================================================
// test_engines.cpp —— MeasurementEngine / SweepEngine / radio_lock 状态机
// ----------------------------------------------------------------------------
// 覆盖（plan.md §3.2/§5.2/§6.1/§9.2/§9.4）：
//   * 单端口 Z=V/I 符号与数值（R/C/L+DCR）
//   * 双端口 H=Vout/Vin（RC 低通：低频 |H|≈1、拐点后下降、phase 为负）
//   * cancel 的下一次 poll 即 safe-off（激励停/ADC 释放/前端失能）
//   * DMA overrun → AdcOverrun；幅度过小 → SignalTooSmall
//   * SweepEngine：完整扫频 → seal → CSV/CRC；失败点不进 CSV；取消不封存
//   * radio_lock 互斥 invariant（测量窗口内射频必须 Off）
// 注意：本文件以 -DLCR_DEBUG_INVARIANTS 编译，覆盖断言路径。
// ============================================================================
#define LCR_DEBUG_INVARIANTS 1

#include "check.h"
#include "test_mocks.h"

#include "calibration.h"
#include "measurement_engine.h"
#include "radio_lock.h"
#include "sweep_engine.h"

static MockExcitation s_exc;
static MockCapture s_cap;
static MockFrontEnd s_front;

static MeasurementEngine makeEngine()
{
    return MeasurementEngine(s_exc, s_cap, &s_front, factoryCalibration(),
                             s_cap.rSense, 1.0);
}

// 把引擎从 start 推到空闲（每 poll 1ms），返回 poll 次数
static int runToIdle(MeasurementEngine& e, uint64_t& t)
{
    int n = 0;
    while (e.active() || e.state() != MeasurementEngineState::Idle) {
        e.poll(t);
        t += 1000;
        if (++n > 200000) break;
    }
    return n;
}

static MeasurementRequest onePortReq(double f)
{
    MeasurementRequest r{};
    r.kind = MeasurementKind::OnePortImpedance;
    r.requestedHz = f;
    r.settleCycles = 4;
    r.captureCycles = 8;
    r.minSamplesPerChannel = 200;
    return r;
}

int main()
{
    radioLockReset();

    // ---- 1. 纯 R：Re≈R，Im≈0（§9.4）---------------------------------------
    {
        MeasurementEngine e = makeEngine();
        s_cap.dutR = 1000.0; s_cap.dutL = 0.0; s_cap.dutC = 0.0;
        s_cap.noiseMv = 0.4;
        uint64_t t = 0;
        CHECK(e.start(onePortReq(1000.0)) == MeasurementStatus::Ok);
        runToIdle(e, t);
        OnePortPoint p;
        CHECK(e.takeResult(p));
        CHECK(p.quality.status == MeasurementStatus::Ok);
        CHECK_NEAR(p.reOhm, 1000.0, 15.0);          // ≤1.5%
        CHECK(fabs(p.imOhm) < 15.0);                // Im(Z)≈0
        CHECK(p.imOhm < 5.0 || p.imOhm > -5.0);
        CHECK(p.actualHz > 0);
        CHECK(!s_front.enabled);
        CHECK(s_exc.stopped());
        CHECK(s_cap.released());
    }

    // ---- 2. 纯 C：Im(Z)<0 且 1/(ω|Im|) 恢复 C（§9.4）------------------------
    {
        MeasurementEngine e = makeEngine();
        s_cap.dutR = 5.0; s_cap.dutC = 100e-9; s_cap.dutL = 0.0;
        uint64_t t = 0;
        CHECK(e.start(onePortReq(1000.0)) == MeasurementStatus::Ok);
        runToIdle(e, t);
        OnePortPoint p;
        CHECK(e.takeResult(p));
        CHECK(p.quality.status == MeasurementStatus::Ok);
        CHECK(p.imOhm < 0);                          // 容性
        const double cRec = -1.0 / (2 * M_PI * p.actualHz * p.imOhm);
        CHECK_NEAR(cRec, 100e-9, 4e-9);              // ≤4%
    }

    // ---- 3. L+DCR：Re≈DCR>=0，Im>0 且随 f 近似线性（§9.4）-------------------
    {
        MeasurementEngine e = makeEngine();
        s_cap.dutR = 10.0; s_cap.dutL = 1e-3; s_cap.dutC = 0.0;
        OnePortPoint p1, p2;
        uint64_t t = 0;
        CHECK(e.start(onePortReq(500.0)) == MeasurementStatus::Ok);
        runToIdle(e, t);
        CHECK(e.takeResult(p1));
        CHECK(e.start(onePortReq(2000.0)) == MeasurementStatus::Ok);
        runToIdle(e, t);
        CHECK(e.takeResult(p2));
        CHECK(p1.quality.status == MeasurementStatus::Ok);
        CHECK(p2.quality.status == MeasurementStatus::Ok);
        CHECK(p1.imOhm > 0 && p2.imOhm > 0);         // 感性
        CHECK(p2.imOhm > 3.5 * p1.imOhm);            // 随 f 近似线性（4×频）
        CHECK_NEAR(p1.reOhm, 10.0, 2.0);
        const double lRec1 = p1.imOhm / (2 * M_PI * p1.actualHz);
        const double lRec2 = p2.imOhm / (2 * M_PI * p2.actualHz);
        CHECK_NEAR(lRec1, 1e-3, 6e-5);
        CHECK_NEAR(lRec2, 1e-3, 6e-5);
    }

    // ---- 4. 双端口 RC 低通（§9.4：H=Vout/Vin）-------------------------------
    {
        MeasurementEngine e = makeEngine();
        s_cap.fc = 1591.5;
        MeasurementRequest r{};
        r.kind = MeasurementKind::TwoPortTransfer;
        r.requestedHz = 159.15;                       // fc/10
        r.settleCycles = 4; r.captureCycles = 8; r.minSamplesPerChannel = 200;
        uint64_t t = 0;
        CHECK(e.start(r) == MeasurementStatus::Ok);
        runToIdle(e, t);
        TwoPortPoint lo;
        CHECK(e.takeResult(lo));
        CHECK(lo.quality.status == MeasurementStatus::Ok);
        CHECK_NEAR(lo.reH, 0.995, 0.01);              // 低频 |H|≈1
        CHECK_NEAR(lo.imH, -0.0990, 0.005);           // H=Vout/Vin 虚部（1/(1+j0.1)）
        CHECK(fabs(lo.gainDb) < 0.1);

        r.requestedHz = 15915.0 > INSTRUMENT_F_MAX_HZ ? 10000.0 : 15915.0;
        // 用 10×fc（裁剪到仪器频段内也行：10k 处 |H| 仍明显下降）
        r.requestedHz = 10000.0;                      // 6.3×fc
        CHECK(e.start(r) == MeasurementStatus::Ok);
        runToIdle(e, t);
        TwoPortPoint hi;
        CHECK(e.takeResult(hi));
        CHECK(hi.quality.status == MeasurementStatus::Ok);
        CHECK(hi.gainDb < lo.gainDb - 10.0);          // 拐点后增益下降
        CHECK(hi.phaseDeg < -70.0 && hi.phaseDeg > -100.0);  // phase 为负
    }

    // ---- 5. cancel：下一次 poll 即停激励/释放 ADC（§9.2 <100ms 语义）---------
    {
        MeasurementEngine e = makeEngine();
        s_cap.dutR = 1000.0; s_cap.dutC = 0.0; s_cap.dutL = 0.0;
        uint64_t t = 0;
        CHECK(e.start(onePortReq(50.0)) == MeasurementStatus::Ok);
        e.poll(t); t += 1000;         // Prepare -> ExcitationStart
        e.poll(t); t += 1000;         // -> Settling（50Hz×4 周期 = 80ms）
        CHECK(e.state() == MeasurementEngineState::Settling);
        CHECK(!s_exc.stopped());                        // 激励在跑
        s_cap.captureRelease();                       // 复位 mock 状态记录
        e.cancel();
        e.poll(t);                                    // 下一次 poll：safe-off
        CHECK(s_exc.stopped());                         // 激励已停
        CHECK(s_cap.released());                        // ADC 已释放
        CHECK(!s_front.enabled);                      // 前端已失能
        t += 1000;
        e.poll(t);
        CHECK(e.state() == MeasurementEngineState::Idle);
        CHECK(e.lastStatus() == MeasurementStatus::Cancelled);
    }

    // ---- 6. DMA overrun → AdcOverrun，不产生 Z 点（§9.2）---------------------
    {
        MeasurementEngine e = makeEngine();
        s_cap.injectOverrun = true;
        uint64_t t = 0;
        CHECK(e.start(onePortReq(1000.0)) == MeasurementStatus::Ok);
        runToIdle(e, t);
        OnePortPoint p;
        CHECK(!e.takeResult(p));                      // 无结果
        CHECK(e.lastStatus() == MeasurementStatus::AdcOverrun);
        CHECK(s_exc.stopped() && s_cap.released());       // 错误路径落安全态
        s_cap.injectOverrun = false;
    }

    // ---- 7. 信号过小 → SignalTooSmall ----------------------------------------
    {
        MeasurementEngine e = makeEngine();
        s_cap.failBelowHz = 1e9;                      // 所有频点幅度置 0
        uint64_t t = 0;
        CHECK(e.start(onePortReq(1000.0)) == MeasurementStatus::Ok);
        runToIdle(e, t);
        OnePortPoint p;
        CHECK(!e.takeResult(p));
        CHECK(e.lastStatus() == MeasurementStatus::SignalTooSmall);
        s_cap.failBelowHz = -1.0;
    }

    // ---- 8. 频率失锁 → FrequencyMismatch -------------------------------------
    {
        MeasurementEngine e = makeEngine();
        s_exc.m_freqError = 50.0;                     // +5% @1kHz > 1% 门限
        uint64_t t = 0;
        CHECK(e.start(onePortReq(1000.0)) == MeasurementStatus::Ok);
        runToIdle(e, t);
        CHECK(e.lastStatus() == MeasurementStatus::FrequencyMismatch);
        s_exc.m_freqError = 0.0;
    }

    // ---- 9. radio_lock：测量窗口内射频必须 Off（§9.3 invariant）--------------
    {
        CHECK(radioLockInvariantOk());
        radioLockNotifyMeasurementActive(true);
        CHECK(radioLockInvariantOk());                // 射频 Off：合法
        radioLockNotifyRadioActive(true);
        CHECK(!radioLockInvariantOk());               // 双活动：invariant 破坏
        radioLockNotifyRadioActive(false);
        radioLockNotifyMeasurementActive(false);
        CHECK(radioLockInvariantOk());
    }

    // ---- 10. SweepEngine：完整扫频 → seal → CSV/CRC（§5.2/§9.6）--------------
    {
        MeasurementEngine e = makeEngine();
        SweepEngine sw(e);
        s_cap.dutR = 1000.0; s_cap.dutC = 0.0; s_cap.dutL = 0.0;
        s_cap.noiseMv = 0.4;
        SweepConfig cfg{};
        cfg.kind = MeasurementKind::OnePortImpedance;
        cfg.fStartHz = 100.0;
        cfg.fStopHz = 2000.0;
        cfg.pointsPerDecade = 5;
        cfg.maxPoints = SWEEP_MAX_POINTS;
        cfg.logSpacing = true;
        CHECK(sw.start(cfg) == SweepStatus::Ok);
        CHECK(sw.totalPoints() == (size_t)(1 + lround(5 * log10(20.0))));

        uint64_t t = 0;
        int guard = 0;
        while (sw.state() != SweepState::TransferReady &&
               sw.state() != SweepState::Error && sw.state() != SweepState::Cancelled) {
            sw.poll(t);
            t += 1000;
            if (++guard > 2000000) break;
        }
        CHECK(sw.state() == SweepState::TransferReady);
        const OnePortDataset* d = sw.sealedOnePortDataset();
        CHECK(d != nullptr);
        CHECK(d->sealed);
        CHECK(d->nPoints == (uint16_t)sw.totalPoints());
        CHECK(d->csvLen > 0);
        CHECK(d->crc32 == crc32Of((const uint8_t*)d->csv, d->csvLen));
        // CSV 内容：见 test_csv 的格式细节；这里验证点数一致
        int rows = 0;
        for (const char* s = d->csv; *s; )
            if (*s++ == '\n') ++rows;
        CHECK(rows == (int)d->nPoints + 7);           // 7 行头（6 注释+表头）
        CHECK(true);
    }

    // ---- 11. 失败点：不伪造 0，不进 CSV，进诊断（§9.6）------------------------
    {
        MeasurementEngine e = makeEngine();
        SweepEngine sw(e);
        s_cap.failBelowHz = 300.0;                    // 100/200Hz 两点失败
        SweepConfig cfg{};
        cfg.kind = MeasurementKind::OnePortImpedance;
        cfg.fStartHz = 100.0;
        cfg.fStopHz = 1000.0;
        cfg.pointsPerDecade = 5;
        cfg.maxPoints = SWEEP_MAX_POINTS;
        cfg.logSpacing = true;
        CHECK(sw.start(cfg) == SweepStatus::Ok);
        uint64_t t = 0;
        int guard = 0;
        while (sw.state() == SweepState::Measuring || sw.state() == SweepState::Sealing) {
            sw.poll(t); t += 1000;
            if (++guard > 2000000) break;
        }
        CHECK(sw.state() == SweepState::TransferReady);
        const OnePortDataset* d = sw.sealedOnePortDataset();
        CHECK(d != nullptr);
        CHECK(sw.errorCount() >= 1);
        CHECK(d->nPoints + d->diag.failedPoints == d->diag.plannedPoints);
        CHECK(d->diag.failures[0].status == MeasurementStatus::SignalTooSmall);
        // CSV 中不含 100/200Hz 行（失败点排除）
        const char* z100 = strstr(d->csv, "\n100,");
        const char* z200 = strstr(d->csv, "\n200,");
        CHECK(z100 == nullptr);
        CHECK(z200 == nullptr);
        s_cap.failBelowHz = -1.0;
    }

    // ---- 12. sweep 取消：不封存、状态 Cancelled -------------------------------
    {
        MeasurementEngine e = makeEngine();
        SweepEngine sw(e);
        SweepConfig cfg{};
        cfg.kind = MeasurementKind::OnePortImpedance;
        cfg.fStartHz = 100.0;
        cfg.fStopHz = 1000.0;
        cfg.pointsPerDecade = 20;
        cfg.maxPoints = SWEEP_MAX_POINTS;
        cfg.logSpacing = true;
        CHECK(sw.start(cfg) == SweepStatus::Ok);
        uint64_t t = 0;
        sw.poll(t);                                   // 启动第一个点
        sw.cancel();
        int guard = 0;
        while (sw.state() == SweepState::Measuring || sw.state() == SweepState::Sealing) {
            sw.poll(t); t += 1000;
            if (++guard > 2000000) break;
        }
        CHECK(sw.state() == SweepState::Cancelled);
        CHECK(sw.sealedOnePortDataset() == nullptr);  // 未封存
        CHECK(s_exc.stopped());
    }

    // ---- 13. 双端口 sweep → TWO_PORT_H dataset --------------------------------
    {
        MeasurementEngine e = makeEngine();
        SweepEngine sw(e);
        s_cap.fc = 1591.5;
        SweepConfig cfg{};
        cfg.kind = MeasurementKind::TwoPortTransfer;
        cfg.fStartHz = 100.0;
        cfg.fStopHz = 5000.0;
        cfg.pointsPerDecade = 5;
        cfg.maxPoints = SWEEP_MAX_POINTS;
        cfg.logSpacing = true;
        CHECK(sw.start(cfg) == SweepStatus::Ok);
        uint64_t t = 0;
        int guard = 0;
        while (sw.state() == SweepState::Measuring || sw.state() == SweepState::Sealing) {
            sw.poll(t); t += 1000;
            if (++guard > 2000000) break;
        }
        CHECK(sw.state() == SweepState::TransferReady);
        const TwoPortDataset* d = sw.sealedTwoPortDataset();
        CHECK(d != nullptr);
        CHECK(d->nPoints >= 5);
        // 低频点 |H|≈1（0dB 附近），高频点 gain 明显下降（§9.7 符号一致性）
        CHECK(fabs(d->points[0].gainDb) < 0.5);
        CHECK(d->points[d->nPoints - 1].gainDb < -5.0);
        CHECK(d->points[0].phaseDeg > -10.0);
        CHECK(d->points[d->nPoints - 1].phaseDeg < -60.0);
        CHECK(strstr(d->csv, "f,re_h,im_h") != nullptr);
    }

    return testSummary("test_engines");
}
