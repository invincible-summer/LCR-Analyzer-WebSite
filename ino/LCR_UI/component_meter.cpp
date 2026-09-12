// ============================================================================
// component_meter.cpp —— API 一致性判型 + 中位数聚合 + 诊断信息
// ============================================================================

#include "component_meter.h"

#include <math.h>

const char* componentTypeText(ComponentEstimate::Type t)
{
    switch (t) {
    case ComponentEstimate::Type::Resistor:  return "RESISTOR";
    case ComponentEstimate::Type::Capacitor: return "CAPACITOR";
    case ComponentEstimate::Type::Inductor:  return "INDUCTOR";
    case ComponentEstimate::Type::Active:    return "ACTIVE";
    case ComponentEstimate::Type::Unknown:   return "UNKNOWN";
    }
    return "?";
}

const char* impedanceNatureText(ImpedanceNature n)
{
    switch (n) {
    case ImpedanceNature::Resistive:         return "RESISTIVE";
    case ImpedanceNature::Capacitive:        return "CAPACITIVE";
    case ImpedanceNature::Inductive:         return "INDUCTIVE";
    case ImpedanceNature::NegativeResistive: return "NEGATIVE-R";
    case ImpedanceNature::Invalid:            return "INVALID";
    }
    return "INVALID";
}

