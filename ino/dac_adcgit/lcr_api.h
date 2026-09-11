#pragma once
/**
 * lcr_api.h — LCR 表统一 C 接口层
 * 约定：
 *  - 返回值 int：>=0 成功（含义见函数注释），<0 为 LcrApiStatus 错误码
 *  - 所有频率 double / Hz
 *  - 结果通过出参结构体或数组返回
 *  - 诊断打印受 g_lcr_diag 开关控制（lcr_api_set_diagnostics），
 *    关闭时测量路径完全静默；校准回执与 # ERR 错误行不受开关控制
 */

#include <Arduino.h>
#include "lcr_diag.h"      // ★ 必须最先：g_lcr_diag 对后续所有底层头文件可见
#include "freq_calc.h"
#include "lcr_adc.h"
#include "lcr_tone.h"
#include "lcr_measure.h"
#include "lcr_calib.h"

// ================== 错误码 ==================
enum LcrApiStatus : int {
    LCR_API_OK                    =  0,
    LCR_API_ERR_PARAM             = -1,
    LCR_API_ERR_BUF_TOO_SMALL     = -2,
    LCR_API_ERR_MEASURE           = -3,
    LCR_API_ERR_MEASURE_ALL       = -4,
    LCR_API_ERR_CAL               = -5,
    LCR_API_ERR_FREQ_OUT_OF_RANGE = -6,
    LCR_API_ERR_FREQ_NO_FIT       = -7,
    LCR_API_ERR_RESTORE_EMPTY     = -8,
};

// ================== 数据结构 ==================
typedef struct {
    double f_req, f_act;
    double z_re, z_im, z_mag, phi_z_deg;
    double D, Q;
    char   type;          // 'R' / 'C' / 'L'
} LcrZPoint;

typedef struct {
    double f_req, f_act;
    double h_mag, h_db, phase_deg;
} LcrWPoint;

typedef struct {
    double f;
    double z_re, z_im, z_mag, phi_z_deg;
    double D, Q;
    char   type;
    double rs, cs, ls;
    double rp, cp, lp;
} LcrCalcResult;

typedef struct {
    bool  valid;
    float g;
    float f_p;            // >=1e8 表示 pole-free 挡
    float tau;
} LcrCalParams;

enum LcrCalItem : int8_t {
    LCR_CAL_OPEN  = -2,
    LCR_CAL_SHORT = -1,
    LCR_CAL_22    = 0,  LCR_CAL_100 = 1,  LCR_CAL_330  = 2,  LCR_CAL_1K  = 3,
    LCR_CAL_3K3   = 4,  LCR_CAL_10K = 5,  LCR_CAL_33K  = 6,  LCR_CAL_100K= 7,
    LCR_CAL_330K  = 8,  LCR_CAL_1M  = 9,
};

typedef struct {
    LcrCalParams range[10];
    bool open_valid;
    bool short_valid;
} LcrCalStatus;

// ================== 诊断开关 ==================
/** true = 打开全部诊断打印（含 set_freq 的 pts/div/实际频率/误差 4 行）；false = 测量路径零输出 */
inline void lcr_api_set_diagnostics(bool on) { g_lcr_diag = on; }
inline bool lcr_api_diagnostics_enabled()    { return g_lcr_diag; }

// ================== 0. 初始化 ==================
inline bool lcr_api_init() {
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0 < 3000)) delay(1);
    Serial.setTxBufferSize(8192);
    if (!lcr_adc_init())     return false;
    lcr_measure_init();
    lcr_calib_load();
    lcr_os_load();
    if (g_lcr_diag) lcr_calib_status();          // ★ 门控
    if (!init_wave_buf())    return false;
    init_lcd_cam_dma();
    if (g_lcr_diag) lcr_adc_print_nominal_rate();// ★ 门控
    lcr_wiring_selfcheck();                      // 内部已按 g_lcr_diag 门控
    if (g_lcr_diag) Serial.println("# DIAG ON");
    return true;
}

// ================== 1. 复位 ==================
inline void lcr_api_reset() {
    stop_sin();
    g_fast_settle     = false;
    g_range_settle_ms = RANGE_SETTLE_MS;
    g_tia_pin         = (TiaRange)0xFF;
    g_ipin            = (CurrentRange)0xFF;
    g_w_mode          = false;
    s_cal_bypass      = false;
    s_os_bypass       = false;
    lcr_reset_all_ranges();
}

// ================== 2. 频率输出 ==================
inline double lcr_api_set_freq(double f) {
    if (f <= 0.0) { stop_sin(); g_sig_freq = 0.0; return 0.0; }
    double fa = out_freq(f, 20, 20);
    if (fa > 0.0) { g_sig_freq = fa; return fa; }
    return (fa == -1.0) ? (double)LCR_API_ERR_FREQ_OUT_OF_RANGE
                        : (double)LCR_API_ERR_FREQ_NO_FIT;
}

