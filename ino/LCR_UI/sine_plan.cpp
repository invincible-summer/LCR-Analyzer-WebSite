// sine_plan.cpp —— 精确有理频率规划实现（host 可编译，单测覆盖）
#include "sine_plan.h"

#include <math.h>

// Fs = 1250000/N（N 整除 1250000）→ 128·Fs = 160e6/N 整除 160 MHz。
// 只保留 PCM5102 合法 LRCK 范围（≥8 kHz）内的档位（降序，优先高 Fs）。
static const uint32_t kExactFs[] = {
    156250, 125000, 78125, 62500, 50000, 31250, 25000, 15625, 12500, 10000,
};
static const size_t kExactFsCount = sizeof(kExactFs) / sizeof(kExactFs[0]);

size_t exactFsSetSize() { return kExactFsCount; }
uint32_t exactFsAt(size_t i)
{
    return i < kExactFsCount ? kExactFs[i] : 0;
}

SinePlan planSineExact(double requestedHz, uint16_t maxTableLen,
                       double minSamplesPerCycle)
{
    SinePlan plan{};
    plan.ok = false;
    if (!(requestedHz > 0.0) || maxTableLen < 2 || !(minSamplesPerCycle >= 1.0))
        return plan;

    double bestErr = INFINITY;
    for (size_t fi = 0; fi < kExactFsCount; ++fi) {
        const double Fs = (double)kExactFs[fi];
        const double r = requestedHz / Fs;          // K/L 目标
        if (r <= 0.0) continue;

        // 直接搜 L：K = round(r·L)。L 小步进（低频大 s）+ 大步进（高频小 s）
        // 两段扫描都覆盖 1..maxTableLen，成本 ≤ 10×4096 次廉价算术。
        for (uint32_t L = 1; L <= maxTableLen; ++L) {
            const double kIdeal = r * (double)L;
            const uint32_t K = (uint32_t)(kIdeal + 0.5);
            if (K < 1) continue;
            const double s = (double)L / (double)K;
            if (s < minSamplesPerCycle) continue;
            const double f = Fs * (double)K / (double)L;
            const double err = fabs(f - requestedHz);
            if (err < bestErr) {
                bestErr = err;
                plan.sampleRateHz = kExactFs[fi];
                plan.cyclesK = (uint16_t)K;
                plan.tableLenL = (uint16_t)L;
                plan.actualHz = f;
                plan.ok = true;
                if (err == 0.0) return plan;         // 精确命中
            }
        }
    }
    return plan;
}
