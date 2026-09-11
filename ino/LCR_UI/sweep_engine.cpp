// ============================================================================
// sweep_engine.cpp —— 扫频状态机实现（host 可编译；mock 驱动下全路径可测）
// ============================================================================

#include "sweep_engine.h"
#include "radio_lock.h"

#include <math.h>
#include <string.h>

// 每个频点的测量参数缺省（v1；实板标定后按 §9.5 规则冻结，不悄悄放宽）
static constexpr uint16_t kSettleCycles = 8;
static constexpr uint16_t kCaptureCycles = 12;
static constexpr uint16_t kMinSamples = 200;
// 封存前静默保护：停激励/ADC 后的射频开启前等待（plan.md §5.2 quiet guard）
static constexpr uint64_t kQuietGuardUs = 50000;   // 50 ms

const char* sweepStatusText(SweepStatus s)
{
    switch (s) {
    case SweepStatus::Ok:            return "OK";
    case SweepStatus::InvalidConfig: return "INVALID CONFIG";
    case SweepStatus::Busy:          return "BUSY";
    case SweepStatus::EngineError:   return "ENGINE ERROR";
    case SweepStatus::Cancelled:     return "CANCELLED";
    }
    return "?";
}

SweepEngine::SweepEngine(MeasurementEngine& engine) : m_engine(engine) {}

const OnePortDataset* SweepEngine::sealedOnePortDataset() const
{
    return (m_state == SweepState::TransferReady && m_data1.sealed) ? &m_data1 : nullptr;
}
const TwoPortDataset* SweepEngine::sealedTwoPortDataset() const
{
    return (m_state == SweepState::TransferReady && m_data2.sealed) ? &m_data2 : nullptr;
}

double SweepEngine::currentFreqHz() const
{
    if (m_nextIdx >= m_nPoints) return 0.0;
    return m_freqs[m_nextIdx];
}

// ---------------------------------------------------------------------------
SweepStatus SweepEngine::start(const SweepConfig& cfg)
{
    if (m_state == SweepState::Measuring || m_state == SweepState::Sealing)
        return SweepStatus::Busy;
    if (m_engine.active()) return SweepStatus::EngineError;
    if (!(cfg.fStartHz > 0.0) || !(cfg.fStopHz > cfg.fStartHz))
        return SweepStatus::InvalidConfig;
    if (cfg.fStartHz < INSTRUMENT_F_MIN_HZ || cfg.fStopHz > INSTRUMENT_F_MAX_HZ)
        return SweepStatus::InvalidConfig;
    if (cfg.pointsPerDecade == 0 || cfg.maxPoints == 0)
        return SweepStatus::InvalidConfig;

    SweepConfig sane = cfg;
    sane.maxPoints = cfg.maxPoints > SWEEP_MAX_POINTS ? SWEEP_MAX_POINTS : cfg.maxPoints;
    const size_t n = buildFrequencyPlan(sane, m_freqs, SWEEP_MAX_POINTS);
    if (n < 2) return SweepStatus::InvalidConfig;

    m_cfg = sane;
    m_nPoints = n;
    m_nextIdx = 0;
    m_valid = 0;
    m_diag = DatasetDiag{};
    m_diag.plannedPoints = (uint16_t)n;
    m_cancelReq = false;
    memset(&m_data1, 0, sizeof(m_data1));
    memset(&m_data2, 0, sizeof(m_data2));
    m_state = SweepState::Measuring;
    return SweepStatus::Ok;
}

void SweepEngine::cancel()
{
    if (m_state != SweepState::Measuring && m_state != SweepState::Sealing) return;
    m_cancelReq = true;
    m_engine.cancel();
}