// ================== 3. 单频单口 ==================
inline int lcr_api_measure_z(double f, LcrZPoint *out) {
    if (!out || !(f > 0.0)) return LCR_API_ERR_PARAM;
    MeasResult r = lcr_measure_point(f, false);
    ZDerived z;
    if (!lcr_derive_z(r, z)) return LCR_API_ERR_MEASURE;
    out->f_req = f;       out->f_act = z.f;
    out->z_re = z.z_re;   out->z_im = z.z_im;
    out->z_mag = z.z_mag; out->phi_z_deg = z.phi_z_deg;
    out->D = z.D;         out->Q = z.Q;   out->type = z.type;
    return LCR_API_OK;
}

// ================== 4. 单频双口 ==================
inline int lcr_api_measure_w(double f, LcrWPoint *out) {
    if (!out || !(f > 0.0)) return LCR_API_ERR_PARAM;
    WPoint wp;
    if (!lcr_w_measure(f, wp, false)) return LCR_API_ERR_MEASURE;
    out->f_req = f;        out->f_act = wp.f;
    out->h_mag = wp.h_mag; out->h_db = wp.h_db; out->phase_deg = wp.phase_deg;
    return LCR_API_OK;
}

// ================== 5. 多频单口扫频 ==================
inline int lcr_api_sweep_z(double f_start, double f_end, int n_points,
                           LcrZPoint *out, int cap) {
    if (!out || n_points < 2) return LCR_API_ERR_PARAM;
    if (cap < n_points)       return LCR_API_ERR_BUF_TOO_SMALL;
    if (!(f_start > 0.0) || !(f_end > 0.0)) return LCR_API_ERR_PARAM;
    lcr_set_fast_settle(true);
    int n_ok = 0;
    for (int i = 0; i < n_points; i++) {
        double f = f_start * pow(f_end / f_start, (double)i / (double)(n_points - 1));
        LcrZPoint &p = out[i];
        if (lcr_api_measure_z(f, &p) == LCR_API_OK) n_ok++;
        else { p.f_req = f; p.f_act = f; p.z_re = p.z_im = p.z_mag = NAN;
               p.phi_z_deg = p.D = p.Q = NAN; p.type = 'E'; }
    }
    lcr_set_fast_settle(false);
    return (n_ok > 0) ? n_ok : LCR_API_ERR_MEASURE_ALL;
}

// ================== 6. 多频双口扫频 ==================
inline int lcr_api_sweep_w(double f_start, double f_end, int n_points,
                           LcrWPoint *out, int cap) {
    if (!out || n_points < 2) return LCR_API_ERR_PARAM;
    if (cap < n_points)       return LCR_API_ERR_BUF_TOO_SMALL;
    if (!(f_start > 0.0) || !(f_end > 0.0)) return LCR_API_ERR_PARAM;
    lcr_set_fast_settle(true);
    int n_ok = 0;
    for (int i = 0; i < n_points; i++) {
        double f = f_start * pow(f_end / f_start, (double)i / (double)(n_points - 1));
        LcrWPoint &p = out[i];
        if (lcr_api_measure_w(f, &p) == LCR_API_OK) n_ok++;
        else { p.f_req = f; p.f_act = f; p.h_mag = p.h_db = p.phase_deg = NAN; }
    }
    lcr_set_fast_settle(false);
    return (n_ok > 0) ? n_ok : LCR_API_ERR_MEASURE_ALL;
}

// ================== 7. 纯数学换算 ==================
inline int lcr_api_calc(double f, double z_re, double z_im,
                        bool apply_calib, LcrCalcResult *out) {
    if (!out || !(f > 0.0)) return LCR_API_ERR_PARAM;
    double zre = z_re, zim = z_im;
    if (apply_calib) {
        lcr_calib_correct(zre, zim, f, (int)g_tia_range);
        lcr_os_correct(zre, zim, f);
    }
    double d2 = zre * zre + zim * zim;
    double w  = 2.0 * M_PI * f;
    out->f = f;
    out->z_re = zre;  out->z_im = zim;
    out->z_mag = (d2 > 0.0) ? sqrt(d2) : 0.0;
    out->phi_z_deg = atan2(zim, zre) * 180.0 / M_PI;
    double G = (d2 > 1e-30) ?  zre / d2 : 0.0;
    double B = (d2 > 1e-30) ? -zim / d2 : 0.0;
    out->D = (fabs(B) > 1e-30) ? fabs(G / B) : 1e30;
    out->Q = (out->D > 1e-30) ? 1.0 / out->D : 1e30;
    out->rs = zre;
    out->cs = (w > 0.0 && zim < 0.0) ? -1.0 / (w * zim) : NAN;
    out->ls = (w > 0.0 && zim > 0.0) ?  zim / w        : NAN;
    out->rp = (G > 1e-15) ? 1.0 / G : 1e15;
    out->cp = (w > 0.0 && B > 0.0) ?  B / w        : NAN;
    out->lp = (w > 0.0 && B < 0.0) ? -1.0 / (w * B) : NAN;
    double phi_y = atan2(B, G) * 180.0 / M_PI;
    if (fabs(phi_y) < 1.0) out->type = 'R';
    else if (B > 0.0)      out->type = 'C';
    else                   out->type = 'L';
    return LCR_API_OK;
}

