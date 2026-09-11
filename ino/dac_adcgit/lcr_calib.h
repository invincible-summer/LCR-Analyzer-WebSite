// ====================================================================
// lcr_calib.h — v2: pole-free 22Ω + 开/短路补偿 + 校准备份
// 模型: Z_meas = Z_true · g · (1 + j·f/f_p) · e^(j·2πf·τ)
//   f_p=1e9 表示无极点挡(22Ω 未并 C_f),修正时跳过极点项只除 g
// 命令:
//   jz<range>_<r_std>   三参数校准(原样)   裸 jz = 状态
//   jzopen / jzshort    夹具开路/短路表征(7 频点,自动量程)
//   jzdump / osdump     打印可回贴备份行
//   jzset<i>_<g>_<fp>_<tau>              恢复一个挡位
//   osseto_<i>_<re>_<im> / ossets_<i>_<re>_<im>  恢复开/短路段
// ====================================================================
#pragma once
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "lcr_measure.h"

// ---------- 可调常量 ----------
static const int NF = 5;
static const double F[NF] = {100.0, 316.228, 1000.0, 3162.28, 10000.0};
#define CAL_IGAIN_BOOST_ABOVE_HZ 30000.0
#define CAL_IGAIN_BOOST_CODE 2
#define CAL_N_MIN 4
#define CAL_SPREAD_MAX 0.15
#define CAL_G_MIN 0.90
#define CAL_G_MAX 1.10
#define CAL_FP_MIN_HZ 5000.0
#define CAL_FP_MAX_HZ 500000.0
#define CAL_TAU_WARN_S 2e-6
#define CAL_POLEFREE_RATIO 0.03   // 10k 处 Im/Re 低于此 → 无显著 C_f(f_p>300k 等效)

static const char* calib_range_name(int idx) {
  static const char* nm[10] = { "22","100","330","1k","3k3","10k","33k","100k","330k","1M" };
  return (idx >= 0 && idx < 10) ? nm[idx] : "?";
}
static int calib_range_to_idx(TiaRange r) { int i = (int)r; return (i >= 0 && i < 10) ? i : -1; }

static double dmedian(double* a, int n) {
  qsort(a, n, sizeof(double), [](const void* x, const void* y) {
    double dx = *(const double*)x, dy = *(const double*)y;
    return (dx > dy) - (dx < dy);
  });
  return (n & 1) ? a[n/2] : 0.5 * (a[n/2-1] + a[n/2]);
}

// ---- jz<range>_<r_std> 解析(原样) ----
static bool lcr_calib_parse_jz(const char* buf, TiaRange& tr, double& ohms) {
  if (strncmp(buf, "jz", 2) != 0) return false;
  const char* p = buf + 2;
  if (*p == '\0') return false;
  static const struct { const char* s; TiaRange r; } tbl[] = {
    { "22",TIA_22 },{ "100",TIA_100 },{ "330",TIA_330 },{ "1k",TIA_1K },{ "3k3",TIA_3K3 },
    { "3.3k",TIA_3K3 },{ "10k",TIA_10K },{ "33k",TIA_33K },{ "100k",TIA_100K },
    { "330k",TIA_330K },{ "1m",TIA_1M },{ "1meg",TIA_1M },
  };
  for (auto& e : tbl) {
    size_t len = strlen(e.s);
    if (strncmp(p, e.s, len) == 0 && p[len] == '_') {
      char* end = nullptr;
      double v = strtod(p + len + 1, &end);
      if (end == p + len + 1 || v <= 0) return false;
      if (*end == 'k' || *end == 'K') v *= 1e3;
      else if (*end == 'm' || *end == 'M') v *= 1e6;
      else if (*end != '\0') return false;
      tr = e.r; ohms = v; return true;
    }
  }
  return false;
}

