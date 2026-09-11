// ============================================================================
// component_meter.cpp —— API 结果一致性判型 + 中位数聚合（host 可编译）
// ============================================================================

#include "component_meter.h"

#include <math.h>

const char* componentTypeText(ComponentEstimate::Type t)
{
    switch (t) {
    case ComponentEstimate::Type::Resistor:  return "RESISTOR";
    case ComponentEstimate::Type::Capacitor: return "CAPACITOR";
    case ComponentEstimate::Type::Inductor:  return "INDUCTOR";
    case ComponentEstimate::Type::Unknown:   return "UNKNOWN";
    }
    return "?";
}

namespace {

// 中位数（升序排序后取中间；偶数个取中间两数均值）。调用者保证 n>0。
double medianOf(double* v, size_t n)
{
    // 小数组插入排序足够（n ≤ 8）
    for (size_t i = 1; i < n; ++i) {
        const double key = v[i];
        size_t j = i;
        while (j > 0 && v[j - 1] > key) { v[j] = v[j - 1]; --j; }
        v[j] = key;
    }
    if (n % 2 == 1) return v[n / 2];
    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

inline bool zPointUsable(const AppZPoint& p)
{
    return p.apiStatus == 0 && p.apiType != 'E' && p.apiType != '\0' &&
           isfinite(p.fAct) && p.fAct > 0.0 &&
           isfinite(p.reOhm) && isfinite(p.imOhm);
}

inline bool calcUsable(const AppCalcResult& c, char wantType)
{
    return c.apiStatus == 0 && c.type == wantType && isfinite(c.rs);
}

}  // namespace

ComponentEstimate summarizeComponent(const AppZPoint* z, const AppCalcResult* calc,
                                     uint16_t n)
{
    ComponentEstimate e;
    if (!z || !calc || n == 0) { e.reason = "NO DATA"; return e; }

    // ---- 收集“API 测量 + 换算”都有效、且换算型与测点型一致的点 --------
    // 下标表（有效点最多 8 个足够：本模式固定 5 点）
    static constexpr int kMaxPts = 8;
    int idx[kMaxPts];
    int nv = 0;
    for (uint16_t i = 0; i < n && nv < kMaxPts; ++i) {
        if (!zPointUsable(z[i])) continue;
        if (calc[i].apiStatus != 0 || !isfinite(calc[i].rs)) continue;
        if (calc[i].type != z[i].apiType) continue;   // 换算与测点自相矛盾
        idx[nv++] = (int)i;
    }
    e.nValid = (uint8_t)nv;

    if (nv < 3) { e.reason = "N_VALID<3"; return e; }

    // ---- apiType 一致性（保守规则，plan.md §6.3）------------------------
    // 全部一致 -> 通过；仅当 nValid>=5 且恰有 1 点不一致时也通过（4/5 门限）。
    int cntR = 0, cntC = 0, cntL = 0;
    for (int k = 0; k < nv; ++k) {
        const char t = z[idx[k]].apiType;
        if (t == 'R') ++cntR;
        else if (t == 'C') ++cntC;
        else if (t == 'L') ++cntL;
    }
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

    // ---- 只用与最终判型一致的点做中位数聚合（过滤 NaN/Inf）--------------
    double v[8];
    int m = 0;
    for (int k = 0; k < nv; ++k) {
        const AppCalcResult& c = calc[idx[k]];
        if (!calcUsable(c, best)) continue;
        double val = 0.0;
        if (best == 'R') val = c.rs;
        else if (best == 'C') val = c.cs;
        else val = c.ls;
        if (!isfinite(val)) continue;
        v[m++] = val;
    }
    if (m < 3) { e.reason = "CALC N_VALID<3"; return e; }
    const double value = medianOf(v, (size_t)m);

    if (best == 'R') {
        e.type = ComponentEstimate::Type::Resistor;
        e.rOhm = value;
    } else if (best == 'C') {
        e.type = ComponentEstimate::Type::Capacitor;
        e.cFarad = value;
    } else {
        e.type = ComponentEstimate::Type::Inductor;
        e.lHenry = value;
        // DCR = median(rs)：负值不钳位（物理模型 DCR>=0），交 UI 告警
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
