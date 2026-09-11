#pragma once
/**
 * =====================================================================
 * lcr_adc.h — ADC 连续采集硬件层（ESP32-S3 ADC1 双通道，硬件 v2）
 * =====================================================================
 * 职责：PSRAM 双缓冲、ADC 连续驱动初始化、阻塞式采集、名义采样率。
 * 通道 A = GPIO2 (ADC1_CH1) 电压；通道 B = GPIO1 (ADC1_CH0) 电流。
 * 约定：本工程为单 .ino 编译单元，.h 内函数用 static 实现。
 *
 * ★★★ 接口函数定义 ★★★
 *   bool     lcr_adc_init(void)
 *       初始化 PSRAM 双缓冲 + ADC 连续驱动（pattern/事件回调）。
 *       返回 false = PSRAM 缺失或分配失败（内部已打印 FATAL）。
 *   uint32_t lcr_adc_capture(uint32_t duration_ms)
 *       阻塞式采集 duration_ms 毫秒，数据写入内部双缓冲。
 *       开始前/结束后自动清空驱动残留池。返回实际耗时 ms。
 *   uint16_t *lcr_adc_buf_a(void)         通道 A（电压）原始码缓冲指针
 *   uint16_t *lcr_adc_buf_b(void)         通道 B（电流）原始码缓冲指针
 *   uint32_t  lcr_adc_count_a(void)       本次采集 A 有效样本数
 *   uint32_t  lcr_adc_count_b(void)       本次采集 B 有效样本数
 *   uint32_t  lcr_adc_invalid(void)       无效/未知通道样本计数
 *   bool      lcr_adc_pool_ovf(void)      驱动内部池溢出标志
 *   uint32_t  lcr_adc_capacity(void)      每通道缓冲容量（样本数）
 *   double    lcr_adc_fs_per_channel(void) 每通道名义采样率 Hz（含量化取整）
 *   float     lcr_adc_raw_to_volt(uint16_t raw) 原始码 → 电压(V)
 *   void      lcr_adc_print_nominal_rate(void)  打印名义采样率诊断块
 *
 * ★ 缓冲容量按最长单次采集 LCR_ADC_CAPTURE_MS_MAX 分配（扫频低频端 2s）★
 */

#include <Arduino.h>
#include "esp_adc/adc_continuous.h"
#include "esp_heap_caps.h"
#include "soc/soc_caps.h"

// ---- 采样配置 ----
#define LCR_ADC_UNIT_ID        ADC_UNIT_1
#define LCR_ADC_ATTEN          ADC_ATTEN_DB_12
#define LCR_ADC_BIT_WIDTH      SOC_ADC_DIGI_MAX_BITWIDTH
#define LCR_ADC_CH_A           ADC_CHANNEL_1          // GPIO2 电压
#define LCR_ADC_CH_B           ADC_CHANNEL_0          // GPIO1 电流
#define LCR_ADC_CH_NUM         2
#define LCR_ADC_SAMPLE_FREQ_HZ SOC_ADC_SAMPLE_FREQ_THRES_HIGH
#define LCR_ADC_FRAME_SIZE     256
#define LCR_ADC_STORE_BUF_SIZE 16384
#define LCR_ADC_CAPTURE_MS_MAX 2000UL   // 允许的最长单次采集（决定缓冲容量）

// 每通道容量（按最长采集时间 + 5% 余量）
#define LCR_ADC_CAPACITY ((uint32_t)(LCR_ADC_SAMPLE_FREQ_HZ / LCR_ADC_CH_NUM) \
                            * LCR_ADC_CAPTURE_MS_MAX / 1000 * 105 / 100 + 16)

// ---- 模块内状态 ----
static adc_continuous_handle_t s_adc_handle = nullptr;
static TaskHandle_t     s_adc_task          = nullptr;
static uint16_t *s_buf_a = nullptr;
static uint16_t *s_buf_b = nullptr;
static volatile uint32_t s_cnt_a   = 0;
static volatile uint32_t s_cnt_b   = 0;
static volatile uint32_t s_invalid = 0;
static volatile bool     s_ovf     = false;
static uint32_t          s_start_ms = 0;
static volatile uint32_t s_cap_ms   = 0;

