// ============================================================================
// analysis.h —— 测量数据分析：幅相换算 / 滤波器类型识别 / 等效电路估计
// ----------------------------------------------------------------------------
// 1) computeRatioPhase：对 AdcSampleResult 的两个 buffer 分别做正弦拟合，
//    按 partner_api.h 顶部 ★★ 约定产出 amplitudeRatio 与 phaseDiff。
// 2) classifyFilter：  由扫频增益曲线估计 高通/低通/带通/带阻 及阶数（启发式）。
// 3) guessEquivalentCircuit：由单端口扫频的 (R, X) 序列估计等效电路模型。
//
// 后两个功能为任务书中的选做项，算法为工程启发式，界面上一律标注 EST(估计)。
// 本模块纯 C++，可 PC 端单测（ino/test/test_dsp.cpp）。
// ============================================================================

#pragma once

#include "partner_api.h"

/// 幅度比 + 相位差计算结果（定义见 partner_api.h 顶部）
struct RatioPhaseResult {
    double amplitudeRatio;   ///< A(outBuffer)/A(inBuffer)，原始码单位
    double phaseDiffDeg;     ///< φ(inBuffer) − φ(outBuffer)，度，(−180,180]
    double residRmsOut;      ///< 激励通道拟合残差 RMS（质量指示）
    double residRmsIn;       ///< 响应通道拟合残差 RMS（质量指示）
    bool   ok;               ///< 任一通道拟合失败 / 幅度为 0 时为 false
};

/// 由一次采样结果计算幅度比与相位差（内部调用 sineFit3）
RatioPhaseResult computeRatioPhase(const AdcSampleResult& s);

/// 滤波器类型识别结果（启发式估计）
struct FilterGuess {
    const char* type;   ///< "LOW-PASS" / "HIGH-PASS" / "BAND-PASS" / "BAND-STOP"
                        ///< / "ALL-PASS" / "N/A"
    int    order;       ///< 估计阶数（0 = 无法估计）
    double f1;          ///< 下限 −3dB 频率 Hz（带通/带阻有效，其余为 0）
    double f2;          ///< 上限 −3dB 频率 Hz（带通/带阻有效；低/高通为拐点）
    bool   ok;
};

/// 由双端口扫频的增益曲线估计滤波器类型与阶数
FilterGuess classifyFilter(const double* f, const double* gainDb, int n);

/// 单端口等效电路估计结果（启发式，仅支持 R / 串联RC / 并联RC / 串联RL / RLC 串联谐振）
struct EqCircuitGuess {
    const char* model;  ///< "R" / "R+C(s)" / "R||C" / "R+L(s)" / "RLC(s)" / "N/A"
    double Rs;          ///< 串联电阻估计值 (Ω)
    double Rp;          ///< 并联电阻估计值 (Ω)
    double L;           ///< 电感估计值 (H)
    double C;           ///< 电容估计值 (F)
    bool   ok;
};

/// 由单端口扫频的复阻抗序列估计等效电路（各频率点 R=Re, X=Im）
EqCircuitGuess guessEquivalentCircuit(const double* f, const double* zRe,
                                      const double* zIm, int n);
