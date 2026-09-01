// ============================================================================
// screen_sweep.cpp —— 幅频/相频特性测量（扫频 Bode 图）
// ----------------------------------------------------------------------------
// 三阶段：
//   PhConfig  配置：起始频率 f0、终止频率 f1、每十倍频点数（对数间隔）、
//                   单/双端口复选框。频率约束 10~10000 Hz 且 f0<f1。
//   PhRun     扫频：onTick 每次测一个频点（队友接口阻塞一次），曲线实时生长；
//                   BACK 中止。量程随数据自动扩展（扩大则整体重绘）。
//   PhDone    结果：双色曲线（1-port: |Z|+辐角；2-port: 增益+相位）、
//                   坐标轴/网格、左右键移动游标并显示读数；
//                   OK 通过蓝牙导出 CSV；顶部横幅显示滤波器类型/等效电路估计。
// ============================================================================

#include "screens.h"
#include "bt_link.h"
#include "partner_api.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

SweepScreen screenSweep;

namespace {
// 绘图区几何（320x240；左 32px 幅度刻度、右 34px 相位刻度、底部 14px 频率刻度）
constexpr int kPlotX = 32, kPlotY = 52, kPlotW = 254, kPlotH = 134;
constexpr int kBannerY = 26;    // 识别结果横幅
constexpr int kReadY   = 38;    // 游标读数行
constexpr int kProgY   = 196;   // 扫频进度条
// 配置页行位置
constexpr int kCfgX = 24;
constexpr int kCfgY0 = 42, kCfgDY = 44;
}  // namespace

// ---------------------------------------------------------------------------
void SweepScreen::onEnter()
{
    static bool inited = false;
    if (!inited) {
        m_f0.setup(FREQ_MIN_HZ, FREQ_MAX_HZ - 1, 5, 100);
        m_f1.setup(FREQ_MIN_HZ + 1, FREQ_MAX_HZ, 5, 10000);
        m_ppd.setup(SWEEP_PPD_MIN, SWEEP_PPD_MAX, 2, SWEEP_PPD_DEFAULT);
        m_mode.setup("1-PORT   |Z| of DUT", "2-PORT   Gain/Phase", false);
        m_plot.setup(kPlotX, kPlotY, kPlotW, kPlotH);
        inited = true;
    }
    m_field = F_F0;
    m_phase = PhConfig;
    drawConfig();
}

// ---------------------------------------------------------------------------
// 配置页
// ---------------------------------------------------------------------------
void SweepScreen::drawConfig()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("FREQ RESPONSE SWEEP", bt.connected());

    struct { const char* label; DigitEditor* ed; } rows[3] = {
        {"START (Hz)", &m_f0},
        {"STOP  (Hz)", &m_f1},
        {"PTS / DECADE", &m_ppd},
    };
    for (int i = 0; i < 3; ++i) {
        const bool focused = (m_field == i);
        tft.setTextFont(1);
        tft.setTextColor(focused ? ui::C_ACCENT : ui::C_DIM, ui::C_BG);
        tft.drawString(rows[i].label, kCfgX, kCfgY0 + i * kCfgDY + 10);
        rows[i].ed->draw(kCfgX + 120, kCfgY0 + i * kCfgDY, 26, focused);
    }

    const int modeY = kCfgY0 + 3 * kCfgDY + 6;
    tft.setTextFont(1);
    tft.setTextColor(m_field == F_MODE ? ui::C_ACCENT : ui::C_DIM, ui::C_BG);
    tft.drawString("TARGET", kCfgX, modeY - 12);
    m_mode.draw(kCfgX, modeY, m_field == F_MODE);

    // 参数错误提示（限时）："F0<F1, 10..10000Hz"
    if (millis() < m_errUntilMs) {
        tft.setTextFont(2);
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        tft.drawCentreString("NEED F0<F1 IN 10..10k", tft.width() / 2, modeY + 30, 2);
    }
    ui::bottomHint("ENC:EDIT/TOGGLE <>:FIELD OK:START BACK:EXIT");
}

