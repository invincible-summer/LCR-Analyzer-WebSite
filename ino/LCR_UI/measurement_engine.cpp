// ============================================================================
// measurement_engine.cpp —— 状态机实现（host 可编译；mock 驱动下全路径可测）
// ============================================================================

#include "measurement_engine.h"
#include "dsp_fit.h"
#include "radio_lock.h"

#include <math.h>

const char* engineStateText(MeasurementEngineState s)
{
    switch (s) {
    case MeasurementEngineState::Idle:             return "IDLE";
    case MeasurementEngineState::Prepare:          return "PREPARE";
    case MeasurementEngineState::ExcitationStart:  return "EXCITATION_START";
    case MeasurementEngineState::Settling:         return "SETTLING";
    case MeasurementEngineState::CaptureArm:       return "CAPTURE_ARM";
    case MeasurementEngineState::Capturing:        return "CAPTURING";
    case MeasurementEngineState::Fitting:          return "FITTING";
    case MeasurementEngineState::ApplyCalibration: return "APPLY_CAL";
    case MeasurementEngineState::ResultReady:      return "RESULT_READY";
    case MeasurementEngineState::Cancelling:       return "CANCELLING";
    case MeasurementEngineState::SafeOff:          return "SAFE_OFF";
    case MeasurementEngineState::ErrorSafeOff:     return "ERROR_SAFE_OFF";
    }
    return "?";
}

const QualityGates& defaultQualityGates()
{
    static const QualityGates g = {
        .minAmplitudeMv = 20.0,
        .freqRelTol = 0.01,       // 1%：actualHz 与 requested 的最大相对偏差
        .residOverAmpMax = 1.0,
        .maxSamplesPerChannel = 8192,
        .maxSettleUs = 2000000ull,   // 2 s
    };
    return g;
}

MeasurementEngine::MeasurementEngine(IExcitationSource& exc, ICaptureDevice& cap,
                                     IFrontEnd* front, const CalibrationProfile& cal,
                                     double currentSenseOhm, double transimpedanceGain)
    : m_exc(exc), m_cap(cap), m_front(front), m_cal(cal),
      m_rSenseOhm(currentSenseOhm), m_tiaGain(transimpedanceGain)
{
}

bool MeasurementEngine::active() const
{
    switch (m_state) {
    case MeasurementEngineState::Prepare:
    case MeasurementEngineState::ExcitationStart:
    case MeasurementEngineState::Settling:
    case MeasurementEngineState::CaptureArm:
    case MeasurementEngineState::Capturing:
    case MeasurementEngineState::Fitting:
    case MeasurementEngineState::ApplyCalibration:
    case MeasurementEngineState::ResultReady:
        return true;
    default:
        return false;
    }
}

float MeasurementEngine::progress() const
{
    switch (m_state) {
    case MeasurementEngineState::Idle:        return 0.0f;
    case MeasurementEngineState::Prepare:     return 0.05f;
    case MeasurementEngineState::ExcitationStart: return 0.15f;
    case MeasurementEngineState::Settling:    return 0.35f;
    case MeasurementEngineState::CaptureArm:  return 0.45f;
    case MeasurementEngineState::Capturing: {
        if (m_captureStartUs == 0 || m_targetPerCh == 0) return 0.5f;
        // 无绝对时钟依赖：按已推进的时间估算（上限 1）
        return 0.5f;
    }
    case MeasurementEngineState::Fitting:     return 0.8f;
    case MeasurementEngineState::ApplyCalibration: return 0.9f;
    case MeasurementEngineState::ResultReady: return 1.0f;
    default:                                  return 0.0f;
    }
}

