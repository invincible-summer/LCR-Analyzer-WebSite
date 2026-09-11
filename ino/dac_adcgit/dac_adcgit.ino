/**
 * ESP32-S3 LCR 表 — 主程序（API 版 + GPIO4 诊断开关）
 * GPIO4 上拉输入：高电平（悬空或接 3.3V）= 开诊断；接地 = 关诊断。
 * 诊断在 lcr_api_init() 之前配置，上电默认关闭（g_lcr_diag 初始 false）。
 */
#include <Arduino.h>
#include "lcr_api.h"

#define DIAG_PIN 4

#define LINE_BUF_LEN 64
#define FREQ_MIN_HZ 10.0
#define FREQ_MAX_HZ 20800.0
#define SWEEP_MAX_POINTS 50

static char s_line[LINE_BUF_LEN];
static size_t s_line_len = 0;

// ---- 结果打印辅助（结果=数据，不受诊断开关控制）----
static void print_z(const LcrZPoint &p) {
    if (p.type == 'E') { Serial.printf("# ERR: f=%.4g Hz amplitude invalid\n", p.f_req); return; }
    Serial.printf("# RESULT_Z a_ohm=%.6g b_ohm=%+.6g f_act=%.4f D=%.4g Q=%.4g type=%c\n",
                  p.z_re, p.z_im, p.f_act, p.D, p.Q, p.type);
    LcrCalcResult c;
    lcr_api_calc(p.f_act, p.z_re, p.z_im, false, &c);
    if (c.type == 'R')
        Serial.printf("# RESULT_LCR type=R R_ohm=%.6g D=%.4g\n", c.rs, c.D);
    else if (c.type == 'C')
        Serial.printf("# RESULT_LCR type=C Rp_ohm=%.6g C_F=%.6g Rs_ohm=%.6g Cs_F=%.6g D=%.4g\n",
                      c.rp, c.cp, c.rs, c.cs, c.D);
    else
        Serial.printf("# RESULT_LCR type=L Rs_ohm=%.6g L_H=%.6g Rp_ohm=%.6g Lp_H=%.6g D=%.4g\n",
                      c.rs, c.ls, c.rp, c.lp, c.D);
}

static void print_w(const LcrWPoint &p) {
    if (isnan(p.h_mag)) { Serial.printf("# W f=%.4g ERR\n", p.f_req); return; }
    Serial.printf("# RESULT_W f=%.4f H=%.6g (%.4g dB) phase=%+.3f deg\n",
                  p.f_act, p.h_mag, p.h_db, p.phase_deg);
}

static void print_sweep_z(const LcrZPoint *tab, int n) {
    Serial.println("# ================ AUTO SWEEP RESULT ================");
    Serial.printf("# %-9s:", "f_Hz");
    for (int i = 0; i < n; i++) Serial.printf(" %.4g", tab[i].f_act);
    Serial.println();
    Serial.printf("# %-9s:", "type");
    for (int i = 0; i < n; i++) Serial.printf(" %c", tab[i].type);
    Serial.println();
    Serial.printf("# %-9s:", "Zre_ohm");
    for (int i = 0; i < n; i++) Serial.printf(" %.4g", tab[i].z_re);
    Serial.println();
    Serial.printf("# %-9s:", "Zim_ohm");
    for (int i = 0; i < n; i++) Serial.printf(" %+.4g", tab[i].z_im);
    Serial.println();
    Serial.printf("# %-9s:", "D_tand");
    for (int i = 0; i < n; i++) Serial.printf(" %.4g", tab[i].D);
    Serial.println();
    Serial.println("# ===================================================");
}

static void print_sweep_w(const LcrWPoint *tab, int n) {
    Serial.println("# ================ W MODE BODE RESULT ================");
    Serial.printf("# %-9s:", "f_Hz");
    for (int i = 0; i < n; i++) Serial.printf(" %.4g", tab[i].f_act);
    Serial.println();
    Serial.printf("# %-9s:", "H_dB");
    for (int i = 0; i < n; i++) Serial.printf(" %.4g", tab[i].h_db);
    Serial.println();
    Serial.printf("# %-9s:", "phase_deg");
    for (int i = 0; i < n; i++) Serial.printf(" %+.3f", tab[i].phase_deg);
    Serial.println();
    Serial.println("# ===================================================");
}