// ---------------------------------------------------------------------------
// 扫频启动：生成对数频率表，重置状态；参数非法返回 false
// ---------------------------------------------------------------------------
bool SweepScreen::startSweep()
{
    const int f0 = m_f0.value(), f1 = m_f1.value();
    if (f0 >= f1 || f0 < FREQ_MIN_HZ || f1 > FREQ_MAX_HZ) {
        m_errUntilMs = millis() + 2000;
        drawConfig();
        return false;
    }
    const double spanDec = log10((double)f1 / (double)f0);
    int n = 1 + (int)lround(m_ppd.value() * spanDec);
    if (n < 2) n = 2;
    if (n > SWEEP_MAX_POINTS) n = SWEEP_MAX_POINTS;

    for (int i = 0; i < n; ++i)                    // 对数等间隔频率表
        m_plan[i] = f0 * pow(10.0, spanDec * i / (n - 1));
    m_nPts = n;
    m_nextIdx = 0;
    m_cursor = n / 2;
    m_abort = false;
    m_rngValid = false;

    // 蓝牙通路：先声明整次扫频（api_contract: /api/scan/start）
    if (BT_AUTO_UPLOAD && bt.connected())
        bt.sendScanStart(BT_DEVICE_NAME, m_plan, n,
                         m_mode.value() ? "sweep-2port" : "sweep-1port");

    m_phase = PhRun;
    drawRun();
    return true;
}

// ---------------------------------------------------------------------------
// 扫频视图初始化（框架 + 空曲线 + 进度条）
// ---------------------------------------------------------------------------
void SweepScreen::drawRun()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar(m_mode.value() ? "H  SWEEP  2-PORT" : "Z  SWEEP  1-PORT",
               bt.connected());
    tft.setTextFont(2);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString("SWEEPING...", 10, kBannerY);

    // 初始量程（完成后按数据自适应）：1-port |Z| 先用对数 1~100k；
    // 2-port 增益先用 ±40dB。首个数据到达后即校正。
    const bool onePort = !m_mode.value();
    m_plot.setXRange(m_plan[0], m_plan[m_nPts - 1]);
    if (onePort) { m_plot.setLegend("|Z| Ohm", "angle deg"); m_plot.setMagRange(true, 1, 1e5); }
    else         { m_plot.setLegend("gain dB", "phase deg"); m_plot.setMagRange(false, -40, 40); }
    m_plot.setPhRange(-180, 180);
    m_plot.drawFrame();
    ui::bottomHint("BACK:ABORT");
}

// ---------------------------------------------------------------------------
// 量程自适应 + 曲线绘制
//   * 扫频中（finalDraw=false）：量程只扩大不缩小；数据超出当前量程 2% 即
//     重设量程并整体重绘已有曲线（低频闪一下，可接受）；
//   * 结束时（finalDraw=true）：按最终数据一次性重绘并画游标。
// ---------------------------------------------------------------------------
void SweepScreen::updateRangesAndDraw(bool finalDraw)
{
    const bool onePort = !m_mode.value();

    // 数据驱动量程（统计已完成点中的有效幅度值）
    const int drawn = (m_nextIdx < m_nPts) ? m_nextIdx : m_nPts;
    double lo = 1e300, hi = -1e300;
    for (int i = 0; i < drawn; ++i) {
        if (isfinite(m_mag[i])) {
            lo = fmin(lo, m_mag[i]);
            hi = fmax(hi, m_mag[i]);
        }
    }

    bool needRedraw = false;
    if (isfinite(lo) && isfinite(hi) && hi > lo) {
        const bool useLog = onePort && (hi / lo > 100.0);   // |Z| 跨 >2 个十倍频用对数轴
        const double pad = (hi - lo) * 0.05;
        const double nlo = lo - pad, nhi = hi + pad;
        if (!m_rngValid || finalDraw ||
            nlo < m_rngMin * 0.98 || nhi > m_rngMax * 1.02 || useLog != m_rngLog) {
            m_rngMin = nlo; m_rngMax = nhi; m_rngLog = useLog; m_rngValid = true;
            m_plot.setMagRange(useLog, (useLog ? fmax(nlo, 1e-9) : nlo), nhi);
            needRedraw = true;
        }
    }

    if (needRedraw || finalDraw) {
        m_plot.drawFrame();
        m_plot.drawCurveMag(m_f, m_mag, drawn, ui::C_CH1);
        m_plot.drawCurvePh(m_f, m_ph, drawn, ui::C_CH2);
    } else if (drawn > 1) {
        m_plot.drawSegMag(m_f, m_mag, drawn - 1, ui::C_CH1);  // 只画最新一段
        m_plot.drawSegPh(m_f, m_ph, drawn - 1, ui::C_CH2);
    }
}

