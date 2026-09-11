// ============================================================================
// adc_capture.cpp —— ADC continuous（DMA）实现：采集 → 解交错 → raw→mV
// ----------------------------------------------------------------------------
// ESP32-S3 约束落地：
//   * 只用 ADC1（ADC2 的 DMA continuous 不做稳定性承诺；Wi-Fi 互斥）。
//   * pattern 总速率 ∈ [611, 83333] Hz（SOC_ADC_SAMPLE_FREQ_THRES）；
//     2 通道交错 → 每通道 ≤ ~41.7 ksps（质量频段见 HARDWARE_MAPPING §3.1）。
//   * 削顶判据用 12 dB 衰减档的满量程（~3.1 V，非 3.3 V）。
// ============================================================================

#include "adc_capture.h"

#include "board_profile.h"

#include <Arduino.h>
#include <esp_adc/adc_cali_scheme.h>
#include <string.h>

AdcCapture adcCapture;

// adc_cali 句柄缓存：按 ADC1 通道号（0..9）索引——单/双端口切换时
// pattern 槽位对应不同物理通道，必须按通道缓存而不是按槽位。
static adc_cali_handle_t s_caliByChan[10] = {};

// ISR 安全回调：只置 flag（无分配/日志/拟合/串口 —— plan.md §2.2）
static volatile bool s_overrunISR = false;
static bool IRAM_ATTR onPoolOverflowCb(adc_continuous_handle_t,
                                       const adc_continuous_evt_data_t*, void*)
{
    s_overrunISR = true;
    return false;
}

// BoardProfile 通道 GPIO 表（索引顺序 = CaptureChannel 枚举）
static int channelGpio(CaptureChannel ch)
{
    switch (ch) {
    case CaptureChannel::VoltageDut:     return kBoard.adcVoltageGpio.gpio;
    case CaptureChannel::CurrentSense:   return kBoard.adcCurrentGpio.gpio;
    case CaptureChannel::Port2Input:     return kBoard.adcPort2InputGpio.gpio;
    case CaptureChannel::Port2Output:    return kBoard.adcPort2OutputGpio.gpio;
    }
    return -1;
}

bool AdcCapture::ensureCali(int)
{
    return true;    // cali 句柄在 captureStart 内按本次使用的通道建立并缓存
}

// ---------------------------------------------------------------------------
CaptureStatus AdcCapture::captureStart(const CaptureRequest& req)
{
    m_status = CaptureStatus::Ok;
    m_overrun = false;
    s_overrunISR = false;
    m_cancelled = false;
    m_done = false;
    m_count[0] = m_count[1] = 0;

    if (m_started) { captureRelease(); }
    if (req.channelCount != 2 || !(req.excitationHz > 0.0) ||
        !(req.targetSampleRateHz > 0) || req.captureCycles == 0)
        return m_status = CaptureStatus::InvalidRequest;

    // 通道 → GPIO → ADC1 通道号（BoardProfile + 数据手册映射）
    for (int i = 0; i < 2; ++i) {
        m_ch[i] = req.channels[i];
        const int gpio = channelGpio(m_ch[i]);
        AdcUnit unit; uint8_t chIdx;
        if (gpio < 0 || !adcLookup(gpio, unit, chIdx))
            return m_status = CaptureStatus::UnsupportedChannel;
        m_adcCh[i] = (adc_channel_t)chIdx;
    }

    // 每通道目标数与 pattern 速率
    uint32_t patternRate = req.targetSampleRateHz * 2;
    if (patternRate < 611) patternRate = 611;
    if (patternRate > captureMaxPatternRateHz()) patternRate = captureMaxPatternRateHz();
    m_patternRateHz = patternRate;
    uint32_t want = (uint32_t)((double)(patternRate / 2) * (double)req.captureCycles /
                               req.excitationHz) + 1;
    if (want > kMaxSamples) want = kMaxSamples;
    if (want < req.minSamplesPerChannel) want = req.minSamplesPerChannel;
    if (want > kMaxSamples) want = kMaxSamples;
    m_targetPerCh = want;

    // ---- adc_cali（curve fitting，每通道；只修正 MCU ADC 传递）------------
    for (int i = 0; i < 2; ++i) {
        const int chIdx = (int)m_adcCh[i];
        if (chIdx < 0 || chIdx > 9) continue;
        if (s_caliByChan[chIdx]) continue;
        adc_cali_curve_fitting_config_t cfg = {};
        cfg.unit_id = ADC_UNIT_1;
        cfg.chan = m_adcCh[i];
        cfg.atten = ADC_ATTEN_DB_12;
        if (adc_cali_create_scheme_curve_fitting(&cfg, &s_caliByChan[chIdx]) != ESP_OK)
            return m_status = CaptureStatus::DriverError;
    }

    // ---- 生命周期：new_handle -> config -> callbacks -> start --------------
    adc_continuous_handle_cfg_t hcfg = {};
    hcfg.conv_frame_size = 512;         // SOC_ADC_DIGI_DATA_BYTES_PER_CONV 的倍数
    if (adc_continuous_new_handle(&hcfg, &m_handle) != ESP_OK)
        return m_status = CaptureStatus::DriverError;

    adc_digi_pattern_config_t pat[2] = {};
    for (int i = 0; i < 2; ++i) {
        pat[i].atten = ADC_ATTEN_DB_12;
        pat[i].bit_width = 12;
        pat[i].channel = m_adcCh[i];
    }
    adc_continuous_config_t cfg = {};
    cfg.pattern_num = 2;
    cfg.adc_pattern = pat;
    cfg.sample_freq_hz = patternRate;
    cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE1;
    if (adc_continuous_config(m_handle, &cfg) != ESP_OK) {
        captureRelease();
        return m_status = CaptureStatus::DriverError;
    }

    adc_continuous_evt_cbs_t cbs = {};
    cbs.on_pool_ovf = onPoolOverflowCb;
    if (adc_continuous_register_event_callbacks(m_handle, &cbs, nullptr) != ESP_OK) {
        captureRelease();
        return m_status = CaptureStatus::DriverError;
    }

    if (adc_continuous_start(m_handle) != ESP_OK) {
        captureRelease();
        return m_status = CaptureStatus::DriverError;
    }
    m_started = true;
    return m_status = CaptureStatus::Ok;
}

