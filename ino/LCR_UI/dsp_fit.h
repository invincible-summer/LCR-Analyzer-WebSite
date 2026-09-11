// ============================================================================
// dsp_fit.h —— 三参数正弦最小二乘拟合（IEEE 1057 / 与后端 sine_fit 同源）
// ----------------------------------------------------------------------------
// 模型：x(t) = a·sin(ωt) + b·cos(ωt) + c，  ω = 2πf
// 在激励频率 f 已知的前提下对采样序列做线性最小二乘，同时得到：
//   幅度 A = hypot(a, b)，相位 φ = atan2(b, a)，直流 c，残差 RMS。
//
// 优点（相对 FFT 取单 bin）：无频谱泄漏、无需加窗、相位干净，
// 在加性白高斯噪声下是最优（最大似然）估计 —— 与本项目后端
// backend/app/dsp/sine_fit.py 的算法完全同源，便于交叉验证。
//
// 时间模型（v4.1，plan.md §3.3）：拟合 API 不再隐含「两通道样本在同一
// 时刻」。SampleSeries 携带 t[k] = t0 + k·dt 的确定性模型；t0 承载同一
// ADC pattern 交错采样的通道 skew，相位差按 Δφ = 2πf·Δt 显式补偿。
//
// 本模块为纯 C++（不依赖任何 Arduino 头文件），可在 PC 上用 g++ 单测：
// 见 ino/test/ 与 ino/tools/run_tests.sh。
// ============================================================================

#pragma once

#include <stddef.h>
#include <stdint.h>

/// 三参数正弦拟合结果。ok=false 表示输入不合法或法方程病态（数据不可用）。
struct SineFitResult {
    double amp;        ///< 正弦幅度（与输入序列同单位）
    double phaseRad;   ///< 相位 φ = atan2(b, a)，弧度（SampleSeries 时间基准）
    double dc;         ///< 直流分量拟合值
    double residRms;   ///< 残差 RMS（噪声/失真的度量）
    uint32_t usedSamples;
    bool   ok;         ///< 拟合是否成功
};

/// 带确定性时间模型的采样序列（已按通道解交错）
struct SampleSeries {
    const int16_t* samples;   ///< 本通道样本（原始 ADC 码或已定标电压）
    size_t count;             ///< 本通道样本数
    double dt;                ///< 相邻样本间隔（秒）
    double t0;                ///< 首样本相对 capture 起点的时间（通道 skew）
};

/// 兼容旧等间隔调用的三参数拟合（t0 = 0）；host 单测保留。
SineFitResult sineFit3(const int16_t* x, size_t n, double freqHz,
                       double sampleFreqHz);

/// 扩展 API：按 SampleSeries 的真实时间基准拟合（v4.1 主路径）
SineFitResult sineFitTimed(const SampleSeries& s, double actualFreqHz);

/// 把角度（度）折算到 (−180, 180]
double wrapDeg180(double deg);