static bool parse_tia_range(const char *s, LcrCalItem &item) {
    static const struct { const char *s; LcrCalItem it; } tbl[] = {
        {"22",LCR_CAL_22},{"100",LCR_CAL_100},{"330",LCR_CAL_330},{"1k",LCR_CAL_1K},
        {"3k3",LCR_CAL_3K3},{"3.3k",LCR_CAL_3K3},{"10k",LCR_CAL_10K},{"33k",LCR_CAL_33K},
        {"100k",LCR_CAL_100K},{"330k",LCR_CAL_330K},{"1m",LCR_CAL_1M},{"1meg",LCR_CAL_1M},
    };
    for (auto &e : tbl) {
        size_t l = strlen(e.s);
        if (strncmp(s, e.s, l) == 0) { item = e.it; return true; }
    }
    return false;
}

static void handle_line(const char *raw) {
    if (raw[0] == '\0' || raw[0] == '#') return;
    char buf[LINE_BUF_LEN];
    strncpy(buf, raw, LINE_BUF_LEN - 1); buf[LINE_BUF_LEN - 1] = '\0';
    for (char *p = buf; *p; ++p) *p = (char)tolower((int)*p);

    if (!strncmp(buf, "auto", 4)) {
        int n = 20;
        if (buf[4]) { long v = strtol(buf + 4, nullptr, 10);
                      if (v >= 2 && v <= SWEEP_MAX_POINTS) n = (int)v; }
        static LcrZPoint tab[SWEEP_MAX_POINTS];
        int got = lcr_api_sweep_z(10.0, 10000.0, n, tab, SWEEP_MAX_POINTS);
        if (got >= 0) { print_sweep_z(tab, n); for (int i = 0; i < n; i++) print_z(tab[i]); }
        else Serial.printf("# ERR: sweep failed (%d)\n", got);
    }
    else if (!strncmp(buf, "wauto", 5)) {
        int n = 20;
        if (buf[5]) { long v = strtol(buf + 5, nullptr, 10);
                      if (v >= 2 && v <= SWEEP_MAX_POINTS) n = (int)v; }
        static LcrWPoint tab[SWEEP_MAX_POINTS];
        int got = lcr_api_sweep_w(10.0, 10000.0, n, tab, SWEEP_MAX_POINTS);
        if (got >= 0) { print_sweep_w(tab, n); for (int i = 0; i < n; i++) print_w(tab[i]); }
        else Serial.printf("# ERR: w sweep failed (%d)\n", got);
    }
    else if (buf[0] == 'w') {
        double f = strtod(buf + 1, nullptr);
        LcrWPoint p;
        if (f >= FREQ_MIN_HZ && f <= FREQ_MAX_HZ && lcr_api_measure_w(f, &p) == LCR_API_OK) print_w(p);
        else Serial.println("# ERR: w measure failed");
    }
    else if (buf[0] == 'u') {
        int8_t code = !strcmp(buf+1,"1") ? 0 : !strcmp(buf+1,"3") ? 1 :
                      !strcmp(buf+1,"10") ? 2 : !strcmp(buf+1,"33") ? 3 : -1;
        Serial.println(lcr_api_set_ranges(code,-1,-1) == LCR_API_OK ? "# READY" : "# ERR");
    }
    else if (buf[0] == 'i') {
        int8_t code = !strcmp(buf+1,"1") ? 0 : !strcmp(buf+1,"3") ? 1 :
                      !strcmp(buf+1,"10") ? 2 : !strcmp(buf+1,"33") ? 3 : -1;
        Serial.println(lcr_api_set_ranges(-1,code,-1) == LCR_API_OK ? "# READY" : "# ERR");
    }
    else if (buf[0] == 'r') {
        LcrCalItem it;
        if (parse_tia_range(buf + 1, it))
            Serial.println(lcr_api_set_ranges(-1,-1,(int8_t)it) == LCR_API_OK ? "# READY" : "# ERR");
        else Serial.println("# ERR: unknown range");
    }
    else if (!strcmp(buf, "jzclr")) { lcr_calib_clear_all(); Serial.println("# READY"); }
    else if (!strcmp(buf, "jzdump") || !strcmp(buf, "osdump")) {
        lcr_api_cal_dump(); Serial.println("# READY");
    }
    else if (!strcmp(buf, "restore")) {
        Serial.println("# RESTORE: send backup lines, finish with END");
        int n = lcr_api_cal_restore();
        Serial.printf("# RESTORE done: %d entries\n", n);
        Serial.println("# READY");
    }
    else if (!strcmp(buf, "jzopen") || !strcmp(buf, "jzshort")) {
        LcrCalItem it = (buf[2] == 'o') ? LCR_CAL_OPEN : LCR_CAL_SHORT;
        Serial.println(lcr_api_calibrate(it, 0, nullptr) == LCR_API_OK ? "# READY" : "# ERR: cal failed");
    }
    else if (!strncmp(buf, "jzset", 5) || !strncmp(buf, "osset", 5)) {
        if (lcr_calib_parse_jzset(buf) || lcr_os_parse_set(buf)) Serial.println("# READY");
    }
    else if (!strncmp(buf, "jz", 2)) {
        const char *p = buf + 2;
        if (*p == '\0') {
            LcrCalStatus st;
            lcr_api_cal_status(&st);
            for (int i = 0; i < 10; i++)
                Serial.printf("# CAL [%s] %s g=%.4f f_p=%.4g tau=%+.3g\n",
                              TIA_RANGE_NAME[i], st.range[i].valid ? "ok" : "--",
                              st.range[i].g, st.range[i].f_p, st.range[i].tau);
            Serial.printf("# OS: open=%s short=%s\n",
                          st.open_valid ? "ok" : "--", st.short_valid ? "ok" : "--");
        } else {
            LcrCalItem it;
            char *us = strchr((char*)p, '_');
            if (us && parse_tia_range(p, it)) {
                double ohm = strtod(us + 1, nullptr);
                if (strchr(us+1,'k')||strchr(us+1,'K')) ohm *= 1e3;
                if (strchr(us+1,'m')||strchr(us+1,'M')) ohm *= 1e6;
                LcrCalParams cp;
                int rc = lcr_api_calibrate(it, ohm, &cp);
                if (rc == LCR_API_OK)
                    Serial.printf("# CAL RESULT g=%.4f f_p=%.4g tau=%.4g\n", cp.g, cp.f_p, cp.tau);
                else Serial.println("# ERR: cal failed");
            } else Serial.println("# ERR: use jz<range>_<ohm>");
        }
    }
    else if (!strcmp(buf, "stop")) { lcr_api_set_freq(0); Serial.println("# READY"); }
    else if (!strcmp(buf, "sc")) { lcr_api_selfcheck(); Serial.println("# READY"); }
    else if (!strcmp(buf, "diag1")) { lcr_api_set_diagnostics(true);  Serial.println("# DIAG ON");  Serial.println("# READY"); }
    else if (!strcmp(buf, "diag0")) { lcr_api_set_diagnostics(false); Serial.println("# DIAG OFF"); Serial.println("# READY"); }
    else {
        char *end = nullptr;
        double f = strtod(buf, &end);
        if (end != buf && f >= FREQ_MIN_HZ && f <= FREQ_MAX_HZ) {
            LcrZPoint p;
            if (lcr_api_measure_z(f, &p) == LCR_API_OK) print_z(p);
            else Serial.println("# ERR: measure failed");
        } else Serial.println("# ERR: unknown command");
    }
    Serial.println("# READY");
}