// ---- ISR 回调 ----
static bool IRAM_ATTR s_adc_conv_done_cb(adc_continuous_handle_t,
                                         const adc_continuous_evt_data_t *, void *)
{
    BaseType_t mustYield = pdFALSE;
    vTaskNotifyGiveFromISR(s_adc_task, &mustYield);
    return (mustYield == pdTRUE);
}

static bool IRAM_ATTR s_adc_pool_ovf_cb(adc_continuous_handle_t,
                                        const adc_continuous_evt_data_t *, void *)
{
    s_ovf = true;
    return false;
}

#define LCR_ADC_SAMPLING_ACTIVE() ((uint32_t)(millis() - s_start_ms) < s_cap_ms)

// ---- PSRAM 分配 ----
static uint16_t *s_adc_alloc_psram(uint32_t count)
{
    size_t bytes = (size_t)count * sizeof(uint16_t);
    uint16_t *p = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!p) {
        Serial.printf("FATAL: PSRAM alloc %u bytes failed (free: %u)\n",
                      (unsigned)bytes, (unsigned)ESP.getFreePsram());
    }
    return p;
}

// ---- 清空驱动内部残留池（驱动运行中调用）----
static void s_adc_drain_pool()
{
    uint8_t  fb[LCR_ADC_FRAME_SIZE];
    uint32_t rn = 0;
    while (adc_continuous_read(s_adc_handle, fb, LCR_ADC_FRAME_SIZE, &rn, 0) == ESP_OK) {
        // 丢弃
    }
}

static bool lcr_adc_init()
{
    if (!psramFound()) {
        Serial.println("FATAL: PSRAM not found! Check Tools->PSRAM->OPI PSRAM");
        return false;
    }
    s_buf_a = s_adc_alloc_psram(LCR_ADC_CAPACITY);
    s_buf_b = s_adc_alloc_psram(LCR_ADC_CAPACITY);
    if (!s_buf_a || !s_buf_b) return false;
    Serial.printf("# PSRAM ok, %u samples x2 ch\n", (unsigned)LCR_ADC_CAPACITY);

    s_adc_task = xTaskGetCurrentTaskHandle();

    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = LCR_ADC_STORE_BUF_SIZE,
        .conv_frame_size    = LCR_ADC_FRAME_SIZE,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &s_adc_handle));

    adc_continuous_config_t dig_cfg = {
        .pattern_num    = LCR_ADC_CH_NUM,
        .sample_freq_hz = LCR_ADC_SAMPLE_FREQ_HZ,
        .conv_mode      = ADC_CONV_SINGLE_UNIT_1,
    };
    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {};
    adc_pattern[0].atten = LCR_ADC_ATTEN;  adc_pattern[0].channel = LCR_ADC_CH_A;
    adc_pattern[0].unit  = LCR_ADC_UNIT_ID; adc_pattern[0].bit_width = LCR_ADC_BIT_WIDTH;
    adc_pattern[1].atten = LCR_ADC_ATTEN;  adc_pattern[1].channel = LCR_ADC_CH_B;
    adc_pattern[1].unit  = LCR_ADC_UNIT_ID; adc_pattern[1].bit_width = LCR_ADC_BIT_WIDTH;
    dig_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_continuous_config(s_adc_handle, &dig_cfg));

    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = s_adc_conv_done_cb,
        .on_pool_ovf  = s_adc_pool_ovf_cb,
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(s_adc_handle, &cbs, nullptr));
    return true;
}

