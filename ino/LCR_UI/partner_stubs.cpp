// ============================================================================
// partner_stubs.cpp —— 队友接口的 weak 参考实现（可独立运行的合成数据源）
// ----------------------------------------------------------------------------
// 作用：
//   1. 让整个工程在队友的真实实现到位之前就能编译、烧录、演示：
//      界面、正弦拟合、扫频曲线、蓝牙通路全部可跑通（数据是合成的）；
//   2. 给出上述函数「正确消费 amplitudeRatio / phaseDiff」的参考示范。
//
// 对接：队友把真实实现的 .cpp 放进本目录即可——同名函数的强定义会在链接期
// 自动覆盖这里的 weak 定义，本文件无需删除、无需任何改动。
//
// 模拟的「被测对象」（演示用）：
//   * 单端口：并联 RC（R=1kΩ, C=1µF）—— 低频呈阻性，高频呈容性；
//   * 双端口：一阶 RC 低通（R=1kΩ, C=0.1µF, fc≈1591Hz）。
// ============================================================================

#include "partner_api.h"

#include <Arduino.h>
#include <math.h>

// ---------------------------------------------------------------------------
// 信号发生器：仅记录状态并打印（无真实 DAC/DDS 硬件）
// ---------------------------------------------------------------------------
static double s_genFreq = 0;
static WaveType s_genType = Sine;

__attribute__((weak)) double generateWave(int freq, WaveType waveType,
                                          double dutyCycle)
{
    Serial.printf("[stub] generateWave f=%d type=%d duty=%.2f\n",
                  freq, (int)waveType, dutyCycle);
    if (freq < 0) return 0;                    // 非法频率 -> 失败
    s_genFreq = freq;
    s_genType = waveType;
    return freq;                               // 模拟完美频率源
}

// ---------------------------------------------------------------------------
// 采样：按模拟 DUT 合成两路 12bit ADC 码（含直流偏置与少量噪声）
// 缓冲区为 static，下一次调用覆盖——与「队友接口」的有效期约定一致。
// ---------------------------------------------------------------------------
// 模拟的「被测对象」（演示用，数值经过设计使两路码值都落在 ADC 量程内）：
//   * 单端口：并联 RC（R=1kΩ, C=1µF）—— 阻抗 994Ω@10Hz -> 15.9Ω@10kHz；
//     电流通道固定 800 码、电压通道随 |Z| 缩放（模拟前端自动换挡的思想）。
//     增益取 tz=1000, vg=cg=2，使 |Z| = ratio·tz·cg/vg = ratio·1000 恰为真值。
//   * 双端口：一阶 RC 低通（R=1kΩ, C=0.1µF, fc≈1591Hz）。
static short s_out[1024];
static short s_in[1024];

