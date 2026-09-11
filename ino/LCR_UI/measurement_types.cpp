// ============================================================================
// measurement_types.cpp —— 纯逻辑实现（host 可编译，单测覆盖）
// ============================================================================

#include "measurement_types.h"

#include <math.h>

const char* measurementKindText(MeasurementKind k)
{
    return k == MeasurementKind::OnePortImpedance ? "ONE_PORT_Z" : "TWO_PORT_H";
}

const char* sweepStatusText(SweepStatus s)
{
    switch (s) {
    case SweepStatus::Ok:            return "OK";
    case SweepStatus::InvalidConfig: return "INVALID CONFIG";
    case SweepStatus::Busy:          return "BUSY";
    case SweepStatus::NotReady:      return "NOT READY";
    case SweepStatus::Cancelled:     return "CANCELLED";
    case SweepStatus::Error:         return "ERROR";
    }
    return "?";
}

const char* sweepStateText(SweepState s)
{
    switch (s) {
    case SweepState::Idle:          return "IDLE";
    case SweepState::Measuring:     return "MEASURING";
    case SweepState::Stopping:      return "STOPPING";
    case SweepState::TransferReady: return "TRANSFER READY";
    case SweepState::Insufficient:  return "DATA INSUFFICIENT";
    case SweepState::Error:         return "ERROR";
    case SweepState::Cancelled:     return "CANCELLED";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// 频率表：对数几何分布（首尾精确）。点数 = round(ppd * log10(f1/f0)) + 1，
// 超过 maxPoints/cap 时整体压缩为 maxPoints 点（用最终 n 重算网格，
// 保证点距均匀，不产生“截尾”）。
// ---------------------------------------------------------------------------
size_t buildFrequencyPlan(const SweepConfig& cfg, double* freqs, size_t cap)
{
    if (!freqs || cap < 2) return 0;
    if (!(cfg.fStartHz > 0.0) || !(cfg.fStopHz > cfg.fStartHz)) return 0;
    if (cfg.pointsPerDecade == 0 || cfg.maxPoints < 2) return 0;
    if (cfg.fStartHz < INSTRUMENT_F_MIN_HZ || cfg.fStopHz > INSTRUMENT_F_MAX_HZ)
        return 0;

    const double dec = log10(cfg.fStopHz / cfg.fStartHz);
    size_t n = 1 + (size_t)lround((double)cfg.pointsPerDecade * dec);
    const size_t lim = cap < cfg.maxPoints ? cap : cfg.maxPoints;
    if (n > lim) n = lim;
    if (n < 2) return 0;

    for (size_t i = 0; i < n; ++i) {
        const double t = (double)i / (double)(n - 1);
        freqs[i] = cfg.fStartHz * pow(10.0, dec * t);
    }
    freqs[0] = cfg.fStartHz;        // 首尾精确（清掉浮点累积误差）
    freqs[n - 1] = cfg.fStopHz;
    return n;
}

void wMagPhaseToComplex(double hMag, double phaseDeg, double* reH, double* imH)
{
    if (!reH || !imH) return;
    if (!isfinite(hMag) || !isfinite(phaseDeg)) { *reH = NAN; *imH = NAN; return; }
    const double rad = phaseDeg * M_PI / 180.0;
    *reH = hMag * cos(rad);
    *imH = hMag * sin(rad);
}