static uint32_t lcr_adc_capture(uint32_t duration_ms)
{
    s_cnt_a = s_cnt_b = s_invalid = 0;
    s_ovf = false;
    s_cap_ms = duration_ms;

    ESP_ERROR_CHECK(adc_continuous_start(s_adc_handle));
    s_start_ms = millis();

    uint8_t  frame_buf[LCR_ADC_FRAME_SIZE];
    uint32_t ret_num = 0;

    while (LCR_ADC_SAMPLING_ACTIVE()) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (LCR_ADC_SAMPLING_ACTIVE()) {
            if (adc_continuous_read(s_adc_handle, frame_buf,
                                    LCR_ADC_FRAME_SIZE, &ret_num, 0) != ESP_OK) break;

            adc_continuous_data_t parsed[LCR_ADC_FRAME_SIZE / SOC_ADC_DIGI_RESULT_BYTES];
            uint32_t parsed_n = 0;
            if (adc_continuous_parse_data(s_adc_handle, frame_buf, ret_num,
                                          parsed, &parsed_n) == ESP_OK) {
                for (uint32_t i = 0; i < parsed_n; i++) {
                    if (!parsed[i].valid) { s_invalid++; continue; }
                    if (parsed[i].channel == (LCR_ADC_CH_A & 0x7)) {
                        if (s_cnt_a < LCR_ADC_CAPACITY)
                            s_buf_a[s_cnt_a++] = (uint16_t)parsed[i].raw_data;
                    } else if (parsed[i].channel == (LCR_ADC_CH_B & 0x7)) {
                        if (s_cnt_b < LCR_ADC_CAPACITY)
                            s_buf_b[s_cnt_b++] = (uint16_t)parsed[i].raw_data;
                    } else {
                        s_invalid++;
                    }
                }
            }
        }
    }

    s_adc_drain_pool();
    ESP_ERROR_CHECK(adc_continuous_stop(s_adc_handle));

    return millis() - s_start_ms;
}

static uint16_t *lcr_adc_buf_a()        { return s_buf_a; }
static uint16_t *lcr_adc_buf_b()        { return s_buf_b; }
static uint32_t  lcr_adc_count_a()      { return s_cnt_a; }
static uint32_t  lcr_adc_count_b()      { return s_cnt_b; }
static uint32_t  lcr_adc_invalid()      { return s_invalid; }
static bool      lcr_adc_pool_ovf()     { return s_ovf; }
static uint32_t  lcr_adc_capacity()     { return LCR_ADC_CAPACITY; }

static double lcr_adc_fs_per_channel()
{
    const uint32_t F_DIGI = 5000000;
    uint32_t interval = F_DIGI / (2u * (uint32_t)LCR_ADC_SAMPLE_FREQ_HZ);
    if (interval < 2)    interval = 2;
    if (interval > 4095) interval = 4095;
    return (double)F_DIGI / ((double)interval * 2.0 * LCR_ADC_CH_NUM);
}

static inline float lcr_adc_raw_to_volt(uint16_t raw)
{
    return (float)raw * 3.3f / (float)((1 << LCR_ADC_BIT_WIDTH) - 1);
}

static void lcr_adc_print_nominal_rate()
{
    const uint32_t F_DIGI = 5000000;
    const uint32_t I_MIN = 2, I_MAX = 4095;
    const uint32_t fs_requested = LCR_ADC_SAMPLE_FREQ_HZ;

    Serial.println("# ---- nominal sample rate ----");
    Serial.printf("# requested : %u Hz (aggregate)\n", (unsigned)fs_requested);

    if (fs_requested > SOC_ADC_SAMPLE_FREQ_THRES_HIGH ||
        fs_requested < SOC_ADC_SAMPLE_FREQ_THRES_LOW) {
        Serial.printf("# WARNING: %u Hz out of driver range, rejected!\n",
                      (unsigned)fs_requested);
        return;
    }

    uint32_t interval = F_DIGI / (2 * fs_requested);
    if (interval < I_MIN) interval = I_MIN;
    if (interval > I_MAX) interval = I_MAX;

    float fs_actual = (float)F_DIGI / (2.0f * (float)interval);
    float err_ppm   = (fs_actual - (float)fs_requested) / (float)fs_requested * 1e6f;

    Serial.printf("# interval  : %u   (Fs = 5MHz / interval / 2)\n", (unsigned)interval);
    Serial.printf("# actual    : %.2f Hz aggregate, %.2f Hz per-channel\n",
                  fs_actual, fs_actual / LCR_ADC_CH_NUM);
    Serial.printf("# quant_err : %.1f ppm\n", err_ppm);
}
