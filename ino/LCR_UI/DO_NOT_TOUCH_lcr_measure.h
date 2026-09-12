#pragma once
/**
 * lcr_measure.h — 测量流程层：档位 MUX、自动量程、接线自检、
 * 完整测量流水线、复阻抗/LCR 换算、扫频汇总表
 * ★ 本版改动：所有诊断打印受 g_lcr_diag 门控（lcr_diag.h），
 *   显式打印函数（lcr_print_* 系列）不变，仅在上层显式调用时输出。
 */
#include <Arduino.h>
#include "DO_NOT_TOUCH_lcr_adc.h"
#include "DO_NOT_TOUCH_lcr_tone.h"
#include "DO_NOT_TOUCH_freq_calc.h"
#include "DO_NOT_TOUCH_lcr_diag.h"  // ★ 新增：诊断开关（须在 lcr_calib_core.h 之前可见）
#include "DO_NOT_TOUCH_lcr_calib_core.h"

// ---- 测量节奏参数 ----
#define MEAS_SETTLE_MS 2000UL
#define RANGE_SETTLE_MS 1000UL
#define SAMPLE_DURATION_MS 500UL
#define SWEEP_SETTLE_MS 1000UL
#define SWEEP_RANGE_SETTLE_MS 250UL
#define SWEEP_CAPTURE_CYCLES 20
#define SWEEP_CAPTURE_MIN_MS 500UL

// ---- 自动量程参数 ----
#define RANGE_LOW_LIMIT_V 0.5f
#define RANGE_HIGH_LIMIT_V 2.8f
#define QUICK_CHECK_CYCLES 5
#define QUICK_CHECK_MIN_MS 50
#define QUICK_CHECK_MAX_MS 500
#define AUTORANGE_MAX_STEPS 14

// ---- 统计/贴轨 ----
#define STATS_SKIP_HEAD 5
#define RAW_SAT_LOW 3
#define RAW_SAT_HIGH 4092
#define PRINT_RAW_TAIL 0
#define PRINT_TAIL_N 100
#define HEAD_PRINT_N 20

// ---- 电压通道偏置模型 ----
#define V_EXPECT_MIDRAIL 1

// ---- 接线自检窗口 ----
#define SELFCHK_BIAS_MIN_V 0.8f
#define SELFCHK_BIAS_MAX_V 2.8f
#define SELFCHK_MS 100

// ---- LCR 换算 ----
#define TIA_INVERTING 1
#define LCR_RESISTIVE_TOL_DEG 1.0

// ---- MUX 引脚（硬件 v3：2 片级联 74HC595）----
#define HC595_PIN_SRCLK 21
#define HC595_PIN_SER 19
#define HC595_PIN_RCLK 20

#define HC595_BIT_W0 1
#define HC595_BIT_W1 2
#define MUX_W_MASK ((1u << HC595_BIT_W0) | (1u << HC595_BIT_W1))

#define HC595_BIT_T2 3
#define HC595_BIT_T1 4
#define HC595_BIT_T0 5
#define HC595_BIT_T3 6

#define HC595_BIT_I_LSB 9
#define HC595_BIT_I_MSB 10

#define HC595_BIT_U_LSB 11
#define HC595_BIT_U_MSB 12

#define MUX_I_MASK ((1u << HC595_BIT_I_MSB) | (1u << HC595_BIT_I_LSB))
#define MUX_U_MASK ((1u << HC595_BIT_U_MSB) | (1u << HC595_BIT_U_LSB))
#define MUX_T_MASK ((1u << HC595_BIT_T3) | (1u << HC595_BIT_T2) | (1u << HC595_BIT_T1) | (1u << HC595_BIT_T0))

#define DEFAULT_VOLTAGE_RANGE VOLT_X1
#define DEFAULT_CURRENT_RANGE CURR_X1
#define DEFAULT_TIA_RANGE TIA_22

// ---- 档位类型 ----
enum VoltageRange : uint8_t { VOLT_X1 = 0,
                              VOLT_X3 = 1,
                              VOLT_X10 = 2,
                              VOLT_X33 = 3 };
enum CurrentRange : uint8_t { CURR_X1 = 0,
                              CURR_X3 = 1,
                              CURR_X10 = 2,
                              CURR_X33 = 3 };
enum TiaRange : uint8_t { TIA_22 = 0,
                          TIA_100,
                          TIA_330,
                          TIA_1K,
                          TIA_3K3,
                          TIA_10K,
                          TIA_33K,
                          TIA_100K,
                          TIA_330K,
                          TIA_1M };

#define TIA_RANGE_COUNT 10
static const char *const VOLT_RANGE_NAME[] = { "x1", "x3", "x10", "x33" };
static const char *const CURR_RANGE_NAME[] = { "x1", "x3", "x10", "x33" };
static const char *const TIA_RANGE_NAME[] = { "22", "100", "330", "1k", "3k3",
                                              "10k", "33k", "100k", "330k", "1M" };
static const double GAIN_OF_CODE[4] = { 1.0, 3.0, 10.0, 33.0 };
static const uint8_t TIA_ENUM_TO_CODE[TIA_RANGE_COUNT] = {
  0x0, 0x1, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF
};
static const double TIA_OHM[TIA_RANGE_COUNT] = {
  22.0, 100.0, 330.0, 1000.0, 3300.0, 10000.0, 33000.0, 100000.0, 330000.0, 1000000.0
};