// ---------------------------------------------------------------------------
void AdcCapture::capturePoll()
{
    if (!m_started || m_done || m_cancelled) return;
    if (s_overrunISR) { m_overrun = true; m_done = true; return; }

    // 非阻塞 drain：一次最多处理一帧（poll 上界明确）
    static uint8_t raw[512];
    static adc_continuous_data_t parsed[256];
    uint32_t len = 0;
    const esp_err_t e = adc_continuous_read(m_handle, raw, sizeof(raw), &len, 0);
    if (e == ESP_OK && len > 0) {
        uint32_t nParsed = 0;
        if (adc_continuous_parse_data(m_handle, raw, len, parsed, &nParsed) == ESP_OK) {
            for (uint32_t i = 0; i < nParsed; ++i) {
                const adc_channel_t c = parsed[i].channel;
                int slot = -1;
                if (c == m_adcCh[0]) slot = 0;
                else if (c == m_adcCh[1]) slot = 1;
                if (slot < 0 || !parsed[i].valid) continue;
                if (m_count[slot] >= m_targetPerCh) continue;   // 后到的丢弃
                const adc_cali_handle_t cali = (c == m_adcCh[0]) ? s_caliByChan[(int)m_adcCh[0]]
                                                                 : s_caliByChan[(int)m_adcCh[1]];
                int mv = 0;
                if (adc_cali_raw_to_voltage(cali, (int)parsed[i].raw_data, &mv) != ESP_OK)
                    continue;
                m_buf[slot][m_count[slot]++] = (int16_t)mv;
            }
        }
    }
    if (m_count[0] >= m_targetPerCh && m_count[1] >= m_targetPerCh)
        m_done = true;
}

CaptureStatus AdcCapture::captureStatus() const
{
    if (m_overrun || s_overrunISR) return CaptureStatus::DmaOverrun;
    if (m_cancelled) return CaptureStatus::Ok;      // 取消由引擎语义处理
    return m_status;
}

TimedSamples AdcCapture::captureChannel(CaptureChannel ch)
{
    TimedSamples ts{};
    if (ch == m_ch[0]) {
        ts.samples = m_buf[0]; ts.count = m_count[0]; ts.t0 = 0.0;
    } else if (ch == m_ch[1]) {
        ts.samples = m_buf[1]; ts.count = m_count[1];
        ts.t0 = 1.0 / (double)m_patternRateHz;   // 交错 skew：B 落后 A 半个 pattern
    }
    ts.dt = 2.0 / (double)m_patternRateHz;       // 每通道有效间隔
    return ts;
}

void AdcCapture::captureRelease()
{
    if (m_handle) {
        if (m_started) adc_continuous_stop(m_handle);
        adc_continuous_deinit(m_handle);
        m_handle = nullptr;
    }
    m_started = false;
    m_done = false;
    m_count[0] = m_count[1] = 0;
}
