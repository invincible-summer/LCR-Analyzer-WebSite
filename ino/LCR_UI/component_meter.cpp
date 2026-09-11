// ============================================================================
// component_meter.cpp —— 三模型拟合/分类实现（host 可编译，单测覆盖）
// ============================================================================

#include "component_meter.h"

#include <math.h>

const ComponentGates& defaultComponentGates()
{
    static const ComponentGates g = {
        .maxRelWrmse = 0.05,        // 最优模型相对 WRMSE ≤ 5%
        .minRelGap = 0.15,          // 第一名须比第二名好 ≥15%
        .rMinOhm = 0.1,  .rMaxOhm = 1e7,
        .cMinF = 10e-12, .cMaxF = 1e-3,
        .lMinH = 0.1e-6, .lMaxH = 100.0,
    };
    return g;
}

const char* componentTypeText(ComponentEstimate::Type t)
{
    switch (t) {
    case ComponentEstimate::Type::Resistor:  return "RESISTOR";
    case ComponentEstimate::Type::Capacitor: return "CAPACITOR";
    case ComponentEstimate::Type::Inductor:  return "INDUCTOR";
    case ComponentEstimate::Type::Unknown:   return "UNKNOWN";
    case ComponentEstimate::Type::OutOfRange: return "OUT OF RANGE";
    }
    return "?";
}

namespace {
// 点质量权重：拟合残差越小权重越大（v1：w ∈ (0,1]）
inline double pointWeight(const OnePortPoint& p)
{
    const double relA = p.quality.amplitudeA > 0 ? p.quality.residualRmsA / p.quality.amplitudeA : 1.0;
    const double relB = p.quality.amplitudeB > 0 ? p.quality.residualRmsB / p.quality.amplitudeB : 1.0;
    const double rel = 0.5 * (relA + relB);
    return 1.0 / (1.0 + rel);
}

// 统一相对 WRMSE：sqrt(Σw²·|ΔZ|²) / sqrt(Σw²·|Z|²)（无量纲）
double relWrmse(const OnePortPoint* const* pts, const double* w, uint16_t n,
                double (*modelRe)(double, const void*),
                double (*modelIm)(double, const void*), const void* ctx)
{
    double num = 0.0, den = 0.0;
    for (uint16_t k = 0; k < n; ++k) {
        const double wk = w[k];
        const double dre = pts[k]->reOhm - modelRe(pts[k]->actualHz, ctx);
        const double dim = pts[k]->imOhm - modelIm(pts[k]->actualHz, ctx);
        num += wk * wk * (dre * dre + dim * dim);
        den += wk * wk * (pts[k]->reOhm * pts[k]->reOhm + pts[k]->imOhm * pts[k]->imOhm);
    }
    if (!(den > 0.0)) return INFINITY;
    return sqrt(num / den);
}

// 模型函数（ctx 携带单参数或双参数）
double mR_re(double, const void* c)  { return *(const double*)c; }
double mR_im(double, const void*)    { return 0.0; }
double mC_re(double, const void*)    { return 0.0; }
double mC_im(double f, const void* c)
{
    const double q = *(const double*)c;             // q = 1/C
    return -q / (2.0 * M_PI * f);
}
struct LCtx { double rd; double l; };
double mL_re(double, const void* c)  { return ((const LCtx*)c)->rd; }
double mL_im(double f, const void* c)
{
    const LCtx* p = (const LCtx*)c;
    return 2.0 * M_PI * f * p->l;
}
}  // namespace