// ---- 模块内状态 ----
static VoltageRange g_voltage_range = VOLT_X1;
static CurrentRange g_current_range = CURR_X1;
static TiaRange g_tia_range = TIA_22;
static uint32_t g_range_settle_ms = RANGE_SETTLE_MS;
static bool g_fast_settle = false;
static double g_sig_freq = 0.0;
static bool g_w_mode = false;

struct ChanStats {
  uint32_t n;
  float v_min, v_max, v_mean, amp_v;
  uint32_t n_sat_low, n_sat_high;
};

// ================== 档位 → 物理值 / 显示名 ==================
static const char *lcr_volt_range_name(VoltageRange r) {
  return VOLT_RANGE_NAME[(uint8_t)r];
}
static const char *lcr_curr_range_name(CurrentRange r) {
  return CURR_RANGE_NAME[(uint8_t)r];
}
static const char *lcr_tia_range_name(TiaRange r) {
  return TIA_RANGE_NAME[(uint8_t)r];
}
static double lcr_volt_gain_of(VoltageRange r) {
  uint8_t c = (uint8_t)r;
  return (c < 4) ? GAIN_OF_CODE[c] : 1.0;
}
static double lcr_curr_gain_of(CurrentRange r) {
  uint8_t c = (uint8_t)r;
  return (c < 4) ? GAIN_OF_CODE[c] : 1.0;
}
static double lcr_tia_ohm_of(TiaRange r) {
  uint8_t c = (uint8_t)r;
  if (c >= TIA_RANGE_COUNT) c = 0;
  return TIA_OHM[c];
}

// ================== 底层 MUX 写入（74HC595 级联） ==================
static uint16_t s_hc595_shadow = 0x0000;
static void s_hc595_latch() {
  digitalWrite(HC595_PIN_RCLK, LOW);
  for (int i = 15; i >= 0; i--) {
    digitalWrite(HC595_PIN_SRCLK, LOW);
    digitalWrite(HC595_PIN_SER, (s_hc595_shadow >> i) & 0x1);
    digitalWrite(HC595_PIN_SRCLK, HIGH);
  }
  digitalWrite(HC595_PIN_SRCLK, LOW);
  digitalWrite(HC595_PIN_RCLK, HIGH);
  digitalWrite(HC595_PIN_RCLK, LOW);
}
static void s_hc595_write(uint16_t mask, uint16_t bits) {
  s_hc595_shadow = (s_hc595_shadow & ~mask) | (bits & mask);
  s_hc595_latch();
}
static void s_mux_voltage_write(uint8_t code) {
  s_hc595_write(MUX_U_MASK, ((code & 0x2) ? (1u << HC595_BIT_U_MSB) : 0) | ((code & 0x1) ? (1u << HC595_BIT_U_LSB) : 0));
}
static void s_mux_current_write(uint8_t code) {
  s_hc595_write(MUX_I_MASK, ((code & 0x2) ? (1u << HC595_BIT_I_MSB) : 0) | ((code & 0x1) ? (1u << HC595_BIT_I_LSB) : 0));
}
static void s_mux_tia_write(uint8_t code) {
  s_hc595_write(MUX_T_MASK, ((code & 0x8) ? (1u << HC595_BIT_T3) : 0) | ((code & 0x4) ? (1u << HC595_BIT_T2) : 0) | ((code & 0x2) ? (1u << HC595_BIT_T1) : 0) | ((code & 0x1) ? (1u << HC595_BIT_T0) : 0));
}
static void s_wctrl(bool on) {
  s_hc595_write(MUX_W_MASK, on ? MUX_W_MASK : 0);
}

// ================== 档位设置（打印已门控） ==================
static bool lcr_set_voltage_range(VoltageRange r) {
  if ((uint8_t)r > 3) return false;
  s_mux_voltage_write((uint8_t)r);
  g_voltage_range = r;
  delay(g_range_settle_ms);
  if (g_lcr_diag) Serial.printf("# RANGE voltage = %s\n", VOLT_RANGE_NAME[(uint8_t)r]);
  return true;
}
static bool lcr_set_current_range(CurrentRange r) {
  if ((uint8_t)r > 3) return false;
  s_mux_current_write((uint8_t)r);
  g_current_range = r;
  delay(g_range_settle_ms);
  if (g_lcr_diag) Serial.printf("# RANGE current = %s\n", CURR_RANGE_NAME[(uint8_t)r]);
  return true;
}
static bool lcr_set_tia_range(TiaRange r) {
  if ((uint8_t)r >= TIA_RANGE_COUNT) return false;
  s_mux_tia_write(TIA_ENUM_TO_CODE[(uint8_t)r]);
  g_tia_range = r;
  delay(g_range_settle_ms);
  if (g_lcr_diag) Serial.printf("# RANGE tia = %s ohm\n", TIA_RANGE_NAME[(uint8_t)r]);
  return true;
}
static void lcr_reset_all_ranges() {
  s_mux_voltage_write((uint8_t)VOLT_X1);
  g_voltage_range = VOLT_X1;
  s_mux_current_write((uint8_t)CURR_X1);
  g_current_range = CURR_X1;
  s_mux_tia_write(TIA_ENUM_TO_CODE[(uint8_t)DEFAULT_TIA_RANGE]);
  g_tia_range = DEFAULT_TIA_RANGE;
  delay(g_range_settle_ms);
  if (g_lcr_diag)
    Serial.printf("# RANGE reset to defaults: V=%s I=%s TIA=%s\n",
                  VOLT_RANGE_NAME[g_voltage_range], CURR_RANGE_NAME[g_current_range],
                  TIA_RANGE_NAME[g_tia_range]);
}
static void lcr_set_fast_settle(bool fast) {
  g_fast_settle = fast;
  g_range_settle_ms = fast ? SWEEP_RANGE_SETTLE_MS : RANGE_SETTLE_MS;
}

