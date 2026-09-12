// ============================================================================
// screen_component.cpp —— 模式 1：未知单元件识别
// ----------------------------------------------------------------------------
// 5 个几何频点逐点 MeasureAndCalcZ。判型后结果始终绑定到一个真实 fAct：
// 被动器件选与中位聚合值最接近的代表点；UNKNOWN/ACTIVE 优先显示原计划
// 中位频率点，只在该点根本没测出 Z 时才从低频向高频找第一个测得点。
// ============================================================================

#include "screens.h"
#include "radio_lock.h"

#include <Arduino.h>
#include <math.h>

ComponentScreen screenComponent;

namespace {
constexpr int kCfgX = 6;
constexpr int kCfgY0 = 32;
constexpr int kCfgDY = 45;

const char* engOrDash(double v, const char* unit, char* buf, int len)
{
    if (!isfinite(v)) { snprintf(buf, len, "--"); return buf; }
    return ui::fmtEng(v, unit, buf, len, 3);
}

double pointRp(const AppZPoint& p, const AppCalcResult& c)
{
    if (isfinite(p.reOhm) && p.reOhm < 0.0 && isfinite(p.imOhm) &&
        fabs(p.reOhm) > 1e-30) {
        const double d2 = p.reOhm * p.reOhm + p.imOhm * p.imOhm;
        return d2 / p.reOhm;
    }
    return c.rp;
}

void drawEquivalents(const AppZPoint& p, const AppCalcResult& c, int y)
{
    char a[16], b[16], line[48];
    const bool calcOk = c.apiStatus == 0;
    const ImpedanceNature nature = classifyImpedanceNature(p);
    const double rp = calcOk ? pointRp(p, c) : NAN;
    const double rs = calcOk ? c.rs : p.reOhm;

    tft.setTextFont(1);
    tft.setTextColor(ui::C_FG, ui::C_BG);
    if (nature == ImpedanceNature::Capacitive) {
        snprintf(line, sizeof(line), "P Cp:%s Rp:%s",
                 engOrDash(calcOk ? c.cp : NAN, "F", a, sizeof(a)),
                 engOrDash(rp, "Ohm", b, sizeof(b)));
        tft.drawString(line, 4, y);
        snprintf(line, sizeof(line), "S Cs:%s Rs:%s",
                 engOrDash(calcOk ? c.cs : NAN, "F", a, sizeof(a)),
                 engOrDash(rs, "Ohm", b, sizeof(b)));
    } else if (nature == ImpedanceNature::Inductive) {
        snprintf(line, sizeof(line), "P Lp:%s Rp:%s",
                 engOrDash(calcOk ? c.lp : NAN, "H", a, sizeof(a)),
                 engOrDash(rp, "Ohm", b, sizeof(b)));
        tft.drawString(line, 4, y);
        snprintf(line, sizeof(line), "S Ls:%s Rs:%s",
                 engOrDash(calcOk ? c.ls : NAN, "H", a, sizeof(a)),
                 engOrDash(rs, "Ohm", b, sizeof(b)));
    } else {
        snprintf(line, sizeof(line), "P Xp:-- Rp:%s",
                 engOrDash(rp, "Ohm", a, sizeof(a)));
        tft.drawString(line, 4, y);
        snprintf(line, sizeof(line), "S Xs:-- Rs:%s",
                 engOrDash(rs, "Ohm", a, sizeof(a)));
    }
    tft.drawString(line, 4, y + 12);
}

void initFailedSlot(AppZPoint& z, AppCalcResult& c, double f)
{
    z = AppZPoint{};
    z.fReq = f;
    z.fAct = z.reOhm = z.imOhm = z.magOhm = z.phaseDeg = z.D = z.Q = NAN;
    z.apiType = 'E';
    z.apiStatus = -3;
    c = AppCalcResult{};
    c.type = 'E';
    c.rs = c.cs = c.ls = c.rp = c.cp = c.lp = c.D = c.Q = NAN;
    c.apiStatus = -3;
}

void drawZ(const AppZPoint& p, int y)
{
    char r[16], i[16], line[48];
    ui::fmtEng(p.reOhm, "", r, sizeof(r), 3);
    ui::fmtEng(fabs(p.imOhm), "", i, sizeof(i), 3);
    snprintf(line, sizeof(line), "Z:%s%cj%s Ohm", r, p.imOhm < 0 ? '-' : '+', i);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_FG, ui::C_BG);
    tft.drawString(line, 4, y);
}

}  // namespace

void ComponentScreen::onEnter()
{
    static bool inited = false;
    if (!inited) {
        m_f0.setup((int32_t)INSTRUMENT_F_MIN_HZ, 9999, 5, 100);
        m_f1.setup(11, (int32_t)INSTRUMENT_F_MAX_HZ, 5, 2000);
        inited = true;
    }
    m_phase = Phase::Config;
    m_field = 0;
    m_nextIdx = 0;
    m_pendingId = 0;
    m_cancelReq = false;
    drawConfig();
}

