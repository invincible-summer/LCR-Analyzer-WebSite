#ifndef FREQ_CALC_H
#define FREQ_CALC_H
#include <Arduino.h>
#include "sinwave.h" // ★ 必须在最前：几何参数(MAX_CHAIN_LEN等)的唯一来源在这里
#include <math.h>
#include "esp_heap_caps.h"
#include "lcr_diag.h" // ★ 新增：诊断开关

// ==========================================
// 1. 宏定义：系统参数
// ==========================================
#define SYS_CLK_HZ 160000000.0
#define MIN_DIV_LIMIT 2
#define MAX_DIV_LIMIT 255 // ★ 255 → 64：高分频引发问题，硬性封顶
#define MIN_LEN_LIMIT 4

// ==========================================
// 2. 全局 Buffer：长度 ×8，放 PSRAM
// ==========================================
static uint8_t *s_wave_buf = nullptr;

bool wave_buf_ready() { return s_wave_buf != nullptr; }

bool init_wave_buf() {
    if (s_wave_buf) return true;
    s_wave_buf = (uint8_t *)heap_caps_aligned_alloc(64, MAX_CHAIN_LEN, MALLOC_CAP_SPIRAM);
    if (!s_wave_buf) {
        Serial.printf("FATAL: wave buf %d B PSRAM alloc failed (free psram %u)\n",
                      (int)MAX_CHAIN_LEN, (unsigned)ESP.getFreePsram());
        return false;
    }
    Serial.printf("# wave buffer %d B @ PSRAM %p (free psram %u)\n",
                  (int)MAX_CHAIN_LEN, s_wave_buf, (unsigned)ESP.getFreePsram());
    return true;
}

double out_freq(double freq, uint8_t mindiv = 8, int minpts = 20) {
    mindiv = mindiv < MIN_DIV_LIMIT ? MIN_DIV_LIMIT : mindiv;
    minpts = minpts < MIN_LEN_LIMIT ? MIN_LEN_LIMIT : minpts;
    if (!s_wave_buf) {
        stop_sin();
        Serial.println("# ERR: wave buffer not init (call init_wave_buf in setup)");
        return -3;
    }
    if (freq * MAX_DIV_LIMIT * MAX_CHAIN_LEN < SYS_CLK_HZ || freq * mindiv * minpts > SYS_CLK_HZ) {
        stop_sin();
        return -1;
    }
    double ever_best_err = freq;
    double best_err = freq;
    int best_pts = 20;
    uint8_t best_div = 8;
    bool can_find = false;
    for (uint8_t div = MAX_DIV_LIMIT; div >= mindiv; div--) {
        double pts = SYS_CLK_HZ / div / freq;
        int intpts = (int)(pts + 2) & 0xfffffffc;
        if (intpts > MAX_CHAIN_LEN || intpts < minpts) continue;
        double now_freq = SYS_CLK_HZ / div / intpts;
        can_find = true;
        double now_err = fabs(now_freq - freq);
        if (now_err <= ever_best_err + 0.0001) {
            best_err = now_err;
            best_pts = intpts;
            best_div = div;
        }
        if (now_err < ever_best_err) ever_best_err = now_err;
    }
    if (can_find == false) return -2;
    out_sin(best_pts, s_wave_buf, best_div);
    // ★ 唯一改动：4 行频率诊断数字受 g_lcr_diag 门控（错误行不门控）
    if (g_lcr_diag) {
        Serial.println(best_pts);
        Serial.println(best_div);
        Serial.println(SYS_CLK_HZ / best_div / best_pts);
        Serial.println(best_err);
    }
    return SYS_CLK_HZ / best_div / best_pts;
}
#endif