// ---- 校准用挡位锁定(0xFF = 不锁定) ----
static TiaRange g_tia_pin = (TiaRange)0xFF;
static CurrentRange g_ipin = (CurrentRange)0xFF;
static void lcr_set_tia_pin(int8_t r) {
  g_tia_pin = (r < 0 || r >= TIA_RANGE_COUNT) ? (TiaRange)0xFF : (TiaRange)r;
}
static void lcr_set_ipin(int8_t r) {
  g_ipin = (r < 0 || r > 3) ? (CurrentRange)0xFF : (CurrentRange)r;
}

// ================== 测量节奏 / 统计辅助 ==================
static uint32_t s_quick_check_ms(double f) {
  uint32_t ms = (uint32_t)(QUICK_CHECK_CYCLES * 1000.0 / f);
  if (ms < QUICK_CHECK_MIN_MS) ms = QUICK_CHECK_MIN_MS;
  if (ms > QUICK_CHECK_MAX_MS) ms = QUICK_CHECK_MAX_MS;
  return ms;
}
static uint32_t s_sweep_capture_ms(double f) {
  uint32_t ms = (uint32_t)(SWEEP_CAPTURE_CYCLES * 1000.0 / f);
  if (ms < SWEEP_CAPTURE_MIN_MS) ms = SWEEP_CAPTURE_MIN_MS;
  if (ms > LCR_ADC_CAPTURE_MS_MAX) ms = LCR_ADC_CAPTURE_MS_MAX;
  return ms;
}
static ChanStats s_chan_stats(const uint16_t *buf, uint32_t n, double f) {
  ChanStats st;
  st.n = n;
  st.v_min = 99.f;
  st.v_max = -99.f;
  st.v_mean = 0.f;
  st.amp_v = 0.f;
  st.n_sat_low = st.n_sat_high = 0;
  if (n == 0) return st;
  uint32_t skip = (n > (uint32_t)STATS_SKIP_HEAD) ? (uint32_t)STATS_SKIP_HEAD : 0;
  double sum = 0.0;
  uint32_t cnt = 0;
  for (uint32_t i = skip; i < n; i++) {
    uint16_t raw = buf[i];
    float v = lcr_adc_raw_to_volt(raw);
    sum += v;
    if (v < st.v_min) st.v_min = v;
    if (v > st.v_max) st.v_max = v;
    if (raw <= RAW_SAT_LOW) st.n_sat_low++;
    if (raw >= RAW_SAT_HIGH) st.n_sat_high++;
    cnt++;
  }
  if (cnt > 0) st.v_mean = (float)(sum / (double)cnt);
  if (n > skip) {
    ToneResult t = lcr_tone_extract(buf + skip, n - skip, lcr_adc_fs_per_channel(), f, true);
    st.amp_v = (float)(t.amp * 3.3f / (double)((1 << LCR_ADC_BIT_WIDTH) - 1));
  }
  return st;
}
static bool s_voltage_out_of_range(const ChanStats &st) {
  bool clip_hi = (st.n_sat_high > 0) || (st.v_max >= RANGE_HIGH_LIMIT_V);
  bool clip_lo = (st.n_sat_low > 0) || (st.v_min <= RANGE_LOW_LIMIT_V);
#if V_EXPECT_MIDRAIL
  (void)0;
  return clip_lo || clip_hi;
#else
  return clip_hi;
#endif
}
static inline bool s_current_out_of_range(const ChanStats &st) {
  return (st.v_min <= RANGE_LOW_LIMIT_V) || (st.v_max >= RANGE_HIGH_LIMIT_V);
}