// ---------------------------------------------------------------------------
// 测量并记录一个频点（一次阻塞调用队友接口；onTick 逐点驱动）
// ---------------------------------------------------------------------------
void SweepScreen::stepOnePoint()
{
    const bool onePort = !m_mode.value();
    const int i = m_nextIdx;

    AdcSampleResult s = measureImpedanceAtFreq((int)lround(m_plan[i]), onePort);
    RatioPhaseResult rp = computeRatioPhase(s);

    m_f[i] = (s.actualFreq > 0) ? s.actualFreq : m_plan[i];
    if (rp.ok && s.sampleLength >= 8) {
        // 字段顺序 = ImpedanceCalcInput 声明顺序（聚合初始化，勿改动）
        ImpedanceCalcInput in = {
            rp.amplitudeRatio, rp.phaseDiffDeg,
            s.transimpedanceGain, s.voltageGain, s.currentGain,
            s.actualFreq,
        };
        if (onePort) {
            const ImpedanceCalcResult z = calculateImpedance(in);
            m_zRe[i] = z.realPart;
            m_zIm[i] = z.imagPart;
            m_mag[i] = hypot(z.realPart, z.imagPart);
            m_ph[i]  = atan2(z.imagPart, z.realPart) * 180.0 / M_PI;
        } else {
            const GainPhaseResult g = calculateGainPhase(in);
            m_mag[i] = g.gainDb;
            m_ph[i]  = g.phaseDiff;
        }
    } else {
        m_mag[i] = NAN;                    // 失败点：画图/统计自动跳过
        m_ph[i]  = NAN;
        m_zRe[i] = m_zIm[i] = NAN;
    }
    ++m_nextIdx;

    // 蓝牙通路：逐点上传原始波形
    if (BT_AUTO_UPLOAD && bt.connected() && isfinite(m_mag[i]))
        bt.sendPoint(s, rp.amplitudeRatio, rp.phaseDiffDeg, onePort);

    // 进度显示
    if (i == 0) { tft.fillRect(0, kBannerY, 200, 18, ui::C_BG); }   // 清 "SWEEPING..."
    char buf[48];
    snprintf(buf, sizeof(buf), "%d/%d", m_nextIdx, m_nPts);
    tft.setTextFont(2);
    tft.setTextColor(ui::C_FG, ui::C_BG);
    tft.drawString(buf, 10, kBannerY);
    ui::progressBar(10, kProgY, tft.width() - 20, 12,
                    (double)m_nextIdx / m_nPts, ui::C_ACCENT);
    updateRangesAndDraw(false);

    if (m_nextIdx >= m_nPts) finishSweep();
}

// ---------------------------------------------------------------------------
// 扫频收尾：识别 + 最终绘图 + 进入游标态
// ---------------------------------------------------------------------------
void SweepScreen::finishSweep()
{
    const bool onePort = !m_mode.value();

    // 选做功能（启发式，界面标注 EST）：
    //   2-port 识别 高通/低通/带通/带阻 与阶数；1-port 估计等效电路
    bool allValid = true;
    for (int i = 0; i < m_nPts; ++i)
        if (!isfinite(m_mag[i])) { allValid = false; break; }

    if (allValid) {
        if (onePort) m_eqc = guessEquivalentCircuit(m_f, m_zRe, m_zIm, m_nPts);
        else         m_filter = classifyFilter(m_f, m_mag, m_nPts);
    }

    if (BT_AUTO_UPLOAD && bt.connected()) bt.sendScanEnd(m_nPts);

    m_phase = PhDone;
    drawDone();
}

