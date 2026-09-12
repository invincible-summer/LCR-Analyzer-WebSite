#pragma once
/**
 * lcr_tone.h — 频率拟合 / 双音分析（纯计算，不碰任何硬件）
 * ★ 标定常数（换硬件后必须重标）：
 *   LCR_TONE_CH_SKEW_SLOTS  通道间采样偏移（采样槽数）
 *   LCR_TONE_CH_GAIN_RATIO  通道间增益比标定值
 */
#include <Arduino.h>
#include "DO_NOT_TOUCH_lcr_diag.h" // ★ 新增：诊断开关

#define LCR_TONE_CH_SKEW_SLOTS -1.0
#define LCR_TONE_CH_GAIN_RATIO 1

struct ToneResult { double amp; double phase_rad; };
struct MeasResult  { double f_used; double ratio_raw; double ratio_corr; double dphi_deg; };

static ToneResult lcr_tone_extract(const uint16_t *buf, uint32_t N, double fs, double f, bool use_hann) {
    double mean = 0.0;
    for (uint32_t n = 0; n < N; n++) mean += buf[n];
    mean /= (double)N;
    const double om = 2.0 * M_PI * f / fs;
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (uint32_t n = 0; n < N; n++) {
        double w = use_hann ? 0.5 * (1.0 - cos(2.0 * M_PI * (double)n / (double)(N - 1))) : 1.0;
        double s = ((double)buf[n] - mean) * w;
        re += s * cos(om * (double)n);
        im -= s * sin(om * (double)n);
        wsum += w;
    }
    ToneResult r;
    r.amp = 2.0 * sqrt(re * re + im * im) / wsum;
    r.phase_rad = atan2(im, re);
    return r;
}

static double lcr_tone_apparent_freq(const uint16_t *buf, uint32_t N, double fs, double f_known) {
    // ★ bugfix(问题3)：退化输入（样本过少 / fs、f 非法或 NaN）直接返回已知频率，
    //   避免 T_sub=0 等 0 除产生 NaN 向上传播。
    if (N < 4 || !(fs > 0.0) || !(f_known > 0.0)) return f_known;
    uint32_t Nh = N / 2;
    ToneResult r1 = lcr_tone_extract(buf, Nh, fs, f_known, false);
    ToneResult r2 = lcr_tone_extract(buf + (N - Nh), Nh, fs, f_known, false);
    double T_sub = (double)Nh / fs;
    double expected = 2.0 * M_PI * f_known * T_sub;
    double adv = r2.phase_rad - r1.phase_rad;
    while (adv > expected + M_PI) adv -= 2.0 * M_PI;
    while (adv < expected - M_PI) adv += 2.0 * M_PI;
    return f_known + (adv - expected) / (2.0 * M_PI * T_sub);
}

static MeasResult lcr_tone_analyze(const uint16_t *buf_a, uint32_t n_a,
                                   const uint16_t *buf_b, uint32_t n_b,
                                   double f_known, double fs) {
    uint32_t N = (n_a < n_b) ? n_a : n_b;
    // ★ bugfix(问题3)：无有效样本或参数非法时跳过拟合，返回 ratio=0 的无效
    //   结果（上层按 ratio 有效性判失败），防止 N=0 时 mean/0 等 0 除的
    //   NaN 从本层扩散到测量结果。
    if (N == 0 || !(fs > 0.0) || !(f_known > 0.0)) {
        MeasResult r;
        r.f_used = f_known;
        r.ratio_raw = 0.0;
        r.ratio_corr = 0.0;
        r.dphi_deg = 0.0;
        return r;
    }
    double f_app_A = lcr_tone_apparent_freq(buf_a, N, fs, f_known);
    double f_app_B = lcr_tone_apparent_freq(buf_b, N, fs, f_known);
    ToneResult ra = lcr_tone_extract(buf_a, N, fs, f_app_A, true);
    ToneResult rb = lcr_tone_extract(buf_b, N, fs, f_app_B, true);
    const double dt_ab = (double)LCR_TONE_CH_SKEW_SLOTS / (2.0 * fs);
    double phi_b_true = rb.phase_rad - 2.0 * M_PI * f_app_B * dt_ab;
    double dphi = phi_b_true - ra.phase_rad;
    while (dphi > M_PI)  dphi -= 2.0 * M_PI;
    while (dphi < -M_PI) dphi += 2.0 * M_PI;
    MeasResult r;
    r.f_used = f_known;
    r.ratio_raw = rb.amp / ra.amp;
    r.ratio_corr = r.ratio_raw / LCR_TONE_CH_GAIN_RATIO;
    r.dphi_deg = dphi * 180.0 / M_PI;
    // ★ 唯一改动：诊断块受 g_lcr_diag 门控
    if (g_lcr_diag) {
        Serial.printf("# ---- tone analysis (f=%.4f Hz, N=%u) ----\n", f_known, (unsigned)N);
        Serial.printf("# fs_per_ch: %.4f Hz\n", fs);
        Serial.printf("# f_app_A: %.4f Hz (offset %+.1f ppm)\n", f_app_A, (f_app_A - f_known) / f_known * 1e6);
        Serial.printf("# f_app_B: %.4f Hz (offset %+.1f ppm)\n", f_app_B, (f_app_B - f_known) / f_known * 1e6);
        Serial.printf("# amp_A: %.2f counts\n", ra.amp);
        Serial.printf("# amp_B: %.2f counts\n", rb.amp);
        Serial.printf("# amp_ratio_B/A corrected: %.6f\n", r.ratio_corr);
        Serial.printf("# skew_corr_B: %.2f deg (dt=%.1f us)\n", 2.0 * M_PI * f_app_B * dt_ab * 180.0 / M_PI, dt_ab * 1e6);
        Serial.printf("# phase_diff_B-A: %.2f deg (measured, before TIA inversion fix)\n", r.dphi_deg);
    }
    return r;
}
