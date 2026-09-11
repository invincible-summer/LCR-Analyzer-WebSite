#ifndef LCR_CALIB_CORE_H
#define LCR_CALIB_CORE_H

#include <Arduino.h>
#include <Preferences.h>

#define CAL_MAX_RANGES 16
#define CAL_NAMESPACE "lcr-cal"
#define OS_NAMESPACE  "lcr-os"
#define OS_NF 7
// 开/短路表征频点(与 AUTO 扫频网格一致)
static const double OS_F[OS_NF] = {10.0, 31.6228, 100.0, 316.228, 1000.0, 3162.28, 10000.0};

struct CalEntry { float g; float f_p; float tau; bool valid; };
inline CalEntry s_cal[CAL_MAX_RANGES];

// 校准采集期间旁路链路修正(重校已校挡位时防止自污染)
static bool s_cal_bypass = false;

// ---- 开/短路补偿表(夹具寄生,全挡共享,按频点存复数 Z) ----
struct OsTable { double re[OS_NF], im[OS_NF]; bool valid; };
inline OsTable s_os_open, s_os_short;
static bool s_os_bypass = false;   // jzopen/jzshort 采集期间旁路 OS 补偿

// ====================================================================
// 三参数链路修正。f_p >= 1e8 视为 pole-free 挡(如 22Ω 未并 C_f):
// 跳过极点项只除 g,tau 照常修正
// ====================================================================
static inline void lcr_calib_correct(double &zre, double &zim, double f, int range_idx)
{
  if (s_cal_bypass) return;
  if (range_idx < 0 || range_idx >= CAL_MAX_RANGES) return;
  const CalEntry &c = s_cal[range_idx];
  if (!c.valid || c.g <= 0.0f) return;

  if (c.f_p > 0.0f && c.f_p < 1e8f) {
    double a = c.g, b = c.g * f / c.f_p;
    double mag2 = a * a + b * b;
    double re = (zre * a + zim * b) / mag2;
    double im = (zim * a - zre * b) / mag2;
    zre = re; zim = im;
  } else {
    zre /= c.g; zim /= c.g;              // pole-free: 只有增益项
  }
  if (c.tau != 0.0f) {
    double phi = 2.0 * M_PI * f * (double)c.tau;
    double co = cos(phi), si = sin(phi);
    double re2 =  zre * co + zim * si;   // 乘 e^(-jφ)
    double im2 =  zim * co - zre * si;
    zre = re2; zim = im2;
  }
}

// ---- OS 频点插值(log-f 线性,越界取端点) ----
static inline void s_os_interp(const OsTable &t, double f, double &re, double &im)
{
  if (f <= OS_F[0])            { re = t.re[0]; im = t.im[0]; return; }
  if (f >= OS_F[OS_NF - 1])    { re = t.re[OS_NF-1]; im = t.im[OS_NF-1]; return; }
  for (int i = 0; i < OS_NF - 1; i++) {
    if (f >= OS_F[i] && f <= OS_F[i+1]) {
      double u = (log(f) - log(OS_F[i])) / (log(OS_F[i+1]) - log(OS_F[i]));
      re = t.re[i] + u * (t.re[i+1] - t.re[i]);
      im = t.im[i] + u * (t.im[i+1] - t.im[i]);
      return;
    }
  }
  re = t.re[0]; im = t.im[0];
}

// ====================================================================
// 开/短路补偿:在链路修正之后、Y 反推之前调用。
// Open 在 Y 域减(加性并联寄生,含漏电,按频点);Short 在 Z 域减(串联 R/L)
// ====================================================================
static inline void lcr_os_correct(double &zre, double &zim, double f)
{
  if (s_os_bypass || s_cal_bypass) return;
    if (s_os_open.valid) {
    double ore, oim; s_os_interp(s_os_open, f, ore, oim);
    double den = zre * zre + zim * zim;
    if (den > 1e-30) {
      double yre =  zre / den, yim = -zim / den;
      double od = ore * ore + oim * oim;          // ★ 修复:Z_open → Y_open
      if (od > 1e-30) {
        yre -= ore / od;                          // Y_open_re = Re(Z)/|Z|²
        yim -= -oim / od;                         // Y_open_im = -Im(Z)/|Z|²
        double d2 = yre * yre + yim * yim;
        if (d2 > 1e-24) { zre = yre / d2; zim = -yim / d2; }
      }
    }
  }

  if (s_os_short.valid) {
    double sre, sim; s_os_interp(s_os_short, f, sre, sim);
    zre -= sre; zim -= sim;
  }
}