// ================== 8. 校准 ==================
inline int lcr_api_calibrate(LcrCalItem item, double std_ohms, LcrCalParams *out) {
    if (out) { out->valid = false; out->g = out->f_p = out->tau = 0.0f; }
    bool ok;
    if (item == LCR_CAL_OPEN)       ok = lcr_os_run(true);
    else if (item == LCR_CAL_SHORT) ok = lcr_os_run(false);
    else {
        if (item < LCR_CAL_22 || item > LCR_CAL_1M) return LCR_API_ERR_PARAM;
        if (!(std_ohms > 0.0)) return LCR_API_ERR_PARAM;
        ok = lcr_calib_run((TiaRange)(int)item, std_ohms);
    }
    if (!ok) return LCR_API_ERR_CAL;
    if (out) {
        if (item >= 0 && s_cal[item].valid) {
            out->valid = true;
            out->g = s_cal[item].g; out->f_p = s_cal[item].f_p; out->tau = s_cal[item].tau;
        } else if (item == LCR_CAL_OPEN)  out->valid = s_os_open.valid;
        else if (item == LCR_CAL_SHORT)   out->valid = s_os_short.valid;
    }
    return LCR_API_OK;
}

// ================== 9. 校准状态 ==================
inline int lcr_api_cal_status(LcrCalStatus *out) {
    if (!out) return LCR_API_ERR_PARAM;
    for (int i = 0; i < 10; i++) {
        out->range[i].valid = s_cal[i].valid;
        out->range[i].g   = s_cal[i].g;
        out->range[i].f_p = s_cal[i].f_p;
        out->range[i].tau = s_cal[i].tau;
    }
    out->open_valid  = s_os_open.valid;
    out->short_valid = s_os_short.valid;
    return LCR_API_OK;
}

// ================== 10. 备份打印 ==================
inline void lcr_api_cal_dump() {
    lcr_calib_dump();
    lcr_os_dump();
}

// ================== 11. 串口恢复备份 ==================
inline int lcr_api_cal_restore(uint32_t timeout_ms = 20000, Stream *port = &Serial) {
    char line[96]; size_t len = 0; int n = 0;
    uint32_t t0 = millis();
    bool done = false;
    while (!done && (millis() - t0 < timeout_ms)) {
        while (port->available()) {
            char c = (char)port->read();
            if (c == '\n' || c == '\r') {
                if (len == 0) continue;
                line[len] = '\0'; len = 0;
                char *p = line;
                while (*p == ' ') p++;
                if (*p == '#') { p++; while (*p == ' ') p++; }
                if (!strncasecmp(p, "END", 3)) { done = true; break; }
                if (lcr_calib_parse_jzset(p) || lcr_os_parse_set(p)) n++;
            } else if (len < sizeof(line) - 1) line[len++] = c;
            t0 = millis();
        }
        delay(2);
    }
    return (n > 0) ? n : LCR_API_ERR_RESTORE_EMPTY;
}

// ================== 附加接口 ==================
inline int lcr_api_set_ranges(int8_t v_code, int8_t i_code, int8_t tia_idx) {
    if (v_code >= 0)  { if (v_code > 3 || !lcr_set_voltage_range((VoltageRange)v_code)) return LCR_API_ERR_PARAM; }
    if (i_code >= 0)  { if (i_code > 3 || !lcr_set_current_range((CurrentRange)i_code)) return LCR_API_ERR_PARAM; }
    if (tia_idx >= 0) { if (tia_idx >= TIA_RANGE_COUNT || !lcr_set_tia_range((TiaRange)tia_idx)) return LCR_API_ERR_PARAM; }
    return LCR_API_OK;
}

inline void lcr_api_selfcheck() { lcr_wiring_selfcheck(); }
inline void lcr_api_print_sample_rate() { if (g_lcr_diag) lcr_adc_print_nominal_rate(); }

inline void lcr_api_get_ranges(int8_t *v_code, int8_t *i_code, int8_t *tia_idx) {
    if (v_code)  *v_code  = (int8_t)g_voltage_range;
    if (i_code)  *i_code  = (int8_t)g_current_range;
    if (tia_idx) *tia_idx = (int8_t)g_tia_range;
}