void ComponentScreen::drawConfig()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("UNKNOWN ID", false);

    DigitEditor* eds[2] = {&m_f0, &m_f1};
    const char* labels[2] = {"START Hz", "STOP Hz"};
    for (int i = 0; i < 2; ++i) {
        const bool focused = (m_field == i);
        const int y = kCfgY0 + i * kCfgDY;
        tft.setTextFont(1);
        tft.setTextColor(focused ? ui::C_ACCENT : ui::C_DIM, ui::C_BG);
        tft.drawString(labels[i], kCfgX, y - 10);
        const int ex = tft.width() - eds[i]->width(26) - 6;
        eds[i]->draw(ex < 4 ? 4 : ex, y, 26, focused);
    }

    if (millis() < m_errUntilMs) {
        tft.setTextFont(1);
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        tft.drawCentreString("NEED F0<F1 10Hz..10k", tft.width() / 2, 126, 1);
    }
    ui::bottomHint("ENC:EDIT OK:IDENT BACK");
}

bool ComponentScreen::startMeasure()
{
    const int32_t f0 = m_f0.value(), f1 = m_f1.value();
    if (f0 >= f1 || f0 < (int32_t)INSTRUMENT_F_MIN_HZ ||
        f1 > (int32_t)INSTRUMENT_F_MAX_HZ) {
        m_errUntilMs = millis() + 2000;
        drawConfig();
        return false;
    }

    SweepConfig cfg{};
    cfg.kind = MeasurementKind::OnePortImpedance;
    cfg.fStartHz = (double)f0;
    cfg.fStopHz = (double)f1;
    cfg.pointsPerDecade = 12;
    cfg.maxPoints = 5;
    m_nPlan = (uint8_t)buildFrequencyPlan(cfg, m_plan, 5);
    if (m_nPlan < 3) {
        m_errUntilMs = millis() + 2000;
        drawConfig();
        return false;
    }

    for (uint8_t i = 0; i < m_nPlan; ++i) initFailedSlot(m_z[i], m_calc[i], m_plan[i]);
    m_nextIdx = 0;
    m_pendingId = 0;
    m_cancelReq = false;
    m_phase = Phase::Run;
    radioLockNotifyMeasurementActive(true);
    if (!submitNext()) {
        radioLockNotifyMeasurementActive(false);
        m_phase = Phase::Config;
        drawConfig();
        return false;
    }
    drawRun();
    return true;
}

bool ComponentScreen::submitNext()
{
    if (m_nextIdx >= m_nPlan) return false;
    LcrJob job{};
    job.kind = LcrJobKind::MeasureAndCalcZ;
    job.frequencyHz = m_plan[m_nextIdx];
    if (!lcrServiceSubmit(job)) return false;
    m_pendingId = job.id;
    return true;
}

void ComponentScreen::drawRun()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("UNKNOWN ID", false);
    tft.setTextFont(2);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawCentreString("IDENTIFYING...", tft.width() / 2, 48, 2);
    ui::progressBar(12, 82, tft.width() - 24, 12, 0.0, ui::C_ACCENT);
    tft.setTextFont(1);
    tft.drawCentreString("0/0", tft.width() / 2, 104, 1);
    ui::bottomHint("BACK:STOP AFTER POINT");
}

void ComponentScreen::updateRunProgress(bool stopping)
{
    char buf[40];
    tft.fillRect(0, 100, tft.width(), 18, ui::C_BG);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_FG, ui::C_BG);
    if (stopping) snprintf(buf, sizeof(buf), "STOPPING...");
    else snprintf(buf, sizeof(buf), "%d/%d", (int)m_nextIdx, (int)m_nPlan);
    tft.drawCentreString(buf, tft.width() / 2, 104, 1);
    ui::progressBar(12, 82, tft.width() - 24, 12,
                    m_nPlan ? (double)m_nextIdx / m_nPlan : 0.0, ui::C_ACCENT);
}

