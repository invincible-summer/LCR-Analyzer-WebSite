// ============================================================================
// excitation_driver.h —— 激励源驱动（I2S 标准模式 → 外部 PCM5102A DAC）
// ----------------------------------------------------------------------------
// ESP32-S3 无片上 DAC。激励路径（BoardProfile §3.4）：I2S TX（BCK/LRCK/DATA
// 三线，MCLK 未用——PCM5102 SCK 接地内部 PLL）。
//
// 频率：sine_plan 精确有理规划（actualHz = Fs·K/L，晶体级精确）。
// 幅度：固定 DAC 半幅 —— plan.md §2.3「硬件只有固定幅度」情形：
//   UI 只显示 calibrated nominal drive，不提供虚假的电压设定。
// 输出：专用低优先级写任务阻塞在 i2s_channel_write（DMA 队列自然限流），
// loop() 与测量状态机从不等待。
// ============================================================================

#pragma once

#include "measurement_engine.h"
#include "measurement_types.h"
#include "sine_plan.h"

#include <stdint.h>

// 前端使能/量程 GPIO（BoardProfile 提供，safe-off 落 0/失能）
class FrontEndGpio : public IFrontEnd {
public:
    void frontEndEnable(uint8_t rangeBits) override;
    void frontEndDisable() override;
};

class ExcitationDriver : public IExcitationSource {
public:
    ExcitationStatus excitationBegin(const ExcitationConfig& cfg) override;
    ExcitationState excitationState() const override { return m_state; }
    void excitationStop() override;

    // 诊断：最近一次规划的 (Fs,K,L)
    uint32_t lastSampleRateHz() const { return m_plan.sampleRateHz; }
    uint16_t lastTableLen() const { return m_plan.tableLenL; }

private:
    ExcitationState m_state{};
    SinePlan m_plan{};
};

extern ExcitationDriver excitationDriver;
extern FrontEndGpio frontEndGpio;
