// ============================================================================
// sweep_engine.cpp —— 扫频编排状态机实现（host 可编译；mock service 下全路径可测）
// ============================================================================

#include "sweep_engine.h"

#include <math.h>
#include <string.h>

// StopTone 完成后的静默保护：停激励 -> seal 之间的最短间隔
static constexpr uint32_t kQuietGuardMs = 20;
// 单口进入网站拟合的最低有效点数（parseZCsv 至少需要 4 点）
static constexpr size_t kOnePortMinFit = 4;
// 双口画曲线的最低有效点数（parseHCsv 至少需要 2 点）
static constexpr size_t kTwoPortMinPlot = 2;

// millis()/uint32_t deadline comparison. Valid for deadlines less than 2^31 ms
// into the future; kQuietGuardMs is only 20 ms. This remains correct when
// nowMs wraps from 0xffffffff to 0, unlike plain nowMs >= deadline.
static bool deadlineReached(uint32_t nowMs, uint32_t deadlineMs)
{
    return (int32_t)(nowMs - deadlineMs) >= 0;
}

// ---------------------------------------------------------------------------
// chunk 规划：偶数 N 全 2 点；奇数 N（>=3）为若干 2 点块 + 末块 3 点。
// 第 k 块的起始网格下标恒为 2k（2 点块各消费 2 个下标）。
// ---------------------------------------------------------------------------
// chunk 总数（与 planSweepChunks 的划分一致；独立成函数供 start/poll 计数）
static size_t sweepChunkCount(size_t nPoints)
{
    if (nPoints < 2) return 0;
    return (nPoints % 2 == 0) ? nPoints / 2 : (nPoints - 1) / 2;
}

size_t planSweepChunks(const double* grid, size_t nPoints, SweepChunk* out, size_t cap)
{
    if (!grid || !out || nPoints < 2) return 0;
    const size_t nC = sweepChunkCount(nPoints);
    if (nC > cap) return 0;
    for (size_t k = 0; k < nC; ++k) {
        const size_t first = 2 * k;
        const bool last = (k + 1 == nC);
        const size_t nPts = (last && nPoints % 2 == 1) ? 3 : 2;
        out[k].fStartHz = grid[first];
        out[k].fStopHz = grid[first + nPts - 1];
        out[k].nPts = (uint8_t)nPts;
    }
    return nC;
}

SweepEngine::SweepEngine(ILcrService& svc) : m_svc(svc) {}

size_t SweepEngine::chunkCount() const { return sweepChunkCount(m_nPoints); }

void SweepEngine::chunkAt(size_t k, SweepChunk& out) const
{
    const size_t nC = (m_nPoints % 2 == 0) ? m_nPoints / 2 : (m_nPoints - 1) / 2;
    const size_t first = 2 * k;
    const bool last = (k + 1 == nC);
    const size_t nPts = (last && m_nPoints % 2 == 1) ? 3 : 2;
    out.fStartHz = m_freqs[first];
    out.fStopHz = m_freqs[first + nPts - 1];
    out.nPts = (uint8_t)nPts;
}

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
    if (m_nPoints == 0) return 0.0;
    size_t i = 2 * m_nextChunk;
    if (i >= m_nPoints) i = m_nPoints - 1;
    return m_freqs[i];
}

// ---------------------------------------------------------------------------
SweepStatus SweepEngine::start(const SweepConfig& cfg)
{
    if (m_state == SweepState::Measuring || m_state == SweepState::Stopping)
        return SweepStatus::Busy;
    if (m_svc.busy()) return SweepStatus::Busy;

    SweepConfig sane = cfg;
    if (sane.maxPoints == 0 || sane.maxPoints > SWEEP_MAX_POINTS)
        sane.maxPoints = SWEEP_MAX_POINTS;
    const size_t n = buildFrequencyPlan(sane, m_freqs, SWEEP_MAX_POINTS);
    if (n < 2) return SweepStatus::InvalidConfig;
    if (sweepChunkCount(n) == 0) return SweepStatus::InvalidConfig;

    m_cfg = sane;
    m_nPoints = n;
    m_nextChunk = 0;
    m_processed = 0;
    m_pendingId = 0;
    m_cancelReq = false;
    m_stopDone = false;
    m_cal = AppCalSummary{};
    m_diag = DatasetDiag{};
    m_diag.plannedPoints = (uint16_t)n;
    memset(&m_data1, 0, sizeof(m_data1));
    memset(&m_data2, 0, sizeof(m_data2));
    m_data1.kind = MeasurementKind::OnePortImpedance;
    m_data2.kind = MeasurementKind::TwoPortTransfer;

    // 测量窗口开启（radio_lock invariant：此期间 RadioState 必须 Off）
    radioLockNotifyMeasurementActive(true);

    // 先取一次真实校准状态（seal 时写进 v2 头部，不杜撰）
    if (!submitJob(LcrJobKind::ReadCalibrationStatus, 0, 0, 0)) {
        radioLockNotifyMeasurementActive(false);
        m_state = SweepState::Error;
        return SweepStatus::Error;
    }
    m_state = SweepState::Measuring;
    return SweepStatus::Ok;
}