// ---------------------------------------------------------------------------
void SweepScreen::drawDone()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar(m_mode.value() ? "H  SWEEP  2-PORT" : "Z  SWEEP  1-PORT",
               bt.connected());

    // 识别横幅（font2，一行）
    char buf[52];
    if (!m_mode.value()) {
        if (m_eqc.ok) {
            char v1[16], v2[16];
            snprintf(buf, sizeof(buf), "EST %s", m_eqc.model);
            if (m_eqc.C > 0) { snprintf(v1, sizeof(v1), " C=%s", ui::fmtEng(m_eqc.C, "F", v2, sizeof(v2))); strcat(buf, v1); }
            if (m_eqc.L > 0) { snprintf(v1, sizeof(v1), " L=%s", ui::fmtEng(m_eqc.L, "H", v2, sizeof(v2))); strcat(buf, v1); }
        } else snprintf(buf, sizeof(buf), "EST n/a");
    } else {
        if (m_filter.ok) {
            char f1s[16], f2s[16];
            ui::fmtFreq(m_filter.f1, f1s, sizeof(f1s));
            ui::fmtFreq(m_filter.f2, f2s, sizeof(f2s));
            if (m_filter.order > 0)
                snprintf(buf, sizeof(buf), "EST %s ~%d", m_filter.type, m_filter.order);
            else
                snprintf(buf, sizeof(buf), "EST %s", m_filter.type);
            if (m_filter.f2 > 0) { strcat(buf, " fc="); strcat(buf, f2s); }
        } else snprintf(buf, sizeof(buf), "EST n/a");
    }
    tft.setTextFont(2);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString(buf, 8, kBannerY);

    m_plot.setXRange(m_plan[0], m_plan[m_nPts - 1]);
    updateRangesAndDraw(true);
    m_plot.drawCursor(m_f[m_cursor]);
    updateReadout();
    ui::bottomHint("<>:CURSOR  OK:CSV->BT  BACK:CONFIG");
}

// ---------------------------------------------------------------------------
// 游标读数行：三段着色（频率白 / 幅度蓝 / 相位橙）
// ---------------------------------------------------------------------------
void SweepScreen::updateReadout()
{
    const int i = m_cursor;
    if (i < 0 || i >= m_nPts || !isfinite(m_f[i])) return;

    tft.fillRect(0, kReadY, tft.width(), 16, ui::C_BG);
    char seg[24];
    int x = 8;
    tft.setTextFont(2);

    ui::fmtFreq(m_f[i], seg, sizeof(seg));
    tft.setTextColor(ui::C_FG, ui::C_BG);
    tft.drawString(seg, x, kReadY);
    x += tft.textWidth(seg) + 12;

    if (!m_mode.value()) {                       // 1-port: |Z| + 辐角
        if (isfinite(m_mag[i])) {
            ui::fmtEng(m_mag[i], "Ohm", seg, sizeof(seg));
            tft.setTextColor(ui::C_CH1, ui::C_BG);
            tft.drawString(seg, x, kReadY);
            x += tft.textWidth(seg) + 12;
        }
        if (isfinite(m_ph[i])) {
            snprintf(seg, sizeof(seg), "%+.1f", m_ph[i]);
            tft.setTextColor(ui::C_CH2, ui::C_BG);
            tft.drawString(seg, x, kReadY);
        }
    } else {                                      // 2-port: 增益 dB + 相位
        if (isfinite(m_mag[i])) {
            snprintf(seg, sizeof(seg), "%+.1fdB", m_mag[i]);
            tft.setTextColor(ui::C_CH1, ui::C_BG);
            tft.drawString(seg, x, kReadY);
            x += tft.textWidth(seg) + 12;
        }
        if (isfinite(m_ph[i])) {
            snprintf(seg, sizeof(seg), "%+.1f", m_ph[i]);
            tft.setTextColor(ui::C_CH2, ui::C_BG);
            tft.drawString(seg, x, kReadY);
        }
    }
}