void ComponentScreen::drawResult()
{
    tft.fillScreen(ui::C_BG);
    const bool unknown = m_est.type == ComponentEstimate::Type::Unknown;
    const bool active = m_est.type == ComponentEstimate::Type::Active;
    ui::topBar(unknown ? "INCONCLUSIVE" : "ID RESULT", false);

    tft.setTextFont(2);
    tft.setTextColor(unknown || active ? ui::C_ERR : ui::C_OK, ui::C_BG);
    tft.drawCentreString(componentTypeText(m_est.type), tft.width() / 2, 20, 2);

    char line[64], fbuf[20];
    int y = 42;
    if (unknown) {
        tft.setTextFont(1);
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        snprintf(line, sizeof(line), "ERR:%s", m_est.reason);
        tft.drawString(line, 4, y); y += 12;
        tft.setTextColor(ui::C_DIM, ui::C_BG);
        snprintf(line, sizeof(line), "MEAS %u/%u CALC %u MM %u",
                 m_est.nMeasured, m_nPlan, m_est.nCalcValid, m_est.nTypeMismatch);
        tft.drawString(line, 4, y); y += 12;
        snprintf(line, sizeof(line), "R/C/L %u/%u/%u",
                 m_est.nR, m_est.nC, m_est.nL);
        tft.drawString(line, 4, y); y += 12;
    } else if (active) {
        tft.setTextFont(1);
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        snprintf(line, sizeof(line), "%s neg=%u/%u", m_est.reason,
                 m_est.nNegativeReal, m_est.nMeasured);
        tft.drawString(line, 4, y); y += 12;
    }

    const int idx = unknown || active ? m_est.detailIndex : m_est.representativeIndex;
    if (idx < 0 || idx >= (int)m_nPlan) {
        tft.setTextFont(1);
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        tft.drawCentreString("NO MEASURED POINT", tft.width() / 2, y + 8, 1);
        ui::bottomHint("OK:AGAIN BACK:CONFIG");
        return;
    }

    const AppZPoint& p = m_z[idx];
    const AppCalcResult& c = m_calc[idx];
    tft.setTextFont(1);
    tft.setTextColor(ui::C_FG, ui::C_BG);
    snprintf(line, sizeof(line), "%s F:%s",
             unknown || active ? "DETAIL" : "REP",
             ui::fmtFreq(p.fAct, fbuf, sizeof(fbuf)));
    tft.drawString(line, 4, y); y += 12;
    tft.drawString(impedanceNatureText(classifyImpedanceNature(p)), 4, y); y += 12;

    drawEquivalents(p, c, y); y += 24;
    const double q = (c.apiStatus == 0 && isfinite(c.Q)) ? c.Q : p.Q;
    const double d = (c.apiStatus == 0 && isfinite(c.D)) ? c.D : p.D;
    snprintf(line, sizeof(line), "Q:%.4g D:%.4g", q, d);
    tft.drawString(line, 4, y); y += 12;
    if (y <= 136) drawZ(p, y);

    if (c.apiStatus != 0 && y + 12 <= 144) {
        snprintf(line, sizeof(line), "calc status=%d", c.apiStatus);
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        tft.drawString(line, 4, y + 12);
    }
    ui::bottomHint("OK:AGAIN BACK:CONFIG");
}

void ComponentScreen::pumpEvents()
{
    LcrEvent ev;
    while (lcrServiceTakeEvent(ev)) {
        if (ev.id != m_pendingId) continue;
        m_pendingId = 0;

        if (ev.kind == LcrJobKind::MeasureAndCalcZ) {
            const uint8_t slot = m_nextIdx;
            if (slot < m_nPlan && ev.pointCount >= 1) {
                m_z[slot] = ev.z[0];
                m_calc[slot] = ev.calc;
            } else if (slot < m_nPlan) {
                m_z[slot].apiStatus = ev.backendStatus;
                m_calc[slot].apiStatus = ev.backendStatus;
            }
            ++m_nextIdx;
            updateRunProgress(m_cancelReq);
        } else if (ev.kind == LcrJobKind::StopTone) {
            radioLockNotifyMeasurementActive(false);
            if (m_cancelReq) {
                m_cancelReq = false;
                m_phase = Phase::Config;
                drawConfig();
            } else {
                finishRun();
            }
            return;
        }
    }
}

void ComponentScreen::finishRun()
{
    m_est = summarizeComponent(m_z, m_calc, m_nPlan);
    m_phase = Phase::Result;
    drawResult();
}

void ComponentScreen::onTick()
{
    if (m_phase != Phase::Run) return;
    pumpEvents();
    if (m_phase != Phase::Run || m_pendingId != 0) return;

    if (m_nextIdx >= m_nPlan || m_cancelReq) {
        LcrJob job{};
        job.kind = LcrJobKind::StopTone;
        if (lcrServiceSubmit(job)) {
            m_pendingId = job.id;
            updateRunProgress(true);
        }
        return;
    }
    submitNext();
}

void ComponentScreen::onEvent(InputEvent e)
{
    if (m_phase == Phase::Run) {
        if (e == InputEvent::Back) {
            m_cancelReq = true;
            lcrServiceRequestCancel();
            updateRunProgress(true);
        }
        return;
    }

    if (m_phase == Phase::Result) {
        if (e == InputEvent::Ok) { startMeasure(); return; }
        if (e == InputEvent::Back) { onEnter(); return; }
        return;
    }

    if (e == InputEvent::Ok) { startMeasure(); return; }
    if (e == InputEvent::Back) { screens.pop(); return; }

    DigitEditor* eds[2] = {&m_f0, &m_f1};
    if (!eds[m_field]->onEvent(e)) {
        if (e == InputEvent::Down && m_field == 0) { m_field = 1; eds[1]->setCursor(0); }
        else if (e == InputEvent::Up && m_field == 1) { m_field = 0; eds[0]->setCursor(4); }
    }
    drawConfig();
}