static void lcr_calib_status() {
  Serial.println("# CAL STATUS: range | valid | g | f_p(Hz) | tau(ns)");
  for (int i = 0; i < 10; i++) {
    const CalEntry& c = s_cal[i];
    if (!c.valid) { Serial.printf("# CAL [%s] -- not calibrated --\n", calib_range_name(i)); continue; }
    if (c.f_p > 1e8f)
      Serial.printf("# CAL [%s] g=%.4f f_p=POLE-FREE tau=%+.1f ns\n",
                    calib_range_name(i), c.g, c.tau * 1e9);
    else
      Serial.printf("# CAL [%s] g=%.4f f_p=%.1f Hz tau=%+.1f ns\n",
                    calib_range_name(i), c.g, c.f_p, c.tau * 1e9);
  }
}

// ====================================================================
// 三参数校准主流程
// ====================================================================
static bool lcr_calib_run(TiaRange tr, double r_std) {
  int range_idx = calib_range_to_idx(tr);
  if (range_idx < 0 || !(r_std > 0)) { Serial.println("# CAL ERR: bad range or r_std"); return false; }
  Serial.printf("# CAL START [%s] r_std=%.6g ohm\n", calib_range_name(range_idx), r_std);
  s_cal_bypass = true;

  lcr_set_tia_pin((int8_t)tr);
  double wre[NF], wim[NF]; int n = 0;
  for (int i = 0; i < NF; i++) {
    bool boost = (F[i] >= CAL_IGAIN_BOOST_ABOVE_HZ);
    lcr_set_ipin(boost ? CAL_IGAIN_BOOST_CODE : -1);
    MeasResult m = lcr_measure_point(F[i], false);
    ZDerived z;
    if (!lcr_derive_z(m, z) || !isfinite(z.z_re) || !isfinite(z.z_im)) {
      Serial.printf("# CAL f=%.4g: measure FAILED\n", F[i]); continue;
    }
    wre[n] = z.z_re / r_std; wim[n] = z.z_im / r_std;
    Serial.printf("# CAL f=%.4g: Z=(%.6g j%+.6g) W=(%.4f j%+.4f)\n",
                  F[i], z.z_re, z.z_im, wre[n], wim[n]);
    n++;
  }
  lcr_set_ipin(-1); lcr_set_tia_pin(-1);
  s_cal_bypass = false;
  if (n < CAL_N_MIN) { Serial.println("# CAL ERR: too few points"); return false; }

  int neg = 0;
  for (int i = 0; i < n; i++) if (wim[i] <= 0.0) neg++;
  if (neg >= 2) { Serial.printf("# CAL ERR: %d pts Im(W)<=0 — check wiring\n", neg); return false; }

  // ---- Pass 0: 粗估 f_p ----
  double num0 = 0, den0 = 0;
  for (int i = 0; i < n; i++) { num0 += wim[i]*F[i]*F[i]; den0 += wre[i]*F[i]; }
  double fp0 = (den0 > 0 && num0 > 0) ? num0 / den0 : 20000.0;
  if (!(fp0 > 100.0)) fp0 = 20000.0;

  // ---- pole-free 判定:最高频点 Im/Re 过小 → 无显著 C_f ----
  int ihb = 0;
  for (int i = 1; i < n; i++) if (F[i] > F[ihb]) ihb = i;
  bool pole_free = (wre[ihb] > 0) && (wim[ihb] / wre[ihb] < CAL_POLEFREE_RATIO);
  double f_p;
  if (pole_free) { f_p = 1e9; fp0 = 1e12; }   // fp0 抬高 → 所有点都视为"低频"

  // ---- 频点预筛:骑在极点二阶峰化区(α>0.7)上的点不进拟合 ----
  bool use_pt[NF]; int n_use = 0;
  for (int i = 0; i < n; i++) { use_pt[i] = (F[i] <= 0.7 * fp0); if (use_pt[i]) n_use++; }
  if (n_use < 3) { for (int i = 0; i < n; i++) use_pt[i] = true; n_use = n; }
  if (n_use < n)
    Serial.printf("# CAL: %d pt(s) above 0.7*f_p excluded (2nd-order region)\n", n - n_use);

  // ---- Pass 1: g ← 低频点 Re(W) 中位 ----
  double gbuf[NF]; int gn = 0;
  for (int i = 0; i < n; i++)
    if (use_pt[i] && F[i] < fp0 / 3.0) gbuf[gn++] = wre[i];
  if (gn == 0) for (int i = 0; i < n; i++) if (use_pt[i]) gbuf[gn++] = wre[i];
  double g = dmedian(gbuf, gn);

  // ---- Pass 2: f_p ← 逐点自洽估计中位(pole-free 跳过) ----
  double spread = 0; int fn = 0;
  if (!pole_free) {
    double fp_est[NF];
    for (int i = 0; i < n; i++) {
      if (!use_pt[i]) continue;
      double a = wre[i] / g, b = wim[i] / g;
      if (b > 1e-9) fp_est[fn++] = F[i] * a / b;
    }
    if (fn < 3) { Serial.println("# CAL ERR: f_p unobservable"); return false; }
    f_p = dmedian(fp_est, fn);
    double dev[NF];
    for (int i = 0; i < fn; i++) dev[i] = fabs(fp_est[i] - f_p) / f_p;
    for (int i = 0; i < fn; i++) spread += dev[i] * dev[i];
    spread = sqrt(spread / fn);
  }

  // ---- Pass 3: τ ← 剥掉 g/f_p 后残余相位,f² 加权 LS + 3σ 剔除一次 ----
  double phi[NF];
  for (int i = 0; i < n; i++) {
    double a = g, b = g * F[i] / f_p, m2 = a * a + b * b;
    double qre = (wre[i] * a + wim[i] * b) / m2;
    double qim = (wim[i] * a - wre[i] * b) / m2;
    phi[i] = atan2(qim, qre);
    Serial.printf("# CAL f=%.4g: residual phase %+.2f deg%s\n",
                  F[i], phi[i] * 180.0 / M_PI, use_pt[i] ? "" : "  [excluded]");
  }
  auto ls_fit = [&](int skip) {
    double num = 0, den = 0;
    for (int i = 0; i < n; i++) {
      if (i == skip || !use_pt[i]) continue;
      num += phi[i] * F[i];
      den += 2.0 * M_PI * F[i] * F[i];
    }
    return (den > 0) ? num / den : 0.0;
  };
  double tau = ls_fit(-1);
  int worst = -1; double worst_r = 0, srms = 0, wsum = 0;
  for (int i = 0; i < n; i++) {
    if (!use_pt[i]) continue;
    double r = phi[i] - 2.0 * M_PI * F[i] * tau, w = F[i] * F[i];
    srms += w * r * r; wsum += w;
    if (fabs(r) > worst_r) { worst_r = fabs(r); worst = i; }
  }
  double sigma = sqrt(srms / wsum);
  if (worst >= 0 && worst_r > 3.0 * sigma && n >= 4) {
    Serial.printf("# CAL: outlier @f=%.4g (resid %.2f deg > 3sigma %.2f deg) dropped\n",
                  F[worst], worst_r * 180.0 / M_PI, sigma * 180.0 / M_PI);
    tau = ls_fit(worst);
  }

  // ---- tau 一致性检查:区分"真实时延"与"单点污染" ----
  if (!pole_free && fabs(tau) > 1e-7) {
    double tmin = 1e9, tmax = 0;
    for (int i = 0; i < n; i++) {
      if (!use_pt[i] || F[i] < 1000.0) continue;
      double ti = fabs(phi[i] / (2.0 * M_PI * F[i]));
      if (ti < tmin) tmin = ti;
      if (ti > tmax) tmax = ti;
    }
    if (tmax > 5e-8 && tmin > 0 && tmax / tmin > 3.0) {
      Serial.printf("# CAL: tau inconsistent across points (%.0f~%.0f ns)"
                    " -> forced 0 (single-point contamination)\n", tmin * 1e9, tmax * 1e9);
      tau = 0.0;
    }
  }

  // ---- 结果打印 ----
  if (pole_free)
    Serial.printf("# CAL [%s] RESULT: g=%.4f (%+.2f%%) f_p=POLE-FREE tau=%+.1f ns\n",
                  calib_range_name(range_idx), g, (g - 1) * 100, tau * 1e9);
  else
    Serial.printf("# CAL [%s] RESULT: g=%.4f (%+.2f%%) f_p=%.1f Hz"
                  " (spread %.1f%%, %d pts) tau=%+.1f ns\n",
                  calib_range_name(range_idx), g, (g - 1) * 100, f_p, spread * 100, fn, tau * 1e9);

  // ---- 验收门槛 ----
  bool ok = true;
  if (g < CAL_G_MIN || g > CAL_G_MAX) { Serial.println("# CAL ERR: g out of range"); ok = false; }
  if (pole_free) {
    Serial.println("# CAL NOTE: pole-free range (Im/Re@10k < 0.03) -> f_p stored 1e9,"
                   " correction bypasses pole term");
  } else {
    if (f_p < CAL_FP_MIN_HZ) { Serial.println("# CAL ERR: f_p < 5kHz — C_f too large / not C0G?"); ok = false; }
    else if (f_p > CAL_FP_MAX_HZ) { Serial.println("# CAL ERR: f_p > 500kHz"); ok = false; }
    else if (f_p < 10000.0) Serial.println("# CAL WARN: f_p <10kHz, in-band correction will be large");
    if (spread > CAL_SPREAD_MAX) { Serial.println("# CAL ERR: f_p spread too large"); ok = false; }
  }
  if (fabs(tau) > CAL_TAU_WARN_S)
    Serial.printf("# CAL WARN: |tau|>%.1f us — real inter-range delay?\n", CAL_TAU_WARN_S * 1e6);
  if (!ok) return false;

  lcr_calib_save(range_idx, (float)g, (float)f_p, (float)tau);
  Serial.printf("# CAL saved (range idx %d, NVS 'lcr-cal')\n", range_idx);
  return true;
}