// ================== 自动量程（打印已门控） ==================
static void s_autorange_voltage() {
  uint32_t dur = s_quick_check_ms(g_sig_freq);
  for (int step = 0; step < AUTORANGE_MAX_STEPS; step++) {
    lcr_adc_capture(dur);
    ChanStats st = s_chan_stats(lcr_adc_buf_a(), lcr_adc_count_a(), g_sig_freq);
    bool viol = s_voltage_out_of_range(st);
    if (g_lcr_diag)
      Serial.printf("# AUTOCHK V: n=%u bias=%.2f vMin=%.2f vMax=%.2f"
                    " amp=%.3fV satL=%u satH=%u gain=%s -> %s\n",
                    (unsigned)st.n, st.v_mean, st.v_min, st.v_max, st.amp_v,
                    (unsigned)st.n_sat_low, (unsigned)st.n_sat_high,
                    VOLT_RANGE_NAME[g_voltage_range], viol ? "OVER" : "ok");
    if (st.n == 0) {
      if (g_lcr_diag) Serial.println("# AUTORANGE: no voltage samples, abort check");
      break;
    }
    if (!viol) {
      if (g_voltage_range == VOLT_X33) {
        if (g_lcr_diag) Serial.println("# AUTORANGE: voltage clean at x33 (max gain), stop here");
        break;
      }
      lcr_set_voltage_range((VoltageRange)((uint8_t)g_voltage_range + 1));
      continue;
    }
    if (g_voltage_range != VOLT_X1) {
      lcr_set_voltage_range((VoltageRange)((uint8_t)g_voltage_range - 1));
      if (g_lcr_diag) Serial.println("# AUTORANGE: backed off one voltage step");
    } else {
      if (g_lcr_diag) Serial.println("# AUTORANGE: voltage over-range even at x1 (min gain)");
    }
    break;
  }
}
static void s_autorange_current() {
  uint32_t dur = s_quick_check_ms(g_sig_freq);
  // 阶段1：扫跨阻（I=x1），22→…→1M，OVER 退一档（W 模式跳过）
  for (int step = 0; step < AUTORANGE_MAX_STEPS && !g_w_mode; step++) {
    lcr_adc_capture(dur);
    ChanStats st = s_chan_stats(lcr_adc_buf_b(), lcr_adc_count_b(), g_sig_freq);
    bool viol = s_current_out_of_range(st);
    if (g_lcr_diag)
      Serial.printf("# AUTOCHK I: n=%u bias=%.2f vMin=%.2f vMax=%.2f"
                    " amp=%.3fV satL=%u satH=%u TIA=%s I=%s -> %s\n",
                    (unsigned)st.n, st.v_mean, st.v_min, st.v_max, st.amp_v,
                    (unsigned)st.n_sat_low, (unsigned)st.n_sat_high,
                    TIA_RANGE_NAME[g_tia_range], CURR_RANGE_NAME[g_current_range],
                    viol ? "OVER" : "ok");
    if (st.n == 0) {
      if (g_lcr_diag) Serial.println("# AUTORANGE: no current samples, abort check");
      return;
    }
    if (viol) {
      if (g_tia_range != TIA_22) {
        lcr_set_tia_range((TiaRange)((uint8_t)g_tia_range - 1));
        if (g_lcr_diag) Serial.println("# AUTORANGE: backed off one TIA step");
      } else {
        if (g_lcr_diag) Serial.println("# AUTORANGE: over-range even at 22 ohm (min TIA)");
      }
      break;
    }
    if (g_tia_range == TIA_1M) {
      if (g_lcr_diag) Serial.println("# AUTORANGE: clean at 1M (max TIA), TIA fixed here");
      break;
    }
    lcr_set_tia_range((TiaRange)((uint8_t)g_tia_range + 1));
  }
  // 阶段2：电流增益 x1→x3→x10→x33（跨阻已定），OVER 退一档
  for (int step = 0; step < AUTORANGE_MAX_STEPS; step++) {
    lcr_adc_capture(dur);
    ChanStats st = s_chan_stats(lcr_adc_buf_b(), lcr_adc_count_b(), g_sig_freq);
    bool viol = s_current_out_of_range(st);
    if (g_lcr_diag)
      Serial.printf("# AUTOCHK Igain: n=%u bias=%.2f vMin=%.2f vMax=%.2f"
                    " amp=%.3fV satL=%u satH=%u TIA=%s I=%s -> %s\n",
                    (unsigned)st.n, st.v_mean, st.v_min, st.v_max, st.amp_v,
                    (unsigned)st.n_sat_low, (unsigned)st.n_sat_high,
                    TIA_RANGE_NAME[g_tia_range], CURR_RANGE_NAME[g_current_range],
                    viol ? "OVER" : "ok");
    if (st.n == 0) {
      if (g_lcr_diag) Serial.println("# AUTORANGE: no current samples, abort gain check");
      break;
    }
    if (!viol) {
      if (g_current_range == CURR_X33) {
        if (g_lcr_diag) Serial.println("# AUTORANGE: current gain clean at x33 (max), stop here");
        break;
      }
      lcr_set_current_range((CurrentRange)((uint8_t)g_current_range + 1));
      continue;
    }
    if (g_current_range != CURR_X1) {
      lcr_set_current_range((CurrentRange)((uint8_t)g_current_range - 1));
      if (g_lcr_diag) Serial.println("# AUTORANGE: backed off one current-gain step");
    } else {
      if (g_lcr_diag) Serial.println("# AUTORANGE: current over-range even at I=x1 (gain stays x1)");
    }
    break;
  }
}