// ---------------------------------------------------------------------------
MeasurementStatus MeasurementEngine::start(const MeasurementRequest& request)
{
    if (active()) return MeasurementStatus::InvalidConfig;   // Busy 语义
    if (!(request.requestedHz > 0.0)) return MeasurementStatus::InvalidConfig;
    if (request.kind != MeasurementKind::OnePortImpedance &&
        request.kind != MeasurementKind::TwoPortTransfer)
        return MeasurementStatus::InvalidConfig;
    if (request.settleCycles == 0 || request.captureCycles == 0 ||
        request.minSamplesPerChannel < 8)
        return MeasurementStatus::InvalidConfig;

    m_req = request;
    m_haveResult = false;
    m_lastStatus = MeasurementStatus::Ok;
    m_state = MeasurementEngineState::Prepare;
    radioLockNotifyMeasurementActive(true);
    return MeasurementStatus::Ok;
}

void MeasurementEngine::cancel()
{
    if (!active() && m_state != MeasurementEngineState::Idle) return;
    if (!active()) return;
    m_state = MeasurementEngineState::Cancelling;
    m_lastStatus = MeasurementStatus::Cancelled;
}

void MeasurementEngine::enterError(MeasurementStatus st, uint64_t)
{
    m_lastStatus = st;
    m_state = MeasurementEngineState::ErrorSafeOff;
}

// 统一安全落点：停激励 → 取消/释放 ADC → 前端失能
void MeasurementEngine::safeOff()
{
    m_exc.excitationStop();
    m_cap.captureCancel();
    m_cap.captureRelease();
    if (m_front) m_front->frontEndDisable();
}

// ---------------------------------------------------------------------------
void MeasurementEngine::poll(uint64_t nowUs)
{
#ifdef LCR_DEBUG_INVARIANTS
    if (!radioLockInvariantOk()) {
        // 射频与测量互斥被破坏：立即安全化（debug 断言语义）
        enterError(MeasurementStatus::InternalError, nowUs);
    }
#endif

    switch (m_state) {
    case MeasurementEngineState::Idle:
        return;

    case MeasurementEngineState::Prepare: {
        if (m_front) m_front->frontEndEnable(0);      // v1 固定量程 0
        m_state = MeasurementEngineState::ExcitationStart;
        return;
    }

    case MeasurementEngineState::ExcitationStart: {
        ExcitationConfig cfg{};
        cfg.requestedHz = m_req.requestedHz;
        cfg.requestedVrms = m_req.driveVrms;
        cfg.waveform = Waveform::Sine;
        if (m_exc.excitationBegin(cfg) != ExcitationStatus::Ok) {
            enterError(MeasurementStatus::ExcitationFail, nowUs);
            return;
        }
        m_excState = m_exc.excitationState();
        if (!(m_excState.actualHz > 0.0) ||
            fabs(m_excState.actualHz - m_req.requestedHz) >
                defaultQualityGates().freqRelTol * m_req.requestedHz) {
            enterError(MeasurementStatus::FrequencyMismatch, nowUs);
            return;
        }
        // 建立期：settleCycles 个周期，封顶 maxSettleUs
        const double perCycleUs = 1e6 / m_excState.actualHz;
        uint64_t settle = (uint64_t)(perCycleUs * m_req.settleCycles);
        if (settle > defaultQualityGates().maxSettleUs)
            settle = defaultQualityGates().maxSettleUs;
        m_settleDeadlineUs = nowUs + settle;
        m_state = MeasurementEngineState::Settling;
        return;
    }

    case MeasurementEngineState::Settling:
        if (nowUs < m_settleDeadlineUs) return;    // 时间截止点推进，无 delay()
        m_state = MeasurementEngineState::CaptureArm;
        return;

    case MeasurementEngineState::CaptureArm:
        requestCapture();
        return;

    case MeasurementEngineState::Capturing: {
        m_cap.capturePoll();
        if (m_cap.captureStatus() == CaptureStatus::DmaOverrun) {
            // DMA overflow → 显式失败，绝不用残缺 buffer 拟合
            enterError(MeasurementStatus::AdcOverrun, nowUs);
            return;
        }
        if (!m_cap.captureDone()) return;
        m_state = MeasurementEngineState::Fitting;
        return;
    }

    case MeasurementEngineState::Fitting:
        if (!fitAndBuildResult(nowUs)) return;     // enterError 已在内部完成
        m_state = MeasurementEngineState::ApplyCalibration;
        return;

    case MeasurementEngineState::ApplyCalibration:
        // 测量已完成：先落安全态（停激励 + 释放 ADC/前端），结果保持可取
        safeOff();
        m_state = MeasurementEngineState::ResultReady;
        m_lastStatus = MeasurementStatus::Ok;
        return;

    case MeasurementEngineState::ResultReady:
        return;                                     // 等 takeResult

    case MeasurementEngineState::Cancelling:
    case MeasurementEngineState::ErrorSafeOff:
        safeOff();
        m_state = MeasurementEngineState::SafeOff;
        return;

    case MeasurementEngineState::SafeOff:
        m_state = MeasurementEngineState::Idle;
        radioLockNotifyMeasurementActive(false);
        return;
    }
}