// ====================================================================
// 备份/恢复
// ====================================================================
static void lcr_calib_dump() {
  Serial.println("# ---- paste these lines to restore calibration ----");
  for (int i = 0; i < 10; i++) {
    const CalEntry& c = s_cal[i];
    if (c.valid) Serial.printf("# jzset%d_%.6g_%.6g_%.6g\n", i, c.g, c.f_p, c.tau);
  }
}

static bool lcr_calib_parse_jzset(const char* buf) {
  if (strncmp(buf, "jzset", 5) != 0) return false;
  char* end = nullptr;
  long idx = strtol(buf + 5, &end, 10);
  if (idx < 0 || idx >= 10 || *end != '_') {
    Serial.println("# ERR: use jzset<idx>_<g>_<fp>_<tau>"); return true;
  }
  double g = strtod(end + 1, &end);
  double fp = 0, tau = 0;
  if (*end == '_') fp = strtod(end + 1, &end);
  if (*end == '_') tau = strtod(end + 1, &end);
  if (!(g > 0.5 && g < 2.0) || !(fp > 0) || fabs(tau) > 1e-5) {
    Serial.println("# ERR: jzset values out of range"); return true;
  }
  lcr_calib_save((int)idx, (float)g, (float)fp, (float)tau);
  Serial.printf("# CAL restored idx%d g=%.4f f_p=%.4g tau=%.4g s\n", (int)idx, g, fp, tau);
  return true;
}