bool SweepEngine::submitJob(LcrJobKind kind, double a, double b, uint8_t nPts)
{
    LcrJob job{};
    job.kind = kind;
    job.fStartHz = a;
    job.fStopHz = b;
    job.pointCount = nPts;
    job.frequencyHz = a;
    if (!m_svc.submit(job)) return false;
    m_pendingId = job.id;
    return true;
}

void SweepEngine::cancel()
{
    if (m_state != SweepState::Measuring && m_state != SweepState::Stopping) return;
    m_cancelReq = true;
    m_svc.requestCancel();   // Worker 丢弃后续测量类 job；当前 job 正常完成
}

// ---------------------------------------------------------------------------
void SweepEngine::poll(uint32_t nowMs)
{
#ifdef LCR_DEBUG_INVARIANTS
    if (!radioLockInvariantOk() &&
        (m_state == SweepState::Measuring || m_state == SweepState::Stopping)) {
        // 不可直接置 Error 后退出：此时 DNT job/激励可能仍在运行。
        // 转入正常 cancel -> StopTone 收尾路径，直到停激励事件确认后才解锁。
        m_cancelReq = true;
        m_svc.requestCancel();
    }
#endif

    if (m_state == SweepState::Measuring || m_state == SweepState::Stopping) {
        LcrEvent ev;
        while (m_svc.takeEvent(ev)) handleEvent(ev, nowMs);
    }

    if (m_state == SweepState::Measuring) {
        if (m_pendingId != 0) return;                 // 等当前 job 的事件
        if (m_cancelReq) { beginStop(); return; }     // 取消：不再提交新块
        if (m_nextChunk < chunkCount()) {
            SweepChunk c;
            chunkAt(m_nextChunk, c);
            const LcrJobKind k = (m_cfg.kind == MeasurementKind::OnePortImpedance)
                                     ? LcrJobKind::SweepZChunk
                                     : LcrJobKind::SweepWChunk;
            submitJob(k, c.fStartHz, c.fStopHz, c.nPts);  // 失败下轮重试
        } else {
            beginStop();
        }
        return;
    }

    if (m_state == SweepState::Stopping) {
        if (!m_stopDone && m_pendingId == 0)          // StopTone 提交失败的
            submitJob(LcrJobKind::StopTone, 0, 0, 0); // 有界重试
        else if (m_stopDone && deadlineReached(nowMs, m_quietDeadlineMs))
            finishSeal(nowMs);          // StopTone 完成 + 静默到期 -> seal
        return;
    }
}