// ================== 接线自检（整体门控） ==================
static void lcr_wiring_selfcheck() {
  if (!g_lcr_diag) return;
  lcr_adc_capture(SELFCHK_MS);
  double sumA = 0.0, sumB = 0.0;
  for (uint32_t i = 0; i < lcr_adc_count_a(); i++) sumA += lcr_adc_buf_a()[i];
  for (uint32_t i = 0; i < lcr_adc_count_b(); i++) sumB += lcr_adc_buf_b()[i];
  float biasA = (lcr_adc_count_a()) ? lcr_adc_raw_to_volt((uint16_t)(sumA / lcr_adc_count_a())) : -1.0f;
  float biasB = (lcr_adc_count_b()) ? lcr_adc_raw_to_volt((uint16_t)(sumB / lcr_adc_count_b())) : -1.0f;
  Serial.printf("# SELFCHK: chA(GPIO2 V) bias=%.2fV chB(GPIO1 I) bias=%.2fV (expect both ~1.5V)\n",
                biasA, biasB);
  if (lcr_adc_count_a() == 0 || lcr_adc_count_b() == 0) {
    Serial.println("# SELFCHK WARN: missing samples on a channel!");
    return;
  }
  if (biasA < SELFCHK_BIAS_MIN_V)
    Serial.println("# SELFCHK WARN: chA bias LOW -> diff-amp not wired to GPIO2?");
  if (biasA > SELFCHK_BIAS_MAX_V)
    Serial.println("# SELFCHK WARN: chA bias HIGH -> GPIO2 driven to rail, wrong pin?");
  if (biasB < SELFCHK_BIAS_MIN_V)
    Serial.println("# SELFCHK WARN: chB bias LOW -> TIA not wired to GPIO1?");
  if (biasB > SELFCHK_BIAS_MAX_V)
    Serial.println("# SELFCHK WARN: chB bias HIGH -> GPIO1 driven to rail (UART?), TIA virtual ground collapsed!");
}

// ================== 诊断输出（由 verbose 参数控制，API 调用传 false） ==================
static void s_print_diagnostics(uint32_t capture_ms) {
  float secs = capture_ms / 1000.0f;
  Serial.printf("# invalid_samples: %u\n", (unsigned)lcr_adc_invalid());
  Serial.printf("# pool_overflow: %s\n", lcr_adc_pool_ovf() ? "YES" : "NO");
  Serial.printf("# count_diag: A=%u B=%u diff(A-B)=%d\n",
                (unsigned)lcr_adc_count_a(), (unsigned)lcr_adc_count_b(),
                (int)((int32_t)lcr_adc_count_a() - (int32_t)lcr_adc_count_b()));
  Serial.printf("# elapsed: %u ms\n", (unsigned)capture_ms);
  Serial.printf("# rate_chA: %.2f kSPS\n", (secs > 0) ? lcr_adc_count_a() / secs / 1000.0f : 0);
  Serial.printf("# rate_chB: %.2f kSPS\n", (secs > 0) ? lcr_adc_count_b() / secs / 1000.0f : 0);
  uint32_t satA_lo = 0, satA_hi = 0, satB_lo = 0, satB_hi = 0;
  uint32_t skipA = (lcr_adc_count_a() > (uint32_t)STATS_SKIP_HEAD) ? (uint32_t)STATS_SKIP_HEAD : 0;
  uint32_t skipB = (lcr_adc_count_b() > (uint32_t)STATS_SKIP_HEAD) ? (uint32_t)STATS_SKIP_HEAD : 0;
  for (uint32_t i = skipA; i < lcr_adc_count_a(); i++) {
    if (lcr_adc_buf_a()[i] <= RAW_SAT_LOW) satA_lo++;
    if (lcr_adc_buf_a()[i] >= RAW_SAT_HIGH) satA_hi++;
  }
  for (uint32_t i = skipB; i < lcr_adc_count_b(); i++) {
    if (lcr_adc_buf_b()[i] <= RAW_SAT_LOW) satB_lo++;
    if (lcr_adc_buf_b()[i] >= RAW_SAT_HIGH) satB_hi++;
  }
  if (satA_lo || satA_hi)
    Serial.printf("# WARN: chA rail clipping (satL=%u satH=%u)\n", (unsigned)satA_lo, (unsigned)satA_hi);
  if (satB_lo || satB_hi)
    Serial.printf("# WARN: chB rail clipping (satL=%u satH=%u)\n", (unsigned)satB_lo, (unsigned)satB_hi);
}

// ================== 工程格式化 ==================
static void s_si_fmt(char *out, size_t len, double v, const char *unit) {
  const char *pre[] = { "p", "n", "u", "m", "", "k", "M", "G" };
  int idx = 4;
  double a = fabs(v);
  while (a >= 1000.0 && idx < 7) {
    a /= 1000.0;
    v /= 1000.0;
    idx++;
  }
  while (a > 0.0 && a < 1.0 && idx > 0) {
    a *= 1000.0;
    v *= 1000.0;
    idx--;
  }
  snprintf(out, len, "%.4g %s%s", v, pre[idx], unit);
}
static void s_si_fmt_ns(char *out, size_t len, double v, const char *unit) {
  const char *pre[] = { "p", "n", "u", "m", "", "k", "M", "G" };
  int idx = 4;
  double a = fabs(v);
  while (a >= 1000.0 && idx < 7) {
    a /= 1000.0;
    v /= 1000.0;
    idx++;
  }
  while (a > 0.0 && a < 1.0 && idx > 0) {
    a *= 1000.0;
    v *= 1000.0;
    idx--;
  }
  snprintf(out, len, "%.4g%s%s", v, pre[idx], unit);
}

// ================== 复阻抗派生（单点/扫频共用，无打印） ==================
struct ZDerived {
  double f;
  double y_mag, phi_y_deg;
  double G, B;
  double z_re, z_im, z_mag, phi_z_deg;
  double D, Q;
  char type;
  double rs, cs, ls;
  double rp, cp, lp;
};