// ---------------------------------------------------------------------------
void MeasurementEngine::requestCapture()
{
    CaptureRequest cr{};
    cr.channelCount = 2;
    cr.channels[0] = m_req.kind == MeasurementKind::OnePortImpedance
                         ? CaptureChannel::VoltageDut : CaptureChannel::Port2Input;
    cr.channels[1] = m_req.kind == MeasurementKind::OnePortImpedance
                         ? CaptureChannel::CurrentSense : CaptureChannel::Port2Output;
    cr.excitationHz = m_excState.actualHz;
    cr.captureCycles = m_req.captureCycles;
    cr.minSamplesPerChannel = m_req.minSamplesPerChannel;

    // 每通道目标速率：min(pattern 上限/2, 32 点/周期)，下限保证 ≥12 点/周期
    const double f = m_excState.actualHz;
    uint32_t perCh = (uint32_t)(32.0 * f);
    const uint32_t capPerCh = m_cap.captureMaxPatternRateHz() / 2;
    if (perCh > capPerCh) perCh = capPerCh;
    const uint32_t minPerCh = (uint32_t)(12.0 * f) + 1;
    if (perCh < minPerCh) perCh = minPerCh;
    cr.targetSampleRateHz = perCh;

    uint32_t want = (uint32_t)((double)perCh * m_req.captureCycles / f) + 1;
    if (want > defaultQualityGates().maxSamplesPerChannel)
        want = defaultQualityGates().maxSamplesPerChannel;
    if (want < m_req.minSamplesPerChannel)
        want = m_req.minSamplesPerChannel;
    m_targetPerCh = want;
    m_patternRateHz = perCh * 2;                    // 2 通道交错

    if (m_cap.captureStart(cr) != CaptureStatus::Ok) {
        enterError(MeasurementStatus::AdcFail, 0);
        m_state = MeasurementEngineState::ErrorSafeOff;
        return;
    }
    // 交错采样确定性 skew = 1 个 pattern 间隔（第二通道落后第一通道）
    m_skewNs = (uint32_t)((1e9 / (double)m_patternRateHz) + 0.5);
    m_state = MeasurementEngineState::Capturing;
}