__attribute__((weak)) AdcSampleResult measureImpedanceAtFreq(int freq,
                                                             bool isOnePort)
{
    AdcSampleResult r;
    r.outBuffer = s_out;
    r.inBuffer  = s_in;
    r.sampleLength = 1024;
    r.transimpedanceGain = 1000;    // 与 vg/cg 配合：|Z|显示值 = ratio × 1000
    r.voltageGain = 2;
    r.currentGain = 2;
    r.actualFreq = freq;            // 模拟理想频率源（真实实现会有偏差）
    r.samplingFreq = freq * 64.0;   // 每周期 64 点

    const double w = 2.0 * M_PI * freq;
    const double dt = 1.0 / r.samplingFreq;

    double ampExc = 0, ampRes = 0;               // 两路幅度（ADC 码）
    double phaseRes = 0;                         // 响应相对激励的相位差
    if (isOnePort) {
        // 并联 RC: Y = 1/R + jwC -> Z 幅度/相位
        const double R = 1000.0, C = 1e-6;
        const double gR = 1.0 / R, gC = w * C;
        const double zMag = 1.0 / hypot(gR, gC);            // |Z|
        const double zPh  = atan2(-gC, gR);                 // ∠Z（容性为负）
        phaseRes = -zPh;                                    // 电流相位 = −∠Z（超前为正）
        ampRes = 800.0;                                     // 电流通道固定幅度
        ampExc = 0.8 * zMag;                                // 电压通道 ∝ |Z|
    } else {
        const double fc = 1.0 / (2.0 * M_PI * 1000.0 * 0.1e-6);   // ≈1591 Hz
        const double x = freq / fc;
        const double g = 1.0 / sqrt(1.0 + x * x);           // |H|
        ampExc = 1000.0;
        ampRes = 1000.0 * g;
        phaseRes = -atan(x);                                // 输出滞后（负）
    }

    // 固定种子的伪随机噪声（同频点可复现，便于算法验证）
    uint32_t seed = (uint32_t)freq * 2654435761u + (isOnePort ? 1 : 2);
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u;
                       return (double)((int32_t)(seed >> 8) % 1000) / 1000.0 - 0.5; };

    for (int k = 0; k < r.sampleLength; ++k) {
        const double ph = w * k * dt;
        s_out[k] = (short)lround(2048 + ampExc * sin(ph) + rnd() * 4.0);
        s_in[k]  = (short)lround(2048 + ampRes * sin(ph + phaseRes) + rnd() * 4.0);
    }
    return r;
}

// ---------------------------------------------------------------------------
// 复阻抗：示范如何按 partner_api.h 的约定消费 ratio / phaseDiff
//   码值换算假设（真实实现以模拟前端为准）：
//     V = out_code * (VREF/4095) / voltageGain
//     I = in_code  * (VREF/4095) / (transimpedanceGain * currentGain)
//   => |Z| = ratio * transimpedanceGain * currentGain / voltageGain
//      ∠Z  = -phaseDiff（phaseDiff = φI − φV = −∠Z）
// ---------------------------------------------------------------------------
__attribute__((weak)) ImpedanceCalcResult calculateImpedance(ImpedanceCalcInput in)
{
    ImpedanceCalcResult r;
    const double kv = in.voltageGain > 0 ? in.voltageGain : 1;
    const double ki = (double)(in.transimpedanceGain > 0 ? in.transimpedanceGain : 1)
                    * (in.currentGain > 0 ? in.currentGain : 1);
    const double zMag = in.amplitudeRatio * ki / kv;
    const double zPh  = -in.phaseDiff * M_PI / 180.0;

    r.realPart = zMag * cos(zPh);
    r.imagPart = zMag * sin(zPh);
    r.isCapacitive = (r.imagPart < 0);
    const double w = 2.0 * M_PI * in.actualFreq;
    if (r.isCapacitive && r.imagPart != 0) {
        r.reactanceValue = -1.0 / (w * r.imagPart);          // 等效电容 (F)
        // 串联 -> 并联电阻变换：Rp = (R^2 + X^2) / R
        r.equivalentResistance = (r.realPart != 0)
            ? (r.realPart * r.realPart + r.imagPart * r.imagPart) / r.realPart
            : 1e12;
    } else {
        r.reactanceValue = r.imagPart / w;                    // 等效电感 (H)
        r.equivalentResistance = r.realPart;                  // 串联电阻
    }
    return r;
}

// ---------------------------------------------------------------------------
// 增益/相位：示范消费约定——双端口时 amplitudeRatio = Vin/Vout = 1/|H|
//   （假设两通道电压增益一致，增益比 cancels；否则应乘 in/out 通道增益比）
// ---------------------------------------------------------------------------
__attribute__((weak)) GainPhaseResult calculateGainPhase(ImpedanceCalcInput in)
{
    GainPhaseResult r;
    r.gainDb = (in.amplitudeRatio > 0) ? -20.0 * log10(in.amplitudeRatio) : -999.0;
    r.phaseDiff = in.phaseDiff;      // 约定已满足：输出超前输入为正
    return r;
}
