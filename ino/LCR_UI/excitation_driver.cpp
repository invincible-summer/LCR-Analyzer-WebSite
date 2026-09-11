// ============================================================================
// excitation_driver.cpp —— I2S 激励实现 + 前端 GPIO 控制（Arduino/ESP-IDF）
// ============================================================================

#include "excitation_driver.h"

#include "board_profile.h"
#include "sine_plan.h"

#include <Arduino.h>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>

// PCM5102A @3.3V：满幅差分输出约 2.1 Vrms；半幅正弦 → ~1.05 Vrms。
// 这是 calibrated nominal drive 的名义值（amplitudeCalibrated=false：
// 前端绝对定标未完成，真实幅度以实板标定为准，见 HARDWARE_MAPPING §6）。
static constexpr double kNominalDriveVrms = 1.05;
static constexpr int16_t kDacHalfScale = 16384;   // 0.5 × 32767

ExcitationDriver excitationDriver;
FrontEndGpio frontEndGpio;

// ---------------------------------------------------------------------------
// 驱动状态（单实例）
// ---------------------------------------------------------------------------
static i2s_chan_handle_t s_txHandle = nullptr;
static TaskHandle_t s_task = nullptr;
static SemaphoreHandle_t s_taskDone = nullptr;
static volatile bool s_taskStop = false;
static int16_t s_table[4096];

// 写任务：阻塞式把正弦表灌进 DMA（auto-correct 由驱动 DMA 环节限流），
// stopFlag 置位后 ≤2ms 退出。绝不做日志/TFT/分配。
static void excitationWriterTask(void*)
{
    const size_t bytes = (size_t)excitationDriver.lastTableLen() * sizeof(int16_t);
    while (!s_taskStop) {
        size_t written = 0;
        const esp_err_t e = i2s_channel_write(s_txHandle, s_table, bytes,
                                              &written, 0);
        if (e != ESP_OK || written < bytes)
            vTaskDelay(pdMS_TO_TICKS(2));   // DMA 满：让出一拍
    }
    xSemaphoreGive(s_taskDone);
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
ExcitationStatus ExcitationDriver::excitationBegin(const ExcitationConfig& cfg)
{
    excitationStop();                        // 幂等：先清场

    if (cfg.waveform != Waveform::Sine || !(cfg.requestedHz > 0.0))
        return ExcitationStatus::Invalid;

    m_plan = planSineExact(cfg.requestedHz);
    if (!m_plan.ok) return ExcitationStatus::Invalid;

    // 正弦表：K 个整周期 / L 样本（相位连续循环写入）
    const uint16_t L = m_plan.tableLenL;
    const uint16_t K = m_plan.cyclesK;
    for (uint16_t i = 0; i < L; ++i)
        s_table[i] = (int16_t)lround(kDacHalfScale *
                                     sin(2.0 * M_PI * (double)K * (double)i / (double)L));

    // I2S 标准模式（Philips），mono 16-bit；Fs ∈ 精确分频档 → MCLK 整数分频
    i2s_chan_config_t chanCfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chanCfg.dma_desc_num = 6;
    chanCfg.dma_frame_num = 240;
    if (i2s_new_channel(&chanCfg, &s_txHandle, nullptr) != ESP_OK)
        return ExcitationStatus::DriverError;

    // 逐字段配置（避免宏的 designated-init 在不同 IDF 版本间的顺序差异）
    i2s_std_config_t stdCfg = {};
    stdCfg.clk_cfg.sample_rate_hz = (uint32_t)m_plan.sampleRateHz;
    stdCfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    stdCfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_128;   // 128·Fs | 160 MHz
    stdCfg.clk_cfg.bclk_div = 8;
    stdCfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
    stdCfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
    stdCfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    stdCfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    stdCfg.slot_cfg.ws_width = 16;
    stdCfg.slot_cfg.ws_pol = false;
    stdCfg.slot_cfg.bit_shift = true;
    stdCfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;   // PCM5102 SCK 接地
    stdCfg.gpio_cfg.bclk = (gpio_num_t)kBoard.excitationBck;
    stdCfg.gpio_cfg.ws = (gpio_num_t)kBoard.excitationLrck;
    stdCfg.gpio_cfg.dout = (gpio_num_t)kBoard.excitationData;
    stdCfg.gpio_cfg.din = I2S_GPIO_UNUSED;
    stdCfg.gpio_cfg.invert_flags.mclk_inv = false;
    stdCfg.gpio_cfg.invert_flags.bclk_inv = false;
    stdCfg.gpio_cfg.invert_flags.ws_inv = false;

    if (i2s_channel_init_std_mode(s_txHandle, &stdCfg) != ESP_OK ||
        i2s_channel_enable(s_txHandle) != ESP_OK) {
        i2s_del_channel(s_txHandle);
        s_txHandle = nullptr;
        return ExcitationStatus::DriverError;
    }

    s_taskDone = xSemaphoreCreateBinary();
    s_taskStop = false;
    if (xTaskCreatePinnedToCore(excitationWriterTask, "lcr_exc", 3072, nullptr,
                                2, &s_task, 0) != pdPASS) {
        i2s_channel_disable(s_txHandle);
        i2s_del_channel(s_txHandle);
        s_txHandle = nullptr;
        vSemaphoreDelete(s_taskDone);
        s_taskDone = nullptr;
        return ExcitationStatus::DriverError;
    }

    m_state.actualHz = m_plan.actualHz;      // 精确回读（构造性）
    m_state.actualVrms = kNominalDriveVrms;   // calibrated nominal drive
    m_state.amplitudeCalibrated = false;
    return ExcitationStatus::Ok;
}

void ExcitationDriver::excitationStop()
{
    if (s_task) {
        s_taskStop = true;
        // 写任务最长 2ms 一拍；超时强删（防御性，正常路径不会走到）
        if (xSemaphoreTake(s_taskDone, pdMS_TO_TICKS(30)) == pdFALSE && s_task)
            vTaskDelete(s_task);
        s_task = nullptr;
    }
    if (s_taskDone) { vSemaphoreDelete(s_taskDone); s_taskDone = nullptr; }
    if (s_txHandle) {
        i2s_channel_disable(s_txHandle);
        i2s_del_channel(s_txHandle);
        s_txHandle = nullptr;
    }
    m_state = ExcitationState{};
}

// ---------------------------------------------------------------------------
// 前端使能/量程（BoardProfile 唯一 GPIO 出处；量程 v1 固定 0 档）
// ---------------------------------------------------------------------------
void FrontEndGpio::frontEndEnable(uint8_t rangeBits)
{
    pinMode(kBoard.frontEndEnablePin, OUTPUT);
    digitalWrite(kBoard.frontEndEnablePin, HIGH);
    for (int i = 0; i < 2; ++i) {
        if (kBoard.rangeSelectPins[i] < 0) continue;
        pinMode(kBoard.rangeSelectPins[i], OUTPUT);
        digitalWrite(kBoard.rangeSelectPins[i], (rangeBits >> i) & 0x1);
    }
}

void FrontEndGpio::frontEndDisable()
{
    if (kBoard.frontEndEnablePin >= 0) {
        pinMode(kBoard.frontEndEnablePin, OUTPUT);
        digitalWrite(kBoard.frontEndEnablePin, LOW);   // 安全态：前端断电
    }
}