static bool lcr_derive_z(const MeasResult &m, ZDerived &z) {
  double v_gain = lcr_volt_gain_of(g_voltage_range);
  double i_gain = lcr_curr_gain_of(g_current_range);
  double r_tia = lcr_tia_ohm_of(g_tia_range);
  if (m.ratio_corr <= 0.0) return false;
  z.f = m.f_used;
  z.y_mag = m.ratio_corr * v_gain / (i_gain * r_tia);
  double phi_y = m.dphi_deg;
#if TIA_INVERTING
  phi_y -= 180.0;
#endif
  while (phi_y > 180.0) phi_y -= 360.0;
  while (phi_y < -180.0) phi_y += 360.0;
  z.phi_y_deg = phi_y;
  double phi_rad = phi_y * M_PI / 180.0;
  z.G = z.y_mag * cos(phi_rad);
  z.B = z.y_mag * sin(phi_rad);
  double denom = z.G * z.G + z.B * z.B;
  z.z_re = (denom > 1e-30) ? z.G / denom : 0.0;
  z.z_im = (denom > 1e-30) ? -z.B / denom : 0.0;
  // 链路修正(乘性) + 开/短路补偿(加性)，顺序：先除后减
  lcr_calib_correct(z.z_re, z.z_im, z.f, (int)g_tia_range);
  lcr_os_correct(z.z_re, z.z_im, z.f);
  {  // 修正后导纳侧反推重算
    double d2 = z.z_re * z.z_re + z.z_im * z.z_im;
    if (d2 > 1e-30) {
      z.G = z.z_re / d2;
      z.B = -z.z_im / d2;
      z.y_mag = sqrt(d2);
      z.phi_y_deg = atan2(z.B, z.G) * 180.0 / M_PI;
    }
  }
  z.z_mag = sqrt(z.z_re * z.z_re + z.z_im * z.z_im);
  z.phi_z_deg = atan2(z.z_im, z.z_re) * 180.0 / M_PI;
  z.D = (fabs(z.B) > 1e-30) ? fabs(z.G / z.B) : 1e30;
  z.Q = (z.D > 1e-30) ? 1.0 / z.D : 1e30;
  double w = 2.0 * M_PI * z.f;
  z.rs = z.z_re;
  z.cs = (w > 0.0 && z.z_im < 0.0) ? -1.0 / (w * z.z_im) : NAN;
  z.ls = (w > 0.0 && z.z_im > 0.0) ? z.z_im / w : NAN;
  //z.rp = (z.G > 1e-15) ? 1.0 / z.G : 1e15;
  z.rp = (fabs(z.G) > 1e-15) ? 1.0 / z.G : NAN;
  z.cp = (w > 0.0 && z.B > 0.0) ? z.B / w : NAN;
  z.lp = (w > 0.0 && z.B < 0.0) ? -1.0 / (w * z.B) : NAN;
  // 判型统一使用校准/OS 修正后重算的相位(z.phi_y_deg)与电纳(z.B)，
// 与修正后的数值同源；phi_y(修正前)仅用于日志和上层换算参考
if (fabs(z.phi_y_deg) < LCR_RESISTIVE_TOL_DEG || fabs(z.phi_y_deg) > 180.0 - LCR_RESISTIVE_TOL_DEG) {
    z.type = 'R';
    z.cs = z.ls = z.cp = z.lp = NAN;
}
else if (z.B > 0.0) {
    z.type = 'C';
    z.ls = z.lp = NAN;
}
else {
    z.type = 'L';
    z.cs = z.cp = NAN;
}

  return true;
}