// ---------------------------------------------------------------------------
// 拟合 + 校准 + 结果（单端口 Z=V/I；双端口 H=Vout/Vin）
// ---------------------------------------------------------------------------
bool MeasurementEngine::fitAndBuildResult(uint64_t)
{
    const CaptureChannel chA = m_req.kind == MeasurementKind::OnePortImpedance
                                   ? CaptureChannel::VoltageDut : CaptureChannel::Port2Input;
    const CaptureChannel chB = m_req.kind == MeasurementKind::OnePortImpedance
                                   ? CaptureChannel::CurrentSense : CaptureChannel::Port2Output;
    const TimedSamples sA = m_cap.captureChannel(chA);
    const TimedSamples sB = m_cap.captureChannel(chB);
    const double f = m_excState.actualHz;
    const QualityGates& g = defaultQualityGates();

    MeasurementQuality q{};
    q.status = MeasurementStatus::Ok;
    q.frequencyLocked = true;                       // ExcitationStart 已核过
    q.channelSkewSeconds = sB.t0 - sA.t0;

    if (sA.count < m_req.minSamplesPerChannel || sB.count < m_req.minSamplesPerChannel) {
        q.status = MeasurementStatus::AdcFail;
        q.samplesA = (uint32_t)sA.count;
        q.samplesB = (uint32_t)sB.count;
        enterError(MeasurementStatus::AdcFail, 0);
        return false;
    }

    // 削顶检测（样本为 mV；削顶 = 贴近 0 或贴近满量程）
    const uint16_t fsMv = m_cap.captureFullScaleMv();
    q.clippedChA = q.clippedChB = false;
    for (size_t i = 0; i < sA.count && !q.clippedChA; ++i)
        if (sA.samples[i] <= 2 || sA.samples[i] >= fsMv - 2) q.clippedChA = true;
    for (size_t i = 0; i < sB.count && !q.clippedChB; ++i)
        if (sB.samples[i] <= 2 || sB.samples[i] >= fsMv - 2) q.clippedChB = true;
    if (q.clippedChA || q.clippedChB) {
        q.status = MeasurementStatus::Clipped;
        enterError(MeasurementStatus::Clipped, 0);
        return false;
    }

    // 三参数正弦拟合（真实时间基准：t0 承载通道 skew）
    const SampleSeries sa{sA.samples, sA.count, sA.dt, sA.t0};
    const SampleSeries sb{sB.samples, sB.count, sB.dt, sB.t0};
    const SineFitResult fA = sineFitTimed(sa, f);
    const SineFitResult fB = sineFitTimed(sb, f);
    if (!fA.ok || !fB.ok) {
        q.status = MeasurementStatus::FitSingular;
        enterError(MeasurementStatus::FitSingular, 0);
        return false;
    }
    q.residualRmsA = fA.residRms;
    q.residualRmsB = fB.residRms;
    q.amplitudeA = fA.amp;
    q.amplitudeB = fB.amp;
    q.samplesA = fA.usedSamples;
    q.samplesB = fB.usedSamples;

    if (fA.amp < g.minAmplitudeMv || fB.amp < g.minAmplitudeMv) {
        q.status = MeasurementStatus::SignalTooSmall;
        enterError(MeasurementStatus::SignalTooSmall, 0);
        return false;
    }
    if (fA.residRms > g.residOverAmpMax * fA.amp ||
        fB.residRms > g.residOverAmpMax * fB.amp) {
        q.status = MeasurementStatus::FitSingular;
        enterError(MeasurementStatus::FitSingular, 0);
        return false;
    }

    // ---- 复数校准（plan.md §4）------------------------------------------
    ComplexCorrection cvA{}, cvB{};
    const double freqForCal = f;
    if (m_req.kind == MeasurementKind::OnePortImpedance) {
        const CalPathStatus st1 = m_cal.voltagePath(freqForCal, cvA);
        const CalPathStatus st2 = m_cal.currentPath(freqForCal, cvB);
        if (st1 == CalPathStatus::OutOfRange || st2 == CalPathStatus::OutOfRange) {
            q.status = MeasurementStatus::CalibrationStale;
            enterError(MeasurementStatus::CalibrationStale, 0);
            return false;
        }
        if (st1 == CalPathStatus::MalformedTable || st2 == CalPathStatus::MalformedTable) {
            q.status = MeasurementStatus::CalibrationMissing;
            enterError(MeasurementStatus::CalibrationMissing, 0);
            return false;
        }
    } else {
        const CalPathStatus st1 = m_cal.port2InputPath(freqForCal, cvA);
        const CalPathStatus st2 = m_cal.port2OutputPath(freqForCal, cvB);
        if (st1 == CalPathStatus::OutOfRange || st2 == CalPathStatus::OutOfRange) {
            q.status = MeasurementStatus::CalibrationStale;
            enterError(MeasurementStatus::CalibrationStale, 0);
            return false;
        }
        if (st1 == CalPathStatus::MalformedTable || st2 == CalPathStatus::MalformedTable) {
            q.status = MeasurementStatus::CalibrationMissing;
            enterError(MeasurementStatus::CalibrationMissing, 0);
            return false;
        }
    }

    // 通道复相量（sin 约定）：X = A·e^{jφ}；校准：X' = gain·e^{jphase}·X
    struct Cpx { double re, im; };
    auto phasor = [](const SineFitResult& fr, const ComplexCorrection& c) -> Cpx {
        const double mag = c.gain * fr.amp;
        const double ph = fr.phaseRad + c.phaseRad;
        return Cpx{mag * cos(ph), mag * sin(ph)};
    };
    const Cpx vc = phasor(fA, cvA);                  // mV·校准
    const Cpx ic = phasor(fB, cvB);                  // mV·校准

    if (m_req.kind == MeasurementKind::OnePortImpedance) {
        // V [V] = v/1000；I [A] = (i/1000) / (Rsense·TiaGain)
        const double denomScale = 1000.0 * m_rSenseOhm * (m_tiaGain > 0 ? m_tiaGain : 1.0);
        const double iReA = ic.re / denomScale, iImA = ic.im / denomScale;
        const double vReV = vc.re / 1000.0, vImV = vc.im / 1000.0;
        const double d = iReA * iReA + iImA * iImA;
        if (!(d > 0.0)) {
            q.status = MeasurementStatus::FitSingular;
            enterError(MeasurementStatus::FitSingular, 0);
            return false;
        }
        OnePortPoint& p = m_result1;
        p.requestedHz = m_req.requestedHz;
        p.actualHz = f;
        p.reOhm = (vReV * iReA + vImV * iImA) / d;
        p.imOhm = (vImV * iReA - vReV * iImA) / d;
        p.magOhm = hypot(p.reOhm, p.imOhm);
        p.phaseDeg = wrapDeg180(atan2(p.imOhm, p.reOhm) * 180.0 / M_PI);
        p.quality = q;
        m_haveResult = true;
    } else {
        // H = Vout/Vin（统一传递函数定义；mV 比 mV，尺度相消）
        const double d = vc.re * vc.re + vc.im * vc.im;
        if (!(d > 0.0)) {
            q.status = MeasurementStatus::FitSingular;
            enterError(MeasurementStatus::FitSingular, 0);
            return false;
        }
        TwoPortPoint& p = m_result2;
        p.requestedHz = m_req.requestedHz;
        p.actualHz = f;
        p.reH = (ic.re * vc.re + ic.im * vc.im) / d;
        p.imH = (ic.im * vc.re - ic.re * vc.im) / d;
        p.gainDb = 20.0 * log10(hypot(p.reH, p.imH));
        p.phaseDeg = wrapDeg180(atan2(p.imH, p.reH) * 180.0 / M_PI);
        p.quality = q;
        m_haveResult = true;
    }
    return true;
}

// ---------------------------------------------------------------------------
bool MeasurementEngine::takeResult(OnePortPoint& out)
{
    if (m_state != MeasurementEngineState::ResultReady ||
        m_req.kind != MeasurementKind::OnePortImpedance || !m_haveResult)
        return false;
    out = m_result1;
    m_state = MeasurementEngineState::SafeOff;
    return true;
}

bool MeasurementEngine::takeResult(TwoPortPoint& out)
{
    if (m_state != MeasurementEngineState::ResultReady ||
        m_req.kind != MeasurementKind::TwoPortTransfer || !m_haveResult)
        return false;
    out = m_result2;
    m_state = MeasurementEngineState::SafeOff;
    return true;
}