// ---------------------------------------------------------------------------
void SweepEngine::handleEvent(const LcrEvent& ev, uint32_t nowMs)
{
    if (ev.id != m_pendingId) return;      // 过期/无关事件（如取消丢弃补发）
    m_pendingId = 0;

    switch (ev.kind) {
    case LcrJobKind::ReadCalibrationStatus:
        // 当前 DNT 对非空 out 固定成功；若未来契约扩展，失败不能伪装成 cal:0/10。
        if (ev.backendStatus == 0) {
            m_cal = ev.cal;
        } else {
            m_cancelReq = true;
            m_svc.requestCancel();
        }
        break;

    case LcrJobKind::SweepZChunk:
    case LcrJobKind::SweepWChunk: {
        const bool z = (ev.kind == LcrJobKind::SweepZChunk);
        for (uint8_t i = 0; i < ev.pointCount && i < 3; ++i) {
            bool ok = false;
            double f = 0, a = 0, b = 0;
            if (z) {
                ok = ev.z[i].apiStatus == 0 && isfinite(ev.z[i].fAct) &&
                     ev.z[i].fAct > 0.0 && isfinite(ev.z[i].reOhm) &&
                     isfinite(ev.z[i].imOhm);
                if (ok) { f = ev.z[i].fAct; a = ev.z[i].reOhm; b = ev.z[i].imOhm; }
                else    { f = ev.z[i].fReq; }
            } else {
                ok = ev.w[i].apiStatus == 0 && isfinite(ev.w[i].fAct) &&
                     ev.w[i].fAct > 0.0 && isfinite(ev.w[i].reH) &&
                     isfinite(ev.w[i].imH);
                if (ok) { f = ev.w[i].fAct; a = ev.w[i].reH; b = ev.w[i].imH; }
                else    { f = ev.w[i].fReq; }
            }
            if (ok) {
                if (z && m_data1.nPoints < SWEEP_MAX_POINTS) {
                    m_data1.points[m_data1.nPoints] = {f, a, b};
                    ++m_data1.nPoints;
                } else if (!z && m_data2.nPoints < SWEEP_MAX_POINTS) {
                    m_data2.points[m_data2.nPoints] = {f, a, b};
                    ++m_data2.nPoints;
                }
            } else if (m_diag.failedPoints < SWEEP_MAX_POINTS) {
                m_diag.failures[m_diag.failedPoints].requestedHz = f;
                m_diag.failures[m_diag.failedPoints].apiStatus =
                    z ? ev.z[i].apiStatus : ev.w[i].apiStatus;
                ++m_diag.failedPoints;
            }
            ++m_processed;
        }
        ++m_nextChunk;      // 当前块已完整返回（无论其中几点失败）
        break;
    }

    case LcrJobKind::StopTone:
        m_stopDone = true;
        m_quietDeadlineMs = nowMs + kQuietGuardMs;
        radioLockNotifyMeasurementActive(false);   // 测量窗口结束
        break;

    default:
        break;
    }
}

void SweepEngine::beginStop()
{
    m_state = SweepState::Stopping;
    m_stopDone = false;
    if (!submitJob(LcrJobKind::StopTone, 0, 0, 0)) {
        // 队列满：下一轮 poll 重试（pendingId==0 且 state==Stopping）
    }
}

// ---------------------------------------------------------------------------
void SweepEngine::finishSeal(uint32_t nowMs)
{
    m_diag.validPoints = m_cfg.kind == MeasurementKind::OnePortImpedance
                             ? m_data1.nPoints : m_data2.nPoints;

    if (m_cancelReq) {
        m_cancelReq = false;
        m_state = SweepState::Cancelled;       // 取消：不封存
        return;
    }

    const uint32_t sessionId =
        nowMs ^ (++m_sessionCounter * 2654435761u);

    if (m_cfg.kind == MeasurementKind::OnePortImpedance) {
        OnePortDataset& d = m_data1;
        d.diag = m_diag;
        d.sessionId = sessionId;
        formatCalStateString(m_cal.rangesValid, m_cal.openValid, m_cal.shortValid,
                             d.calibrationState, sizeof(d.calibrationState));
        strncpy(d.measurementBackend, "DO_NOT_TOUCH_lcr_api",
                sizeof(d.measurementBackend) - 1);
        d.csvLen = (uint32_t)formatOnePortCsv(d.points, d.nPoints,
                                              d.calibrationState, d.csv,
                                              sizeof(d.csv));
        d.crc32 = (d.csvLen > 0) ? crc32Of((const uint8_t*)d.csv, d.csvLen) : 0;
        d.sealedUptimeMs = nowMs;
        d.sealed = d.csvLen > 0;
        if (!d.sealed)                       m_state = SweepState::Error;
        else if (d.nPoints < kOnePortMinFit) m_state = SweepState::Insufficient;
        else                                 m_state = SweepState::TransferReady;
    } else {
        TwoPortDataset& d = m_data2;
        d.diag = m_diag;
        d.sessionId = sessionId;
        // 双口 W 链在 DNT 中明确为 raw chain / no calib —— 如实标注
        strncpy(d.calibrationState, "raw_w_path", sizeof(d.calibrationState) - 1);
        strncpy(d.measurementBackend, "DO_NOT_TOUCH_lcr_api",
                sizeof(d.measurementBackend) - 1);
        d.csvLen = (uint32_t)formatTwoPortCsv(d.points, d.nPoints,
                                              d.calibrationState, d.csv,
                                              sizeof(d.csv));
        d.crc32 = (d.csvLen > 0) ? crc32Of((const uint8_t*)d.csv, d.csvLen) : 0;
        d.sealedUptimeMs = nowMs;
        d.sealed = d.csvLen > 0;
        if (!d.sealed)                       m_state = SweepState::Error;
        else if (d.nPoints < kTwoPortMinPlot) m_state = SweepState::Insufficient;
        else                                 m_state = SweepState::TransferReady;
    }
}