ComponentEstimate classifyComponent(const OnePortPoint* pts, uint16_t n)
{
    ComponentEstimate e;
    const ComponentGates& g = defaultComponentGates();

    // ---- 收集有效点（Ok、有限、f>0、无质量红旗）----------------------------
    const OnePortPoint* v[8];
    double w[8];
    uint16_t nv = 0;
    for (uint16_t i = 0; i < n && nv < 8; ++i) {
        const OnePortPoint& p = pts[i];
        if (p.quality.status != MeasurementStatus::Ok) continue;
        if (p.quality.clippedChA || p.quality.clippedChB) continue;
        if (!(p.actualHz > 0.0) || !isfinite(p.reOhm) || !isfinite(p.imOhm)) continue;
        v[nv] = &p;
        w[nv] = pointWeight(p);
        ++nv;
    }
    e.nValid = nv;
    if (nv < 3) {
        e.type = ComponentEstimate::Type::Unknown;
        e.reason = "N_VALID<3";
        return e;
    }

    double sumW = 0, sumWRe = 0;
    for (uint16_t k = 0; k < nv; ++k) {
        sumW += w[k];
        sumWRe += w[k] * v[k]->reOhm;
    }
    const double rMean = sumWRe / sumW;

    // ---- R 模型：R = weightedMean(ReZ) ------------------------------------
    double rFit = rMean;
    const bool rPhys = (rFit >= g.rMinOhm && rFit <= g.rMaxOhm);

    // ---- C 模型：Im(Z_k) = −q/ω_k，加权 LS 求 q = 1/C ----------------------
    double s1 = 0, s2 = 0;               // Σw²·(−ImZ/ω)，Σw²/ω²
    for (uint16_t k = 0; k < nv; ++k) {
        const double wk = w[k];
        const double om = 2.0 * M_PI * v[k]->actualHz;
        s1 += wk * wk * (-v[k]->imOhm / om);
        s2 += wk * wk / (om * om);
    }
    const double qFit = (s2 > 0) ? s1 / s2 : 0.0;    // q = Σw²(−ImZ/ω)/Σw²(1/ω²)
    const double cFit = qFit > 0 ? 1.0 / qFit : 0.0;
    const bool cPhys = (cFit >= g.cMinF && cFit <= g.cMaxF);

    // ---- L+DCR 模型：Rd = wMean(ReZ)，L = Σw²ω·ImZ / Σw²ω² -----------------
    double s3 = 0, s4 = 0;
    for (uint16_t k = 0; k < nv; ++k) {
        const double wk = w[k];
        const double om = 2.0 * M_PI * v[k]->actualHz;
        s3 += wk * wk * om * v[k]->imOhm;
        s4 += wk * wk * om * om;
    }
    double rdFit = rMean;
    if (rdFit < 0) rdFit = 0;                        // 强制 Rd ≥ 0
    const double lFit = (s4 > 0) ? s3 / s4 : 0.0;
    const bool lPhys = (lFit >= g.lMinH && lFit <= g.lMaxH);

    // ---- 统一相对 WRMSE（完整复残差）---------------------------------------
    e.wrmseR = relWrmse(v, w, nv, mR_re, mR_im, &rFit);
    e.wrmseC = cPhys ? relWrmse(v, w, nv, mC_re, mC_im, &qFit) : INFINITY;
    LCtx lc{rdFit, lFit};
    e.wrmseL = lPhys ? relWrmse(v, w, nv, mL_re, mL_im, &lc) : INFINITY;
    e.q = qFit;

    // ---- 排序取前两名 --------------------------------------------------------
    struct Cand { char tag; double err; double param1; double param2; bool phys; };
    Cand cands[3] = {
        {'R', rPhys ? e.wrmseR : INFINITY, rFit, 0, rPhys},
        {'C', cPhys ? e.wrmseC : INFINITY, cFit, 0, cPhys},
        {'L', lPhys ? e.wrmseL : INFINITY, lFit, rdFit, lPhys},
    };
    for (int i = 0; i < 2; ++i)          // 小数组选择排序
        for (int j = i + 1; j < 3; ++j)
            if (cands[j].err < cands[i].err) { const Cand t = cands[i]; cands[i] = cands[j]; cands[j] = t; }

    e.bestWrmse = cands[0].err;
    e.runnerUpWrmse = cands[1].err;
    e.relGap = (cands[1].err < INFINITY && cands[1].err > 0)
                   ? (cands[1].err - cands[0].err) / cands[1].err : 1.0;

    // ---- 分类判据 ------------------------------------------------------------
    if (!cands[0].phys || cands[0].err >= INFINITY) {
        // 参数越界：若数值本身合理但超范围 → OUT OF RANGE
        const bool anyFinite = isfinite(e.wrmseR) || isfinite(e.wrmseC) || isfinite(e.wrmseL);
        e.type = anyFinite ? ComponentEstimate::Type::OutOfRange
                           : ComponentEstimate::Type::Unknown;
        e.reason = "PARAM OUT OF RANGE";
        e.rOhm = rFit; e.cFarad = cFit; e.lHenry = lFit; e.dcrOhm = rdFit;
        return e;
    }
    if (cands[0].err > g.maxRelWrmse) {
        e.type = ComponentEstimate::Type::Unknown;
        e.reason = "RESIDUAL TOO HIGH";
        e.rOhm = rFit; e.cFarad = cFit; e.lHenry = lFit; e.dcrOhm = rdFit;
        return e;
    }
    if (e.relGap < g.minRelGap) {
        e.type = ComponentEstimate::Type::Unknown;
        e.reason = "AMBIGUOUS MODELS";
        e.rOhm = rFit; e.cFarad = cFit; e.lHenry = lFit; e.dcrOhm = rdFit;
        return e;
    }

    switch (cands[0].tag) {
    case 'R':
        e.type = ComponentEstimate::Type::Resistor;
        e.rOhm = cands[0].param1;
        break;
    case 'C':
        e.type = ComponentEstimate::Type::Capacitor;
        e.cFarad = cands[0].param1;
        break;
    case 'L':
        e.type = ComponentEstimate::Type::Inductor;
        e.lHenry = cands[0].param1;
        e.dcrOhm = cands[0].param2;
        break;
    }
    e.reason = "";
    return e;
}