void setup() {
    // ★ 1. 先读 GPIO4（上拉输入：高=诊断开），再初始化 → 上电默认关闭，主程序决定开/关
    pinMode(DIAG_PIN, INPUT_PULLUP);
    delay(20);  // 让上拉稳定
    lcr_api_set_diagnostics(digitalRead(DIAG_PIN) == HIGH);

    // ★ 2. 整体初始化（内部打印已按 g_lcr_diag 门控）
    if (!lcr_api_init()) { while (1) delay(1000); }

    if (g_lcr_diag) {
        Serial.println("# == LCR API firmware (diag ON via GPIO4) ==");
        Serial.println("# cmds: <freq> | auto[N] | w<freq> | wauto[N] | u/i/r ranges");
        Serial.println("#       jz | jz<range>_<ohm> | jzopen/jzshort | jzclr | jzdump/osdump");
        Serial.println("#       restore | stop | sc | diag1/diag0");
    } else {
        Serial.println("# == LCR API firmware (diag OFF) ==");
    }
    Serial.println("# READY");
}

void loop() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (s_line_len == 0) continue;
            s_line[s_line_len] = '\0'; s_line_len = 0;
            handle_line(s_line);
        } else if (s_line_len < LINE_BUF_LEN - 1) s_line[s_line_len++] = c;
        else s_line_len = 0;
    }
    delay(2);
}