inline void lcr_calib_load()
{
  for (int i = 0; i < CAL_MAX_RANGES; i++) s_cal[i] = {0, 0, 0, false};
  Preferences p;
  if (!p.begin(CAL_NAMESPACE, true)) return;
  char k[12]; int n = 0;
  for (int i = 0; i < CAL_MAX_RANGES; i++) {
    snprintf(k, sizeof(k), "g%d", i);
    if (p.isKey(k)) {
      s_cal[i].g = p.getFloat(k, 0);
      snprintf(k, sizeof(k), "fp%d", i);  s_cal[i].f_p = p.getFloat(k, 0);
      snprintf(k, sizeof(k), "tau%d", i); s_cal[i].tau = p.getFloat(k, 0);
      s_cal[i].valid = (s_cal[i].g > 0 && s_cal[i].f_p > 0);
      if (s_cal[i].valid) n++;
    }
  }
  p.end();
  Serial.printf("# cal: %d range(s) loaded from NVS\n", n);
}

inline void lcr_calib_save(int idx, float g, float f_p, float tau)
{
  Preferences p;
  if (!p.begin(CAL_NAMESPACE, false)) { Serial.println("# ERR: NVS open failed"); return; }
  char k[12];
  snprintf(k, sizeof(k), "g%d", idx);   p.putFloat(k, g);
  snprintf(k, sizeof(k), "fp%d", idx);  p.putFloat(k, f_p);
  snprintf(k, sizeof(k), "tau%d", idx); p.putFloat(k, tau);
  p.end();
  s_cal[idx] = {g, f_p, tau, true};
  // ★ 自动备份行:每次存档把可回贴的恢复命令直接打进日志
  Serial.printf("# CAL_DUMP: jzset%d_%.6g_%.6g_%.6g\n", idx, g, f_p, tau);
}

inline void lcr_calib_clear_all()
{
  Preferences p; p.begin(CAL_NAMESPACE, false); p.clear(); p.end();
  for (int i = 0; i < CAL_MAX_RANGES; i++) s_cal[i] = {0, 0, 0, false};
  Serial.println("# cal: all calibration entries cleared");
}

// ---- 开/短路表 NVS 读写 ----
inline void lcr_os_load()
{
  s_os_open.valid = s_os_short.valid = false;
  Preferences p;
  if (!p.begin(OS_NAMESPACE, true)) return;
  char k[12];
  if (p.isKey("ore0")) {
    for (int i = 0; i < OS_NF; i++) {
      snprintf(k, sizeof(k), "ore%d", i); s_os_open.re[i] = p.getFloat(k, 0);
      snprintf(k, sizeof(k), "oim%d", i); s_os_open.im[i] = p.getFloat(k, 0);
    }
    s_os_open.valid = true;
  }
  if (p.isKey("sre0")) {
    for (int i = 0; i < OS_NF; i++) {
      snprintf(k, sizeof(k), "sre%d", i); s_os_short.re[i] = p.getFloat(k, 0);
      snprintf(k, sizeof(k), "sim%d", i); s_os_short.im[i] = p.getFloat(k, 0);
    }
    s_os_short.valid = true;
  }
  p.end();
  if (s_os_open.valid) {
    double re = s_os_open.re[4], im = s_os_open.im[4];      // 1 kHz 点
    double c_pF = 0;
    if (im < 0) { double yim = -im / (re*re + im*im); c_pF = yim / (2*M_PI*1000.0) * 1e12; }
    Serial.printf("# os boot: OPEN loaded, C_open@1k ≈ %.3f pF\n", c_pF);
  }
  if (s_os_short.valid) {
    double re = s_os_short.re[4], im = s_os_short.im[4];
    Serial.printf("# os boot: SHORT loaded, |Z|@1k = %.4f ohm\n", sqrt(re*re + im*im));
  }
}

inline void lcr_os_save()
{
  Preferences p;
  if (!p.begin(OS_NAMESPACE, false)) { Serial.println("# ERR: NVS open failed"); return; }
  char k[12];
  for (int i = 0; i < OS_NF; i++) {
    snprintf(k, sizeof(k), "ore%d", i); p.putFloat(k, (float)s_os_open.re[i]);
    snprintf(k, sizeof(k), "oim%d", i); p.putFloat(k, (float)s_os_open.im[i]);
    snprintf(k, sizeof(k), "sre%d", i); p.putFloat(k, (float)s_os_short.re[i]);
    snprintf(k, sizeof(k), "sim%d", i); p.putFloat(k, (float)s_os_short.im[i]);
  }
  p.end();
}

#endif