static void lcr_os_dump() {
  Serial.println("# ---- paste these lines to restore open/short tables ----");
  for (int i = 0; i < OS_NF; i++)
    if (s_os_open.valid)
      Serial.printf("# osseto_%d_%.7g_%.7g\n", i, s_os_open.re[i], s_os_open.im[i]);
  for (int i = 0; i < OS_NF; i++)
    if (s_os_short.valid)
      Serial.printf("# ossets_%d_%.7g_%.7g\n", i, s_os_short.re[i], s_os_short.im[i]);
}

static bool lcr_os_parse_set(const char* buf) {
  bool is_open;
  if      (!strncmp(buf, "osseto_", 7)) is_open = true;
  else if (!strncmp(buf, "ossets_", 7)) is_open = false;
  else return false;
  char* end = nullptr;
  long i = strtol(buf + 7, &end, 10);
  if (i < 0 || i >= OS_NF || *end != '_') {
    Serial.println("# ERR: use osset<o|s>_<i>_<re>_<im>"); return true;
  }
  double re = strtod(end + 1, &end), im = 0;
  if (*end == '_') im = strtod(end + 1, &end);
  OsTable& t = is_open ? s_os_open : s_os_short;
  t.re[i] = re; t.im[i] = im; t.valid = true;
  lcr_os_save();
  Serial.printf("# OS restored %s pt%d Z=(%.6g j%+.6g)\n",
                is_open ? "OPEN" : "SHORT", (int)i, re, im);
  return true;
}

