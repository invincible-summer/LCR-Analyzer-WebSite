// ============================================================================
// dsp_fit.h —— 三参数正弦最小二乘拟合（IEEE 1057 / 与后端 sine_fit 同源）
// ----------------------------------------------------------------------------
// 模型：x[k] = a·sin(ωt_k) + b·cos(ωt_k) + c，  t_k = k / fs，  ω = 2πf
// 在激励频率 f 已知的前提下对采样序列做线性最小二乘，同时得到：
//   幅度 A = hypot(a, b)，相位 φ = atan2(b, a)，直流 c，残差 RMS。
//
// 优点（相对 FFT 取单 bin）：无频谱泄漏、无需加窗、相位干净，
// 在加性白高斯噪声下是最优（最大似然）估计 —— 与本项目后端
// backend/app/dsp/sine_fit.py 的算法完全同源，便于交叉验证。
//
// 本模块为纯 C++（不依赖任何 Arduino 头文件），可在 PC 上用 g++ 单测：
// 见 ino/test/test_dsp.cpp 与 ino/tools/run_tests.sh。
// ============================================================================

#pragma once

#include <stdint.h>

/// 三参数正弦拟合结果。ok=false 表示输入不合法或法方程病态（数据不可用）。
struct SineFitResult {
    double amp;        ///< 正弦幅度（与输入序列同单位）
    double phaseRad;   ///< 相位 φ = atan2(b, a)，弧度
    double dc;         ///< 直流分量拟合值
    double residRms;   ///< 残差 RMS（噪声/失真的度量，可用于界面上的质量指示）
    bool   ok;         ///< 拟合是否成功
};

/// 对 n 点采样序列 x 做 3 参数正弦拟合，激励频率 freqHz、采样率 sampleFreqHz。
/// 前提（来自 docs/api_contract.md 的时间约定）：窗长覆盖 ≥1 个完整信号周期，
/// 建议 ≥10 周期、每周期 ≥32 点；不满足时相位精度下降，本函数不报错。
SineFitResult sineFit3(const int16_t* x, int n, double freqHz, double sampleFreqHz);

/// 把角度（度）折算到 (−180, 180]
double wrapDeg180(double deg);
