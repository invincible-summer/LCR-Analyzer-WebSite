// ============================================================================
// measurement_types.cpp —— 纯逻辑实现（host 可编译，单测覆盖）
// ============================================================================

#include "measurement_types.h"

#include <math.h>

const char* measurementStatusText(MeasurementStatus s)
{
    switch (s) {
    case MeasurementStatus::Ok:                 return "OK";
    case MeasurementStatus::InvalidConfig:      return "INVALID CONFIG";
    case MeasurementStatus::ExcitationFail:     return "EXCITATION FAIL";
    case MeasurementStatus::AdcFail:            return "ADC FAIL";
    case MeasurementStatus::AdcOverrun:         return "ADC OVERRUN";
    case MeasurementStatus::Clipped:            return "CLIPPED";
    case MeasurementStatus::SignalTooSmall:     return "SIGNAL TOO SMALL";
    case MeasurementStatus::FitSingular:        return "FIT SINGULAR";
    case MeasurementStatus::FrequencyMismatch:  return "FREQ MISMATCH";
    case MeasurementStatus::CalibrationMissing: return "CAL MISSING";
    case MeasurementStatus::CalibrationStale:   return "CAL STALE";
    case MeasurementStatus::Cancelled:          return "CANCELLED";
    case MeasurementStatus::InternalError:      return "INTERNAL ERROR";
    }
    return "?";
}

const char* measurementKindText(MeasurementKind k)
{
    return k == MeasurementKind::OnePortImpedance ? "ONE_PORT_Z" : "TWO_PORT_H";
}

const char* sweepStateText(SweepState s)
{
    switch (s) {
    case SweepState::Idle:           return "IDLE";
    case SweepState::Measuring:      return "MEASURING";
    case SweepState::Sealing:        return "SEALING";
    case SweepState::TransferReady:  return "TRANSFER READY";
    case SweepState::Error:          return "ERROR";
    case SweepState::Cancelled:      return "CANCELLED";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// 频率表：对数（首尾精确）或线性等间隔，几何分布
// ---------------------------------------------------------------------------
size_t buildFrequencyPlan(const SweepConfig& cfg, double* freqs, size_t cap)
{
    if (!freqs || cap == 0) return 0;
    if (!(cfg.fStartHz > 0.0) || !(cfg.fStopHz > cfg.fStartHz)) return 0;
    if (cfg.pointsPerDecade == 0 || cfg.maxPoints == 0) return 0;

    size_t n;
    if (cfg.logSpacing) {
        const double dec = log10(cfg.fStopHz / cfg.fStartHz);
        n = 1 + (size_t)lround((double)cfg.pointsPerDecade * dec);
    } else {
        n = 1 + (size_t)cfg.pointsPerDecade;
    }
    if (n < 2) n = 2;
    if (n > cap || n > cfg.maxPoints) n = (cap < cfg.maxPoints ? cap : cfg.maxPoints);
    if (n < 2) return 0;

    for (size_t i = 0; i < n; ++i) {
        if (cfg.logSpacing) {
            const double t = (double)i / (double)(n - 1);
            freqs[i] = cfg.fStartHz * pow(10.0, log10(cfg.fStopHz / cfg.fStartHz) * t);
        } else {
            freqs[i] = cfg.fStartHz +
                       (cfg.fStopHz - cfg.fStartHz) * (double)i / (double)(n - 1);
        }
    }
    freqs[0] = cfg.fStartHz;       // 首尾精确（浮点累积误差清零）
    freqs[n - 1] = cfg.fStopHz;
    return n;
}