// 以下显式打印函数保持原样（仅上层显式调用时输出，API 不调用）
static void lcr_print_lcr_analysis(const MeasResult &m) { /* …原样保留… */
  ZDerived z;
  if (!lcr_derive_z(m, z)) {
    Serial.println("# LCR ERR: amplitude ratio invalid");
    return;
  }
  char s1[32], s2[32], s3[32], s4[32];
  Serial.printf("# ---- LCR analysis [TIA=%s V=%s I=%s] ----\n",
                TIA_RANGE_NAME[g_tia_range], VOLT_RANGE_NAME[g_voltage_range],
                CURR_RANGE_NAME[g_current_range]);
  Serial.printf("# |Y|=%.6g S dphi_meas=%+.2f deg phi_Y=%+.2f deg |Z|=%.6g ohm D(tanδ)=%.4g Q=%.4g\n",
                z.y_mag, m.dphi_deg, z.phi_y_deg, z.z_mag, z.D, z.Q);
  s_si_fmt_ns(s1, sizeof(s1), z.z_re, "");
  s_si_fmt_ns(s2, sizeof(s2), fabs(z.z_im), "");
  Serial.printf("# Z = %s %s j%s ohm\n", s1, (z.z_im < 0.0) ? "-" : "+", s2);
  Serial.printf("# RESULT_Z a_ohm=%.6g b_ohm=%+.6g\n", z.z_re, z.z_im);
  if (z.type == 'R') {
    s_si_fmt(s1, sizeof(s1), z.z_re, "ohm");
    Serial.printf("# LCR: RESISTIVE R=%s (D=%.3g)\n", s1, z.D);
    Serial.printf("# RESULT_LCR type=R R_ohm=%.6g D=%.4g\n", z.z_re, z.D);
    if (z.z_re < 0.0) Serial.println("# LCR INFO: negative R (Rs<0) -> active DUT (negative resistance)");
  } else if (z.type == 'C') {
    s_si_fmt(s1, sizeof(s1), z.rp, "ohm");
    s_si_fmt(s2, sizeof(s2), z.cp, "F");
    s_si_fmt(s3, sizeof(s3), z.rs, "ohm");
    s_si_fmt(s4, sizeof(s4), z.cs, "F");
    Serial.printf("# LCR: CAPACITIVE\n");
    Serial.printf("# parallel: Rp=%s Cp=%s\n", s1, s2);
    Serial.printf("# series: Rs=%s Cs=%s\n", s3, s4);
    Serial.printf("# RESULT_LCR type=C Rp_ohm=%.6g C_F=%.6g Rs_ohm=%.6g Cs_F=%.6g D=%.4g Q=%.4g\n",
                  z.rp, z.cp, z.rs, z.cs, z.D, z.Q);
    if (z.rp < 0.0 || z.rs < 0.0) Serial.println("# LCR INFO: negative R component -> active DUT (negative resistance)");
    if (z.D > 5.0) Serial.println("# LCR HINT: D>>1 -> loss dominates, treat as resistive DUT");
  } else {
    s_si_fmt(s1, sizeof(s1), z.rs, "ohm");
    s_si_fmt(s2, sizeof(s2), z.ls, "H");
    s_si_fmt(s3, sizeof(s3), z.rp, "ohm");
    s_si_fmt(s4, sizeof(s4), z.lp, "H");
    Serial.printf("# LCR: INDUCTIVE\n");
    Serial.printf("# series: Rs=%s Ls=%s\n", s1, s2);
    Serial.printf("# parallel: Rp=%s Lp=%s\n", s3, s4);
    Serial.printf("# RESULT_LCR type=L Rs_ohm=%.6g L_H=%.6g Rp_ohm=%.6g Lp_H=%.6g D=%.4g Q=%.4g\n",
                  z.rs, z.ls, z.rp, z.lp, z.D, z.Q);
    if (z.rs < 0.0) Serial.println("# LCR INFO: negative Rs -> active DUT (negative resistance)");
    if (z.D > 5.0) Serial.println("# LCR HINT: D>>1 -> loss dominates, treat as resistive DUT");
  }
}
static void lcr_print_result_line(double f_req, const MeasResult &r) {
  Serial.printf("# RESULT f_req=%.4f f_act=%.4f ratio_corr=%.6f dphi_deg=%.3f"
                " [TIA=%s V=%s I=%s]\n",
                f_req, r.f_used, r.ratio_corr, r.dphi_deg,
                TIA_RANGE_NAME[g_tia_range], VOLT_RANGE_NAME[g_voltage_range],
                CURR_RANGE_NAME[g_current_range]);
}

// ================== 完整单点测量流水线（打印已门控） ==================
static MeasResult lcr_measure_point(double f_req, bool verbose) {
  // 1) 输出频率
  if (g_lcr_diag) Serial.printf("# SET f = %.4f Hz -> out_freq()\n", f_req);
  double f_act = out_freq(f_req, 20, 20);
  if (f_act > 0.0) g_sig_freq = f_act;
  else {
    g_sig_freq = f_req;
    if (g_lcr_diag) Serial.println("# WARN: out_freq return invalid, fallback to requested");
  }
  if (g_lcr_diag)
    Serial.printf("# actual f = %.4f Hz (err %+.1f ppm vs requested)\n",
                  g_sig_freq, (g_sig_freq - f_req) / f_req * 1e6);
  if (g_sig_freq > 20800.0 || g_sig_freq < 10.0)
    if (g_lcr_diag) Serial.println("# WARN: actual freq outside analysis range, check result");
  // 2) 稳定等待
  uint32_t settle = g_fast_settle ? SWEEP_SETTLE_MS : MEAS_SETTLE_MS;
  if (g_lcr_diag) Serial.printf("# settling %u ms...\n", (unsigned)settle);
  delay(settle);
  // 3) 重置档位 + 自动量程
  lcr_reset_all_ranges();
  bool pinned = (g_tia_pin < TIA_RANGE_COUNT);
  if (pinned) {
    lcr_set_tia_range(g_tia_pin);
    if (g_ipin < 4) lcr_set_current_range(g_ipin);
  }
  s_autorange_voltage();
  if (!pinned) s_autorange_current();
  // 4) 正式采集
  uint32_t dur = g_fast_settle ? s_sweep_capture_ms(g_sig_freq) : SAMPLE_DURATION_MS;
  uint32_t elapsed = lcr_adc_capture(dur);
  if (verbose) s_print_diagnostics(elapsed);
  // 5) 双音分析
  return lcr_tone_analyze(lcr_adc_buf_a(), lcr_adc_count_a(),
                          lcr_adc_buf_b(), lcr_adc_count_b(),
                          g_sig_freq, lcr_adc_fs_per_channel());
}

// ================== 初始化 ==================
static void lcr_measure_init() {
  pinMode(HC595_PIN_SER, OUTPUT);
  pinMode(HC595_PIN_SRCLK, OUTPUT);
  pinMode(HC595_PIN_RCLK, OUTPUT);
  digitalWrite(HC595_PIN_SER, LOW);
  digitalWrite(HC595_PIN_SRCLK, LOW);
  digitalWrite(HC595_PIN_RCLK, LOW);
  s_hc595_shadow = 0x0000;
  s_hc595_latch();
  s_wctrl(false);
  lcr_reset_all_ranges();
}

