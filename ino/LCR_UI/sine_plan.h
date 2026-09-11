// ============================================================================
// sine_plan.h —— 激励正弦的「精确有理频率」规划（host 可编译）
// ----------------------------------------------------------------------------
// 问题：sine fit 的相位精度要求 |actualHz − fitHz|/fitHz ≲ 1e-4，因此激励
// 频率必须「按构造精确可知」，而不是近似设定后凑一个名义值。
//
// 方案：I2S 标准模式，MCLK = 128·Fs 由 160 MHz PLL 整数分频得到。取
// Fs ∈ ExactFsSet（1250000/N 且 N 整除 1250000 → 128·Fs 整除 160 MHz，
// 分频 fraction = 0，Fs 晶体级精确），正弦表 K 个周期 / L 个样本 →
//   actualHz = Fs·K/L      （有理数，按构造回读，非估计）
// 样本表 s = L/K ≥ kMinSamplesPerCycle 保证 DAC 重建质量。
//
// planSineExact() 在 ExactFsSet × L≤maxTableLen 上搜索最优 (Fs,K,L)，
// 使 |actualHz − requestedHz| 最小。典型误差 < 0.1%（远低于 1% 频率门限），
// 且拟合一律使用 actualHz，无相位漂移。
// ============================================================================

#pragma once

#include <stddef.h>
#include <stdint.h>

struct SinePlan {
    uint32_t sampleRateHz;   // 精确 Fs（ExactFsSet 成员）
    uint16_t cyclesK;        // 表内整周期数 K
    uint16_t tableLenL;      // 表长 L（样本数）
    double actualHz;         // = Fs·K/L，精确
    bool ok;
};

// requestedHz ∈ (0, ~156kHz)；tableLen 上限（int16 缓冲预算）
SinePlan planSineExact(double requestedHz, uint16_t maxTableLen = 4096,
                       double minSamplesPerCycle = 8.0);

// ExactFsSet 元素个数（测试/诊断用）
size_t exactFsSetSize();
// 第 i 个精确采样率（Hz），越界返回 0
uint32_t exactFsAt(size_t i);