namespace {

inline bool zPointMeasured(const AppZPoint& p)
{
    return p.apiStatus == 0 && p.apiType != 'E' && p.apiType != '\0' &&
           isfinite(p.fAct) && p.fAct > 0.0 && isfinite(p.reOhm) &&
           isfinite(p.imOhm) && isfinite(p.phaseDeg);
}

inline bool calcBasicValid(const AppCalcResult& c)
{
    return c.apiStatus == 0 && isfinite(c.rs);
}

inline bool calcUsable(const AppCalcResult& c, char wantType)
{
    return calcBasicValid(c) && c.type == wantType;
}

double medianOf(double* v, size_t n)
{
    for (size_t i = 1; i < n; ++i) {
        const double key = v[i];
        size_t j = i;
        while (j > 0 && v[j - 1] > key) { v[j] = v[j - 1]; --j; }
        v[j] = key;
    }
    if (n % 2 == 1) return v[n / 2];
    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

inline bool clearlyNegativeResistance(const AppZPoint& p)
{
    const double scale = fmax(1.0, isfinite(p.magOhm) ? fabs(p.magOhm)
                                                        : hypot(p.reOhm, p.imOhm));
    // 避免把“电抗占主导、仅 Rs/DCR 因噪声略为负”的点直接判成有源。
    // ACTIVE 需要负实部显著且实部至少与电抗同量级；接近 180deg 的点
    // 由下方 near180 强证据单独捕获。
    return p.reOhm < -1e-6 * scale && fabs(p.reOhm) >= fabs(p.imOhm);
}

}  // namespace

ImpedanceNature classifyImpedanceNature(const AppZPoint& p, double tolDeg)
{
    if (!zPointMeasured(p) || !(tolDeg >= 0.0)) return ImpedanceNature::Invalid;
    double a = fmod(fabs(p.phaseDeg), 360.0);
    if (a > 180.0) a = 360.0 - a;
    if (fabs(180.0 - a) <= tolDeg) return ImpedanceNature::NegativeResistive;
    if (a <= tolDeg) return ImpedanceNature::Resistive;
    if (p.imOhm < 0.0) return ImpedanceNature::Capacitive;
    if (p.imOhm > 0.0) return ImpedanceNature::Inductive;
    return ImpedanceNature::Resistive;
}

ComponentEstimate summarizeComponent(const AppZPoint* z, const AppCalcResult* calc,
                                     uint16_t n)
{
    ComponentEstimate e;
    if (!z || !calc || n == 0) { e.reason = "NO DATA"; return e; }

    const uint16_t mid = n / 2;
    for (uint16_t i = 0; i < n; ++i) {
        if (!zPointMeasured(z[i])) continue;
        ++e.nMeasured;
        if (e.detailIndex < 0) e.detailIndex = (int8_t)i;
        if (clearlyNegativeResistance(z[i])) ++e.nNegativeReal;
        if (calcBasicValid(calc[i])) ++e.nCalcValid;
        if (calcBasicValid(calc[i]) && calc[i].type != z[i].apiType)
            ++e.nTypeMismatch;
    }
    if (mid < n && zPointMeasured(z[mid])) e.detailIndex = (int8_t)mid;

    bool near180 = false;
    for (uint16_t i = 0; i < n; ++i)
        if (classifyImpedanceNature(z[i]) == ImpedanceNature::NegativeResistive)
            near180 = true;
    if (e.nMeasured > 0 &&
        (near180 || (uint16_t)e.nNegativeReal * 2U > (uint16_t)e.nMeasured)) {
        e.type = ComponentEstimate::Type::Active;
        e.reason = "NEGATIVE REAL Z";
        e.nValid = e.nMeasured;
        e.nConsistent = e.nNegativeReal;
        e.representativeIndex = e.detailIndex;
        if (e.representativeIndex >= 0)
            e.representativeFreqHz = z[e.representativeIndex].fAct;
        return e;
    }

    static constexpr int kMaxPts = 8;
    int idx[kMaxPts];
    int nv = 0;
    for (uint16_t i = 0; i < n && nv < kMaxPts; ++i) {
        if (!zPointMeasured(z[i]) || !calcBasicValid(calc[i])) continue;
        if (calc[i].type != z[i].apiType) continue;
        idx[nv++] = (int)i;
    }
    e.nValid = (uint8_t)nv;

    int cntR = 0, cntC = 0, cntL = 0;
    for (int k = 0; k < nv; ++k) {
        const char t = z[idx[k]].apiType;
        if (t == 'R') ++cntR;
        else if (t == 'C') ++cntC;
        else if (t == 'L') ++cntL;
    }
    e.nR = (uint8_t)cntR; e.nC = (uint8_t)cntC; e.nL = (uint8_t)cntL;
    if (nv < 3) { e.reason = "N_VALID<3"; return e; }

    char best = 0;
    int bestCnt = 0;
    if (cntR >= cntC && cntR >= cntL && cntR > 0)      { best = 'R'; bestCnt = cntR; }
    else if (cntC >= cntL && cntC > 0)                 { best = 'C'; bestCnt = cntC; }
    else if (cntL > 0)                                 { best = 'L'; bestCnt = cntL; }
    if (best == 0) { e.reason = "NO TYPE"; return e; }
    e.nConsistent = (uint8_t)bestCnt;
    if (bestCnt != nv && !(nv >= 5 && bestCnt == nv - 1)) {
        e.reason = "TYPE INCONSISTENT";
        return e;
    }

    double values[8];
    int valueIdx[8];
    int m = 0;
    for (int k = 0; k < nv; ++k) {
        const int i = idx[k];
        const AppCalcResult& c = calc[i];
        if (!calcUsable(c, best)) continue;
        double val = NAN;
        if (best == 'R') val = c.rs;
        else if (best == 'C') val = c.cs;
        else val = c.ls;
        if (!isfinite(val)) continue;
        values[m] = val;
        valueIdx[m] = i;
        ++m;
    }
    if (m < 3) { e.reason = "CALC N_VALID<3"; return e; }

    double sorted[8];
    for (int i = 0; i < m; ++i) sorted[i] = values[i];
    const double value = medianOf(sorted, (size_t)m);

    int rep = valueIdx[0];
    double repErr = fabs(values[0] - value);
    for (int i = 1; i < m; ++i) {
        const double d = fabs(values[i] - value);
        if (d < repErr || (d == repErr && z[valueIdx[i]].fAct < z[rep].fAct)) {
            repErr = d;
            rep = valueIdx[i];
        }
    }
    e.representativeIndex = (int8_t)rep;
    e.representativeFreqHz = z[rep].fAct;

    if (best == 'R') {
        e.type = ComponentEstimate::Type::Resistor;
        e.rOhm = value;
    } else if (best == 'C') {
        e.type = ComponentEstimate::Type::Capacitor;
        e.cFarad = value;
    } else {
        e.type = ComponentEstimate::Type::Inductor;
        e.lHenry = value;
        double d[8];
        int dm = 0;
        for (int k = 0; k < nv; ++k) {
            const AppCalcResult& c = calc[idx[k]];
            if (calcUsable(c, 'L') && isfinite(c.rs)) d[dm++] = c.rs;
        }
        if (dm > 0) {
            e.dcrOhm = medianOf(d, (size_t)dm);
            if (e.dcrOhm < 0.0) e.dcrWarn = true;
        }
    }
    e.reason = "";
    return e;
}
