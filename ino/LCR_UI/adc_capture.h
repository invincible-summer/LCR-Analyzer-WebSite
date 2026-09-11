// ============================================================================
// adc_capture.h —— ADC 连续采样（DMA）驱动（Arduino/ESP-IDF，仅 ADC1）
// ----------------------------------------------------------------------------
// 生命周期（plan.md §2.2，每次采集完整走一遍）：
//   adc_continuous_new_handle -> adc_continuous_config
//   -> register_event_callbacks -> adc_continuous_start
//   -> 非阻塞 drain/read -> adc_continuous_stop -> adc_continuous_deinit
//
// * 两路在同一 ADC1 pattern 中 = 交错采样：TimedSamples 的 t0 携带确定性
//   通道 skew，正弦拟合按真实时间基准补偿 Δφ = 2πfΔt。
// * ISR 回调（on_conv_done/on_pool_ovf）只置 flag，不做 String/TFT/BLE/
//   拟合/动态分配/日志。DMA overflow → 显式 DmaOverrun，残缺 buffer 不拟合。
// * raw code → mV 走 ESP-IDF curve-fitting 校准（adc_cali_*）；该层只修正
//   MCU ADC 传递，复增益/相位误差由 CalibrationProfile 层负责。
// ============================================================================

#pragma once

#include "measurement_engine.h"
#include "measurement_types.h"

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_continuous.h>

#include <stdint.h>

class AdcCapture : public ICaptureDevice {
public:
    CaptureStatus captureStart(const CaptureRequest& req) override;
    void capturePoll() override;
    bool captureDone() const override { return m_done; }
    CaptureStatus captureStatus() const override;
    TimedSamples captureChannel(CaptureChannel ch) override;
    uint32_t captureMaxPatternRateHz() const override { return 83333; }
    uint16_t captureFullScaleMv() const override { return 3100; }  // 12dB 衰减档
    void captureCancel() override { m_cancelled = true; }
    void captureRelease() override;

private:
    bool ensureCali(int gpioIdx);   // adc_cali 句柄按通道懒创建并缓存

    adc_continuous_handle_t m_handle = nullptr;
    bool m_started = false;
    volatile bool m_overrun = false;
    bool m_cancelled = false;
    bool m_done = false;
    CaptureStatus m_status = CaptureStatus::Ok;

    CaptureChannel m_ch[2] = {CaptureChannel::VoltageDut, CaptureChannel::CurrentSense};
    adc_channel_t m_adcCh[2] = {ADC_CHANNEL_0, ADC_CHANNEL_0};

    // 样本缓冲（mV，post adc_cali）：2 通道 × 上限样本数 = 32 KB 静态
    static constexpr uint32_t kMaxSamples = 8192;
    int16_t m_buf[2][kMaxSamples];
    uint32_t m_count[2] = {0, 0};
    uint32_t m_targetPerCh = 0;
    uint32_t m_patternRateHz = 0;
};

extern AdcCapture adcCapture;
