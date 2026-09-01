// ============================================================================
// analysis.cpp —— 幅相换算与启发式识别的实现
// ============================================================================

#include "analysis.h"

#include "dsp_fit.h"

#include <math.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// 1. 幅度比 / 相位差（核心算法，非启发式）
// ---------------------------------------------------------------------------
RatioPhaseResult computeRatioPhase(const AdcSampleResult& s)
{
    RatioPhaseResult r = {0, 0, 0, 0, false};

    // 两路分别做三参数正弦拟合；用 actualFreq（而非设定频率）是关键——
    // 频率误差直接进相位误差：1 Hz 的频率偏差在 1 kHz 下 1 个周期即 0.36°。
    SineFitResult fo = sineFit3(s.outBuffer, s.sampleLength, s.actualFreq, s.samplingFreq);
    SineFitResult fi = sineFit3(s.inBuffer,  s.sampleLength, s.actualFreq, s.samplingFreq);
    if (!fo.ok || !fi.ok || fi.amp <= 0.0)
        return r;

    r.amplitudeRatio = fo.amp / fi.amp;
    r.phaseDiffDeg   = wrapDeg180((fi.phaseRad - fo.phaseRad) * 180.0 / M_PI);
    r.residRmsOut    = fo.residRms;
    r.residRmsIn     = fi.residRms;
    r.ok             = isfinite(r.amplitudeRatio);
    return r;
}

// ---------------------------------------------------------------------------
// 内部小工具：三点中值滤波（抑制单点噪声对极值/拐点判断的干扰）
// ---------------------------------------------------------------------------
static void medianSmooth3(const double* src, double* dst, int n)
{
    for (int i = 0; i < n; ++i) {
        const double a = src[i > 0 ? i - 1 : i];
        const double b = src[i];
        const double c = src[i < n - 1 ? i + 1 : i];
        const double lo = fmin(a, b), hi = fmax(a, b);
        dst[i] = fmin(fmax(c, lo), hi);   // 三数取中值
    }
}

// 在 [fLo, fHi]（对数坐标）之间对 dB 曲线做最小二乘直线拟合，返回斜率 dB/decade
static double slopeDbPerDecade(const double* f, const double* db, int n)
{
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int m = 0;
    for (int i = 0; i < n; ++i) {
        if (f[i] <= 0) continue;
        const double x = log10(f[i]);
        sx += x; sy += db[i]; sxx += x*x; sxy += x*db[i];
        ++m;
    }
    if (m < 2) return 0;
    const double den = m*sxx - sx*sx;
    if (fabs(den) < 1e-12) return 0;
    return (m*sxy - sx*sy) / den;
}

// ---------------------------------------------------------------------------
// 2. 滤波器类型 + 阶数识别（启发式）
// ---------------------------------------------------------------------------
// 思路：中值平滑后比较通带最大增益 maxG 与两端增益；按 −3dB 判据分类，
// 再对阻带侧的对数频率—dB 曲线做直线拟合，斜率 ÷20 dB/dec ≈ 阶数。

// 带通情形：低频侧通带边缘（增益回到 maxG−drop 以内的最后一个低频点）
static int iMinLow(const double* sm, int n, double maxG, double drop)
{
    int iEdge = 0;
    for (int i = 0; i < n; ++i) {
        if (sm[i] >= maxG - drop) { iEdge = i; break; }
    }
    return iEdge;
}

FilterGuess classifyFilter(const double* f, const double* gainDb, int n)
{
    FilterGuess g = {"N/A", 0, 0, 0, false};
    if (n < 5) return g;

    double* sm = (double*)malloc(sizeof(double) * n);
    if (!sm) return g;
    medianSmooth3(gainDb, sm, n);

    // 通带最大增益及其位置
    int iMax = 0;
    for (int i = 1; i < n; ++i)
        if (sm[i] > sm[iMax]) iMax = i;
    const double maxG = sm[iMax];
    // 通带最小增益及其位置（用于带阻判断）
    int iMin = 0;
    for (int i = 1; i < n; ++i)
        if (sm[i] < sm[iMin]) iMin = i;
    const double minG = sm[iMin];

    const double gLo = sm[0], gHi = sm[n - 1];
    const double drop = 3.0;   // −3dB 判据

    // 下降沿：从 from 向 to 扫描，返回增益首次跌破 maxG−drop 的频率（通带→阻带）
    auto cornerDown = [&](int from, int to) -> double {
        for (int i = from; i != to; i += (to > from ? 1 : -1))
            if (sm[i] < maxG - drop) return f[i];
        return 0;
    };
    // 上升沿：从 from 向 to 扫描，返回增益首次回到 maxG−drop 以上的频率（阻带→通带）
    auto cornerUp = [&](int from, int to) -> double {
        for (int i = from; i != to; i += (to > from ? 1 : -1))
            if (sm[i] >= maxG - drop) return f[i];
        return 0;
    };

    const bool loDown = (gLo < maxG - drop);   // 低频端衰减
    const bool hiDown = (gHi < maxG - drop);   // 高频端衰减

    // 阻带区间（maxG−6dB 以下）的斜率拟合估计阶数：阶数 = dB/dec ÷ 20。
    // 注意只对「阻带段」拟合——把通带/过渡带一起拟合会把斜率严重拉低。
    auto stopIndex = [&](int from, int to) -> int {   // 首个低于 maxG−6 的下标
        for (int i = from; i != to; i += (to > from ? 1 : -1))
            if (sm[i] < maxG - 6.0) return i;
        return to;
    };
    auto orderInBand = [&](int i0, int i1) -> int {
        if (i1 - i0 < 3) return 0;
        const double s = slopeDbPerDecade(f + i0, sm + i0, i1 - i0);
        int ord = (int)lround(fabs(s) / 20.0);
        return (ord >= 1 && ord <= 8) ? ord : 0;
    };

    if (!loDown && !hiDown) {
        // 两端都不衰减：带阻（中间有明显凹坑）或全通
        if (iMin > 0 && iMin < n - 1 && minG < maxG - 20.0) {
            g.type = "BAND-STOP";
            g.f1 = cornerUp(iMin, 0);        // 向低频找上升沿
            g.f2 = cornerUp(iMin, n - 1);    // 向高频找上升沿
        } else {
            g.type = "ALL-PASS";
        }
    } else if (!loDown && hiDown) {
        g.type = "LOW-PASS";
        g.f2 = cornerDown(iMax, n - 1);
        g.order = orderInBand(stopIndex(iMax, n - 1), n - 1);
    } else if (loDown && !hiDown) {
        g.type = "HIGH-PASS";
        g.f1 = cornerDown(iMax, 0);
        g.order = orderInBand(0, stopIndex(iMax, 0));
    } else {
        // 两端都衰减：带通
        g.type = "BAND-PASS";
        g.f1 = cornerDown(iMinLow(sm, n, maxG, drop), 0);        // 低频侧下降沿
        g.f2 = cornerDown(iMax, n - 1);                          // 高频侧下降沿
        // 阶数取两侧阻带斜率的平均
        const int iSLo = stopIndex(iMax, 0), iSHi = stopIndex(iMax, n - 1);
        int o1 = orderInBand(0, iSLo), o2 = orderInBand(iSHi, n - 1);
        g.order = (o1 + o2 + 1) / 2;   // 四舍五入
    }

    free(sm);
    g.ok = true;
    return g;
}