// ====================================================================
// 开/短路表征:7 频点,自动量程,链路修正照常(用当时挡位的 g/f_p/tau),
// OS 补偿旁路防止自污染。存 Z(开路)/Z(短路) 复数表
// ====================================================================
static bool lcr_os_run(bool open) {
  s_os_bypass = true;
  Serial.printf("# OS CAL START: %s, %d freqs %.4g..%.4g Hz (autorange, ~1 min)\n",
                open ? "OPEN (remove DUT!)" : "SHORT (short the terminals!)",
                OS_NF, OS_F[0], OS_F[OS_NF - 1]);
  OsTable t; t.valid = true;
  int n_ok = 0;
  for (int i = 0; i < OS_NF; i++) {
    MeasResult m = lcr_measure_point(OS_F[i], false);
    ZDerived z;
    if (!lcr_derive_z(m, z) || !isfinite(z.z_re) || !isfinite(z.z_im)) {
      Serial.printf("# OS f=%.4g: measure FAILED\n", OS_F[i]);
      t.re[i] = t.im[i] = 0; continue;
    }
    t.re[i] = z.z_re; t.im[i] = z.z_im; n_ok++;
    Serial.printf("# OS f=%.4g TIA=%s: Z=(%.6g j%+.6g)\n",
                  OS_F[i], TIA_RANGE_NAME[g_tia_range], z.z_re, z.z_im);
  }
  s_os_bypass = false;
  if (n_ok < OS_NF - 1) { Serial.println("# OS ERR: too few points"); return false; }

  // ---- 合理性检查 ----
  if (open) {
    double re = t.re[4], im = t.im[4];       // 1 kHz
    if (im >= 0) { Serial.println("# OS WARN: open Im(Z) not capacitive — fixture odd?"); }
    else {
      double yim = -im / (re*re + im*im);
      double c_pF = yim / (2*M_PI*1000.0) * 1e12;
      Serial.printf("# OS: C_open@1k = %.3f pF %s\n", c_pF,
                    (c_pF > 0.1 && c_pF < 10.0) ? "ok" : "<<< SUSPECT");
    }
  } else {
    double re = t.re[4], im = t.im[4];
    double zm = sqrt(re*re + im*im);
    Serial.printf("# OS: |Z_short|@1k = %.4f ohm %s\n", zm, (zm < 2.0) ? "ok" : "<<< SUSPECT");
  }

  if (open) s_os_open = t; else s_os_short = t;
  lcr_os_save();
  Serial.printf("# OS saved (NVS '%s')\n", OS_NAMESPACE);
  for (int i = 0; i < OS_NF; i++)
    Serial.printf("# OS_DUMP: osset%c%d_%.7g_%.7g\n", open ? 'o' : 's', i, t.re[i], t.im[i]);
  return true;
}