// ================== 扫频汇总表（显式打印，保持原样） ==================
static void s_print_row(const char *name, const ZDerived *tab, uint8_t n,
                        double (*fn)(const ZDerived &), bool si) {
  Serial.printf("# %-9s:", name);
  for (uint8_t i = 0; i < n; i++) {
    double v = fn(tab[i]);
    char cell[24];
    if (isnan(v)) snprintf(cell, sizeof(cell), "nan");
    else if (si) s_si_fmt_ns(cell, sizeof(cell), v, "");
    else snprintf(cell, sizeof(cell), "%.4g", v);
    Serial.printf(" %s", cell);
  }
  Serial.println();
}
static void lcr_print_sweep_table(const ZDerived *tab, uint8_t n) { /* 原样保留 */
  Serial.println("# ================ AUTO SWEEP RESULT ================");
  Serial.printf("# N=%u, log-spaced %.4g Hz -> %.4g Hz; one param per row, one freq per column\n",
                (unsigned)n, tab[0].f, tab[n - 1].f);
  s_print_row(
    "f_Hz", tab, n, [](const ZDerived &z) {
      return z.f;
    },
    false);
  Serial.printf("# %-9s:", "type");
  for (uint8_t i = 0; i < n; i++) Serial.printf(" %c", tab[i].type);
  Serial.println();
  s_print_row(
    "Zre_ohm", tab, n, [](const ZDerived &z) {
      return z.z_re;
    },
    true);
  s_print_row(
    "Zim_ohm", tab, n, [](const ZDerived &z) {
      return z.z_im;
    },
    true);
  s_print_row(
    "Zmag_ohm", tab, n, [](const ZDerived &z) {
      return z.z_mag;
    },
    true);
  s_print_row(
    "phiZ_deg", tab, n, [](const ZDerived &z) {
      return z.phi_z_deg;
    },
    false);
  s_print_row(
    "D_tand", tab, n, [](const ZDerived &z) {
      return z.D;
    },
    false);
  s_print_row(
    "Q", tab, n, [](const ZDerived &z) {
      return z.Q;
    },
    false);
  s_print_row(
    "Rs_ohm", tab, n, [](const ZDerived &z) {
      return z.rs;
    },
    true);
  s_print_row(
    "Cs_F", tab, n, [](const ZDerived &z) {
      return z.cs;
    },
    true);
  s_print_row(
    "Ls_H", tab, n, [](const ZDerived &z) {
      return z.ls;
    },
    true);
  s_print_row(
    "Rp_ohm", tab, n, [](const ZDerived &z) {
      return z.rp;
    },
    true);
  s_print_row(
    "Cp_F", tab, n, [](const ZDerived &z) {
      return z.cp;
    },
    true);
  s_print_row(
    "Lp_H", tab, n, [](const ZDerived &z) {
      return z.lp;
    },
    true);
  Serial.println("# note: Cs/Ls valid per type; nan = N/A; Rs=Zre, D=|Zre/Zim|");
  Serial.println("# ===================================================");
}

// ================== W 旁路模式 ==================
struct WPoint {
  double f, h_mag, h_db, phase_deg;
};
static bool lcr_w_measure(double f_req, WPoint &wp, bool verbose) {
  g_w_mode = true;
  s_wctrl(true);
  MeasResult m = lcr_measure_point(f_req, verbose);
  s_wctrl(false);
  g_w_mode = false;
  if (m.ratio_corr <= 0.0) return false;
  double v_g = lcr_volt_gain_of(g_voltage_range);
  double i_g = lcr_curr_gain_of(g_current_range);
  wp.f = m.f_used;
  wp.h_mag = m.ratio_corr * v_g / i_g;
  wp.h_db = 20.0 * log10(wp.h_mag);
  wp.phase_deg = m.dphi_deg;
  return true;
}
static void lcr_print_w_table(const WPoint *tab, uint8_t n) { /* 原样保留 */
  Serial.println("# ================ W MODE BODE RESULT ================");
  Serial.printf("# N=%u, log-spaced %.4g Hz -> %.4g Hz; H = Vout/Vin (ext network)\n",
                (unsigned)n, tab[0].f, tab[n - 1].f);
  Serial.printf("# %-9s:", "f_Hz");
  for (uint8_t i = 0; i < n; i++) Serial.printf(" %.4g", tab[i].f);
  Serial.println();
  Serial.printf("# %-9s:", "H_dB");
  for (uint8_t i = 0; i < n; i++) {
    if (isnan(tab[i].h_db) || tab[i].h_mag <= 0) Serial.printf(" nan");
    else Serial.printf(" %.4g", tab[i].h_db);
  }
  Serial.println();
  Serial.printf("# %-9s:", "H_mag");
  for (uint8_t i = 0; i < n; i++) {
    char cell[24];
    if (isnan(tab[i].h_mag) || tab[i].h_mag <= 0) snprintf(cell, sizeof(cell), "nan");
    else s_si_fmt_ns(cell, sizeof(cell), tab[i].h_mag, "");
    Serial.printf(" %s", cell);
  }
  Serial.println();
  Serial.printf("# %-9s:", "phase_deg");
  for (uint8_t i = 0; i < n; i++) Serial.printf(" %+.3f", tab[i].phase_deg);
  Serial.println();
  Serial.println("# note: phase NOT 180-shifted (TIA bypassed); raw chain, no calib");
  Serial.println("# ===================================================");
}