// ---------------------------------------------------------------------------
// 3. 单端口等效电路估计（启发式）
// ---------------------------------------------------------------------------
// 判据概要（均从数据趋势出发，不做非线性拟合，结果标注 EST）：
//   * θ 全程 |θ|<10°            → 纯电阻 R
//   * 容性（X<0）：
//       - R 随 f 上升下降超过 5 倍 → 并联 RC（低频趋于 Rp，高频只剩 C 支路）
//       - 否则                    → 串联 RC（R 恒定，X = −1/(ωC)）
//   * 感性（X>0）：R 恒定 → 串联 RL；若 X 先增后减 → 串联 RLC（谐振在带内）
EqCircuitGuess guessEquivalentCircuit(const double* f, const double* zRe,
                                      const double* zIm, int n)
{
    EqCircuitGuess g = {"N/A", 0, 0, 0, 0, false};
    if (n < 3) return g;

    // 相位与 R、X 的中值（对单点毛刺鲁棒）
    double phaseSum = 0;
    for (int i = 0; i < n; ++i)
        phaseSum += atan2(zIm[i], zRe[i]);
    const double phaseMean = phaseSum / n;                 // 平均相位（弧度）
    const double rMed = zRe[n / 2];                        // R 中值近似
    const double rLo = zRe[0], rHi = zRe[n - 1];

    // 电抗符号投票
    int nCap = 0, nInd = 0;
    for (int i = 0; i < n; ++i) (zIm[i] < 0 ? nCap : nInd)++;

    if (fabs(phaseMean) < 10.0 * M_PI / 180.0) {
        g.model = "R";
        g.Rs = rMed;
    } else if (nCap > nInd) {
        // 容性：X = −1/(ωC) → C = −1/(ωX)。取高频端估计（电抗大、相对误差小）
        double best = 0;
        for (int i = 0; i < n; ++i)
            if (f[i] > 0 && zIm[i] < 0) best = -1.0 / (2.0 * M_PI * f[i] * zIm[i]);
        g.C = best;
        const bool rFalls = (rLo > 5.0 * fmax(rHi, 1e-12));  // R 随 f 明显下降
        if (rFalls) {
            g.model = "R||C";
            g.Rp = rLo;      // 低频端阻抗实部 ≈ 并联电阻
            g.Rs = rHi;      // 高频端残余实部（损耗）
        } else {
            g.model = "R+C(s)";
            g.Rs = rMed;     // 串联电阻取中值
        }
    } else {
        // 感性：X = ωL → L = X/ω。取高频端估计（感抗大、相对误差小）
        double best = 0;
        for (int i = 0; i < n; ++i)
            if (f[i] > 0 && zIm[i] > 0) best = zIm[i] / (2.0 * M_PI * f[i]);
        g.L = best;
        // 检查 X 是否先增后减（带内谐振 → 串联 RLC）
        int iXmax = 0;
        for (int i = 1; i < n; ++i) if (zIm[i] > zIm[iXmax]) iXmax = i;
        if (iXmax > 0 && iXmax < n - 1 && zIm[n - 1] < 0.5 * zIm[iXmax]) {
            g.model = "RLC(s)";
            // 谐振频率处 C = 1/(ω₀²L)
            if (f[iXmax] > 0 && g.L > 0)
                g.C = 1.0 / (pow(2.0 * M_PI * f[iXmax], 2) * g.L);
        } else {
            g.model = "R+L(s)";
            g.Rs = rMed;
        }
    }

    g.ok = true;
    return g;
}