// ---------------------------------------------------------------------------
void SweepEngine::poll(uint64_t nowUs)
{
#ifdef LCR_DEBUG_INVARIANTS
    if (!radioLockInvariantOk() && m_state == SweepState::Measuring) {
        m_state = SweepState::Error;
        m_engine.cancel();
        return;
    }
#endif

    if (m_state == SweepState::Measuring) {
        if (m_cancelReq) {
            if (m_engine.active()) {
                m_engine.cancel();
                m_engine.poll(nowUs);
                return;
            }
            m_state = SweepState::Cancelled;
            m_cancelReq = false;
            return;
        }

        if (m_engine.active()) {
            m_engine.poll(nowUs);
            if (m_engine.resultReady()) {
                if (m_cfg.kind == MeasurementKind::OnePortImpedance) {
                    OnePortPoint p;
                    if (m_engine.takeResult(p)) { storePointOk(p); m_pointInFlight = false; }
                } else {
                    TwoPortPoint p;
                    if (m_engine.takeResult(p)) { storePointOk(p); m_pointInFlight = false; }
                }
                m_engine.poll(nowUs);          // 推过 SafeOff -> Idle（一次有界）
            }
            return;
        }

        // 引擎空闲：若上一点以失败收场，记录诊断（不中止整次扫频）
        if (m_pointInFlight) {
            storePointFail(m_freqs[m_nextIdx], m_engine.lastStatus());
            m_pointInFlight = false;
            ++m_nextIdx;
        }

        if (m_nextIdx >= m_nPoints) {
            beginSeal(nowUs);
            return;
        }
        MeasurementRequest req{};
        req.kind = m_cfg.kind;
        req.requestedHz = m_freqs[m_nextIdx];
        req.driveVrms = m_cfg.driveVrms;
        req.settleCycles = kSettleCycles;
        req.captureCycles = kCaptureCycles;
        req.minSamplesPerChannel = kMinSamples;
        if (m_engine.start(req) == MeasurementStatus::Ok) {
            m_pointInFlight = true;
        } else {
            storePointFail(m_freqs[m_nextIdx], MeasurementStatus::InternalError);
            ++m_nextIdx;
            if (m_nextIdx >= m_nPoints) beginSeal(nowUs);
        }
        return;
    }

    if (m_state == SweepState::Sealing) {
        if (m_cancelReq) { m_state = SweepState::Cancelled; m_cancelReq = false; return; }
        if (nowUs >= m_quietDeadlineUs) finishSeal(nowUs);
        return;
    }
}

// ---------------------------------------------------------------------------
void SweepEngine::storePointOk(const OnePortPoint& p)
{
    m_pts1[m_valid] = p;
    ++m_valid;
    ++m_nextIdx;
}

void SweepEngine::storePointOk(const TwoPortPoint& p)
{
    m_pts2[m_valid] = p;
    ++m_valid;
    ++m_nextIdx;
}

void SweepEngine::storePointFail(double requestedHz, MeasurementStatus st)
{
    if (m_diag.failedPoints < SWEEP_MAX_POINTS) {
        m_diag.failures[m_diag.failedPoints].requestedHz = requestedHz;
        m_diag.failures[m_diag.failedPoints].status = st;
        ++m_diag.failedPoints;
    }
}

void SweepEngine::beginSeal(uint64_t nowUs)
{
    m_state = SweepState::Sealing;
    m_quietDeadlineUs = nowUs + kQuietGuardUs;
}

void SweepEngine::finishSeal(uint64_t nowUs)
{
    const uint32_t sessionId = (uint32_t)(nowUs & 0xFFFFFFFFull) ^ (++m_sessionCounter * 2654435761u);
    if (m_cfg.kind == MeasurementKind::OnePortImpedance) {
        OnePortDataset& d = m_data1;
        d.kind = MeasurementKind::OnePortImpedance;
        memcpy(d.points, m_pts1, sizeof(OnePortPoint) * m_valid);
        d.nPoints = (uint16_t)m_valid;
        d.diag = m_diag;
        d.diag.validPoints = (uint16_t)m_valid;
        d.sessionId = sessionId;
        d.driveVrms = 1.05;   // calibrated nominal drive（与 ExcitationDriver 一致）
        strncpy(d.calibrationId, "factory-none", sizeof(d.calibrationId) - 1);
        d.csvLen = (uint32_t)formatOnePortCsv(d.points, d.nPoints, d.calibrationId,
                                             d.driveVrms, d.csv, sizeof(d.csv));
        d.crc32 = (d.csvLen > 0) ? crc32Of((const uint8_t*)d.csv, d.csvLen) : 0;
        d.sealedUnixMs = nowUs / 1000ull;
        d.sealed = d.csvLen > 0;
        m_state = d.sealed ? SweepState::TransferReady : SweepState::Error;
    } else {
        TwoPortDataset& d = m_data2;
        d.kind = MeasurementKind::TwoPortTransfer;
        memcpy(d.points, m_pts2, sizeof(TwoPortPoint) * m_valid);
        d.nPoints = (uint16_t)m_valid;
        d.diag = m_diag;
        d.diag.validPoints = (uint16_t)m_valid;
        d.sessionId = sessionId;
        d.driveVrms = 1.05;
        strncpy(d.calibrationId, "factory-none", sizeof(d.calibrationId) - 1);
        d.csvLen = (uint32_t)formatTwoPortCsv(d.points, d.nPoints, d.calibrationId,
                                             d.driveVrms, d.csv, sizeof(d.csv));
        d.crc32 = (d.csvLen > 0) ? crc32Of((const uint8_t*)d.csv, d.csvLen) : 0;
        d.sealedUnixMs = nowUs / 1000ull;
        d.sealed = d.csvLen > 0;
        m_state = d.sealed ? SweepState::TransferReady : SweepState::Error;
    }
}
