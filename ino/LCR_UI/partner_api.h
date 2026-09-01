// ============================================================================
// partner_api.h —— 队友（硬件/模拟前端/激励源侧）提供的接口 · 权威声明
// ----------------------------------------------------------------------------
// 本文件是对外协作的核心契约，语义一旦确定，双方都不得单方面更改；
// 有任何变更请在 README.md「接口契约」一节同步更新。
//
// 对接方式：
//   * 本工程自带 partner_stubs.cpp，为下列函数提供 weak 参考实现，
//     因此整个工程可以独立编译、独立跑通界面与算法（用合成数据演示）。
//   * 队友把真实实现的 .cpp 放进本 sketch 目录即可——同名函数的强定义
//     会自动覆盖 weak 参考实现，无需删除任何文件。
//
// ★★ 幅度比 / 相位差的统一定义（本工程 calculate 出来后填进
//     ImpedanceCalcInput 的两个字段，务必按此理解）：
//
//   amplitudeRatio = A(outBuffer) / A(inBuffer)
//       —— A(x) 为对 buffer x 做三参数正弦拟合得到的正弦幅度（原始 ADC 码单位）。
//       单端口：= V码 / I码（跨阻/放大倍数不折算，由 calculateImpedance 内部用
//               transimpedanceGain / voltageGain / currentGain 折算物理量）。
//       双端口：= Vin码 / Vout码 = 1/|H|（增益为其倒数，calculateGainPhase 内取
//               gainDb = -20·log10(amplitudeRatio × 通道增益比)）。
//
//   phaseDiff = φ(inBuffer) − φ(outBuffer)   单位：度，已折算到 (−180, 180]
//       —— φ 为正弦拟合相位（模型 a·sin + b·cos 的 atan2(b,a)）。
//       单端口：= φ_I − φ_V，即「电流超前电压为正」。
//       双端口：= φ_Vout − φ_Vin，即「输出超前输入为正」。
//
//   （两种模式下公式完全一致：一律「响应(inBuffer) 相位减 激励(outBuffer) 相位」。）
// ============================================================================

#pragma once

// --- 信号发生器 -------------------------------------------------------------
// 产生指定波形。返回实际产生的频率（Hz），0 表示失败。
// 暂停输出：freq 传 0。
enum WaveType { Sine, Square, Triangle };

extern double generateWave(int freq, WaveType waveType, double dutyCycle);
// 参数：freq 频率 Hz（10~10000，0 = 停止输出）；waveType 波形；
//       dutyCycle 占空比 0.0~1.0（正弦无效）。返回实际频率 Hz，0 = 失败。

// --- 单频点采样 -------------------------------------------------------------
// 在指定频率上激励并同步采样两路 ADC。
// isOnePort = true  : 单端口（测复阻抗）；false : 双端口（测输入输出关系）。
// 返回的缓冲区指针至少在「下一次调用本函数之前」保持有效（调用方不得 free）。
struct AdcSampleResult {
    int sampleLength;             // 采样长度
    short* outBuffer;             // 待测元件两端电压 或 待测网络输入端电压（ADC 码 0-4095）
    short* inBuffer;              // 待测元件电流     或 待测网络输出端电压（ADC 码 0-4095）
    int transimpedanceGain;       // 若 inBuffer 为电流信号，电流→电压的跨阻放大倍数
    int voltageGain;              // 外置电压放大倍数
    int currentGain;              // 外置电流放大倍数
    double actualFreq;            // 实际产生的频率（多数频率无法准确产生）
    double samplingFreq;          // 采样频率 = 同一 buffer 相邻两次采样时间差的倒数
};

extern AdcSampleResult measureImpedanceAtFreq(int freq, bool isOnePort);

// --- 由幅度比 + 相位差求复阻抗 / 增益相位 ------------------------------------
struct ImpedanceCalcInput {
    double amplitudeRatio;        // 幅度比（定义见文件顶部 ★★）
    double phaseDiff;             // 相位差（度，定义见文件顶部 ★★）
    int transimpedanceGain;       // 同 AdcSampleResult
    int voltageGain;              // 同上
    int currentGain;              // 同上
    double actualFreq;            // 同上
};

struct ImpedanceCalcResult {
    double realPart;              // 复阻抗实部
    double imagPart;              // 复阻抗虚部
    bool isCapacitive;            // true = 容性，false = 感性
    double reactanceValue;        // 等效电容(F)或电感(H)（单频点计算结果）
    double equivalentResistance;  // 等效电阻：电感按串联、电容按并联
};

struct GainPhaseResult {
    double gainDb;                // 增益 (dB)
    double phaseDiff;             // 相位差 (度)
};

extern ImpedanceCalcResult calculateImpedance(ImpedanceCalcInput input);  // 单端口
extern GainPhaseResult calculateGainPhase(ImpedanceCalcInput input);      // 双端口