// ---------------------------------------------------------------------------
void SweepScreen::onTick()
{
    if (m_phase == PhRun) {
        if (m_abort) {                            // 用户中止 -> 回配置页
            m_phase = PhConfig;
            if (bt.connected()) bt.sendScanEnd(m_nextIdx);
            drawConfig();
            return;
        }
        if (m_nextIdx < m_nPts) stepOnePoint();   // 每个循环测一个点，UI 保持响应
    }
    if (m_msgUntilMs && millis() > m_msgUntilMs) {// 临时提示复位
        m_msgUntilMs = 0;
        ui::bottomHint("<>:CURSOR  OK:CSV->BT  BACK:CONFIG");
    }
}

// ---------------------------------------------------------------------------
void SweepScreen::onEvent(InputEvent e)
{
    // ---- 扫频进行中 --------------------------------------------------------
    if (m_phase == PhRun) {
        if (e == InputEvent::Back) m_abort = true;
        return;
    }

    // ---- 结果页（游标态） --------------------------------------------------
    if (m_phase == PhDone) {
        // 游标移动：全量重绘曲线区（避免旧游标虚线残留），再叠加新游标
        auto moveCursor = [&](int delta) {
            const int nc = m_cursor + delta;
            if (nc < 0 || nc > m_nPts - 1) return;
            m_cursor = nc;
            updateRangesAndDraw(true);
            m_plot.drawCursor(m_f[m_cursor]);
            updateReadout();
        };
        switch (e) {
        case InputEvent::Left:   moveCursor(-1);  break;   // 按住可连发（input 层）
        case InputEvent::Right:  moveCursor(+1);  break;
        case InputEvent::EncDec: moveCursor(-5);  break;   // 编码器快移
        case InputEvent::EncInc: moveCursor(+5);  break;
        case InputEvent::Ok: {                    // 蓝牙导出 CSV
            if (bt.connected()) {
                bt.sendCsvSweep("lcr_sweep", m_f, m_mag, m_ph, m_nPts,
                                !m_mode.value());
                ui::bottomHint("CSV SENT VIA BT");
            } else {
                ui::bottomHint("BT NOT CONNECTED");
            }
            m_msgUntilMs = millis() + 1500;
            break;
        }
        case InputEvent::Back:
            m_phase = PhConfig;
            drawConfig();
            break;
        default:
            break;
        }
        return;
    }

    // ---- 配置页 ------------------------------------------------------------
    switch (e) {
    case InputEvent::Ok:
        startSweep();
        return;
    case InputEvent::Back:
        screens.pop();
        return;
    default:
        break;
    }

    const bool lr = (e == InputEvent::Left || e == InputEvent::Right);
    int next = m_field;

    if (m_field <= F_PPD) {                       // 三个数字编辑器
        static const int nd[3] = {5, 5, 2};       // 各编辑器位数
        DigitEditor* eds[3] = {&m_f0, &m_f1, &m_ppd};
        DigitEditor* ed = eds[m_field];
        if (!ed->onEvent(e)) {                    // 数位边界 -> 焦点转移
            if (!lr) return;
            // 环形顺序 F_F0->F_F1->F_PPD->F_MODE（共 4 个字段）
            next = (e == InputEvent::Right) ? (m_field + 1) % 4
                                           : (m_field + 3) % 4;
            if (next <= F_PPD)                    // 新编辑器光标置于进入侧
                eds[next]->setCursor(e == InputEvent::Right ? 0 : nd[next] - 1);
        }
    } else {                                      // 复选框
        if (m_mode.onEvent(e)) { drawConfig(); return; }
        if (lr) {
            next = F_PPD;
            m_ppd.setCursor(e == InputEvent::Right ? 0 : 1);
        }
    }
    if (next != m_field) {
        m_field = next;
    }
    drawConfig();                                 // 配置页整体重绘（轻量）
}
