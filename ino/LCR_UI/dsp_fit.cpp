// ============================================================================
// dsp_fit.cpp —— 三参数正弦拟合实现
// ----------------------------------------------------------------------------
// 数值方法：构造法方程 M·θ = v（M 为 3x3 对称正定矩阵），用 Cholesky 分解求解。
// 基函数 {sin ωt, cos ωt, 1} 在窗长覆盖多个周期时近似正交，法方程条件数良好；
// 使用 double 精度累加，N ≤ 几千点时舍入误差可忽略。
//
// 推导（与后端 docs/algorithms.md §时域正弦拟合 一致）：
//   J(a,b,c) = Σ (x[k] − a·s[k] − b·c[k] − c)² ，s=sin(ωt)，c=cos(ωt)
//   令 ∂J/∂θ = 0 得：
//   [ Σs²  Σsc  Σs ] [a]   [ Σsx ]
//   [ Σsc  Σc²  Σc ] [b] = [ Σcx ]
//   [ Σs   Σc   N  ] [c]   [ Σx  ]
// ============================================================================

#include "dsp_fit.h"

#include <math.h>

SineFitResult sineFit3(const int16_t* x, int n, double freqHz, double sampleFreqHz)
{
    SineFitResult r = {0, 0, 0, 0, false};
    if (!x || n < 8 || freqHz <= 0.0 || sampleFreqHz <= 0.0)
        return r;

    const double w = 2.0 * M_PI * freqHz;      // 角频率
    const double dt = 1.0 / sampleFreqHz;      // 采样间隔

    // ---- 累加法方程矩阵与右端项 -------------------------------------------
    double Ss = 0, Sc = 0;        // Σsin, Σcos
    double Sss = 0, Scc = 0, Ssc = 0;
    double Ssx = 0, Scx = 0, Sx = 0, Sxx = 0;
    for (int k = 0; k < n; ++k) {
        const double ph = w * (double)k * dt;
        // 长窗时 sin/cos 实参会增大，但 double 精度下 sin(2π·1e4·N/fs) 仍足够；
        // 若 f/fs 接近整数可改用相位折叠，此处采样率由队友保证远高于信号频率。
        const double s  = sin(ph);
        const double c  = cos(ph);
        const double xk = (double)x[k];
        Ss  += s;   Sc  += c;
        Sss += s*s; Scc += c*c; Ssc += s*c;
        Ssx += s*xk; Scx += c*xk; Sx += xk; Sxx += xk*xk;
    }

    // ---- Cholesky 分解 M = L·Lᵀ（3x3 手写展开） ----------------------------
    const double m00 = Sss, m01 = Ssc, m02 = Ss;
    const double m11 = Scc, m12 = Sc;
    const double m22 = (double)n;

    const double l00 = sqrt(m00);
    if (l00 <= 0.0) return r;
    const double l10 = m01 / l00;
    const double l11 = m11 - l10*l10;
    if (l11 <= 0.0) return r;                 // 病态：基函数线性相关
    const double l20 = m02 / l00;
    const double l21 = (m12 - l20*l10) / sqrt(l11);
    const double l22 = m22 - l20*l20 - l21*l21;
    if (l22 <= 1e-9 * (double)n) return r;    // 病态

    // 前代 L·y = v
    const double y0 = Ssx / l00;
    const double y1 = (Scx - l10*y0) / sqrt(l11);
    const double y2 = (Sx - l20*y0 - l21*y1) / sqrt(l22);
    // 回代 Lᵀ·θ = y
    const double cC = y2 / sqrt(l22);
    const double bB = (y1 - l21*cC) / sqrt(l11);
    const double aA = (y0 - l10*bB - l20*cC) / l00;

    // ---- 残差 RMS（第二遍扫描，精确计算） ----------------------------------
    double rsum = 0.0;
    for (int k = 0; k < n; ++k) {
        const double ph = w * (double)k * dt;
        const double model = aA*sin(ph) + bB*cos(ph) + cC;
        const double e = (double)x[k] - model;
        rsum += e*e;
    }

    r.amp      = hypot(aA, bB);
    r.phaseRad = atan2(bB, aA);               // 相位约定：a=A·cosφ, b=A·sinφ
    r.dc       = cC;
    r.residRms = sqrt(rsum / (double)n);
    r.ok       = (r.amp > 0.0);
    return r;
}

double wrapDeg180(double deg)
{
    if (!isfinite(deg)) return deg;
    deg = fmod(deg, 360.0);
    if (deg < 0.0) deg += 360.0;   // 折到 [0, 360)
    if (deg > 180.0) deg -= 360.0; // 折到 (−180, 180]
    return deg;
}
