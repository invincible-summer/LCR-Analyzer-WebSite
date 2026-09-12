// ============================================================================
// screen_single_point.cpp —— 单频点 LCR 测量
// ----------------------------------------------------------------------------
// 用户输入一个频率，只提交一次 MeasureAndCalcZ。测量与 StopTone 都通过
// Worker completion 推进；UI loop 不等待硬件、不 delay。结果显示实际频率、
// 复阻抗、串/并联等效参数、Q/D，以及容性/感性/阻性/负阻性。
// ============================================================================

#include "screens.h"
#include "radio_lock.h"

#include <Arduino.h>
#include <math.h>

SinglePointScreen screenSinglePoint;

namespace {

const char* engOrDash(double v, const char* unit, char* buf, int len)
{
    if (!isfinite(v)) { snprintf(buf, len, "--"); return buf; }
    return ui::fmtEng(v, unit, buf, len, 3);
}

double displayRp(const AppZPoint& p, const AppCalcResult& c)
{
    // DNT calc 的正阻路径直接使用 API 值；负阻点需要保留 Rp 的负号。
    // Rp=1/G=(Re^2+Im^2)/Re，仅是同一个已测 Z 的坐标表示，不是新测量。
    if (isfinite(p.reOhm) && p.reOhm < 0.0 && isfinite(p.imOhm)) {
        const double d2 = p.reOhm * p.reOhm + p.imOhm * p.imOhm;
        if (fabs(p.reOhm) > 1e-30) return d2 / p.reOhm;
    }
    return c.rp;
}

void drawEquivalent(const AppZPoint& p, const AppCalcResult& c, int y)
{
    char a[16], b[16], line[48];
    const bool calcOk = c.apiStatus == 0;
    const ImpedanceNature nature = classifyImpedanceNature(p);
    const double rp = calcOk ? displayRp(p, c) : NAN;
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

void setFailedPoint(AppZPoint& z, AppCalcResult& c, double f, int status)
{
    z = AppZPoint{};
    z.fReq = f;
    z.fAct = z.reOhm = z.imOhm = z.magOhm = z.phaseDeg = z.D = z.Q = NAN;
    z.apiType = 'E';
    z.apiStatus = status;
    c = AppCalcResult{};
    c.type = 'E';
    c.rs = c.cs = c.ls = c.rp = c.cp = c.lp = c.D = c.Q = NAN;
    c.apiStatus = status;
}

}  // namespace

void SinglePointScreen::onEnter()
{
    static bool inited = false;
    if (!inited) {
        m_freq.setup((int32_t)INSTRUMENT_F_MIN_HZ,
                     (int32_t)INSTRUMENT_F_MAX_HZ, 5, 1000);
        inited = true;
    }
    m_phase = Phase::Config;
    m_pendingId = 0;
    m_cancelReq = false;
    drawConfig();
}

void SinglePointScreen::drawConfig()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("SINGLE FREQ LCR", false);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString("FREQUENCY / Hz", 6, 32);
    const int x = (tft.width() - m_freq.width(26)) / 2;
    m_freq.draw(x < 3 ? 3 : x, 52, 26, true);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawCentreString("10 .. 10000 Hz", tft.width() / 2, 98, 1);
    ui::bottomHint("ENC:EDIT OK:MEAS BACK");
}

void SinglePointScreen::drawRun(const char* text)
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("SINGLE FREQ LCR", false);
    tft.setTextFont(2);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawCentreString(text, tft.width() / 2, 52, 2);
    char fbuf[20];
    tft.setTextFont(1);
    tft.drawCentreString(ui::fmtFreq(m_requestedHz, fbuf, sizeof(fbuf)),
                         tft.width() / 2, 82, 1);
    ui::bottomHint("BACK:STOP AFTER POINT");
}

bool SinglePointScreen::startMeasure()
{
    if (m_phase != Phase::Config && m_phase != Phase::Result) return false;
    m_requestedHz = (double)m_freq.value();
    setFailedPoint(m_z, m_calc, m_requestedHz, -1);
    m_cancelReq = false;
    m_pendingId = 0;

    LcrJob job{};
    job.kind = LcrJobKind::MeasureAndCalcZ;
    job.frequencyHz = m_requestedHz;
    radioLockNotifyMeasurementActive(true);
    if (!lcrServiceSubmit(job)) {
        radioLockNotifyMeasurementActive(false);
        setFailedPoint(m_z, m_calc, m_requestedHz, (int)AppLcrStatus::Busy);
        m_phase = Phase::Result;
        drawResult();
        return false;
    }
    m_pendingId = job.id;
    m_phase = Phase::Measuring;
    drawRun("MEASURING...");
    return true;
}

bool SinglePointScreen::submitStop()
{
    if (m_pendingId != 0) return true;
    LcrJob job{};
    job.kind = LcrJobKind::StopTone;
    if (!lcrServiceSubmit(job)) return false;
    m_pendingId = job.id;
    m_phase = Phase::Stopping;
    drawRun("STOPPING...");
    return true;
}

void SinglePointScreen::pumpEvents()
{
    LcrEvent ev;
    while (lcrServiceTakeEvent(ev)) {
        if (ev.id != m_pendingId) continue;
        m_pendingId = 0;

        if (m_phase == Phase::Measuring && ev.kind == LcrJobKind::MeasureAndCalcZ) {
            if (ev.pointCount >= 1) {
                m_z = ev.z[0];
                m_calc = ev.calc;
            } else {
                setFailedPoint(m_z, m_calc, m_requestedHz, ev.backendStatus);
            }
            m_phase = Phase::Stopping;
            submitStop();
            return;
        }

        if (m_phase == Phase::Stopping && ev.kind == LcrJobKind::StopTone) {
            radioLockNotifyMeasurementActive(false);
            if (m_cancelReq) {
                m_cancelReq = false;
                m_phase = Phase::Config;
                drawConfig();
            } else {
                m_phase = Phase::Result;
                drawResult();
            }
            return;
        }
    }
}

void SinglePointScreen::drawResult()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("SINGLE FREQ LCR", false);

    const ImpedanceNature nature = classifyImpedanceNature(m_z);
    if (nature == ImpedanceNature::Invalid) {
        tft.setTextFont(2);
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        tft.drawCentreString("MEASURE ERROR", tft.width() / 2, 30, 2);
        char line[40];
        tft.setTextFont(1);
        tft.setTextColor(ui::C_DIM, ui::C_BG);
        snprintf(line, sizeof(line), "status=%d", m_z.apiStatus);
        tft.drawCentreString(line, tft.width() / 2, 62, 1);
        char fbuf[20];
        snprintf(line, sizeof(line), "req %s", ui::fmtFreq(m_requestedHz, fbuf, sizeof(fbuf)));
        tft.drawCentreString(line, tft.width() / 2, 78, 1);
        ui::bottomHint("OK:AGAIN BACK:CONFIG");
        return;
    }

    tft.setTextFont(2);
    tft.setTextColor(nature == ImpedanceNature::NegativeResistive ? ui::C_ERR : ui::C_OK,
                     ui::C_BG);
    tft.drawCentreString(impedanceNatureText(nature), tft.width() / 2, 22, 2);

    char line[48], a[16], b[16], fbuf[20];
    tft.setTextFont(1);
    tft.setTextColor(ui::C_FG, ui::C_BG);
    snprintf(line, sizeof(line), "F:%s", ui::fmtFreq(m_z.fAct, fbuf, sizeof(fbuf)));
    tft.drawString(line, 4, 46);

    ui::fmtEng(m_z.reOhm, "", a, sizeof(a), 3);
    ui::fmtEng(fabs(m_z.imOhm), "", b, sizeof(b), 3);
    snprintf(line, sizeof(line), "Z:%s%cj%s Ohm", a, m_z.imOhm < 0 ? '-' : '+', b);
    tft.drawString(line, 4, 58);

    drawEquivalent(m_z, m_calc, 70);

    const double q = (m_calc.apiStatus == 0 && isfinite(m_calc.Q)) ? m_calc.Q : m_z.Q;
    const double d = (m_calc.apiStatus == 0 && isfinite(m_calc.D)) ? m_calc.D : m_z.D;
    snprintf(line, sizeof(line), "Q:%.4g  D:%.4g", q, d);
    tft.drawString(line, 4, 98);
    snprintf(line, sizeof(line), "phase:%+.3g deg", m_z.phaseDeg);
    tft.drawString(line, 4, 110);
    if (m_calc.apiStatus != 0) {
        snprintf(line, sizeof(line), "calc status=%d", m_calc.apiStatus);
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        tft.drawString(line, 4, 122);
    }
    ui::bottomHint("OK:AGAIN BACK:CONFIG");
}

void SinglePointScreen::onTick()
{
    if (m_phase != Phase::Measuring && m_phase != Phase::Stopping) return;
    pumpEvents();
    if (m_phase == Phase::Stopping && m_pendingId == 0) submitStop();
}

void SinglePointScreen::onEvent(InputEvent e)
{
    if (m_phase == Phase::Measuring || m_phase == Phase::Stopping) {
        if (e == InputEvent::Back) {
            m_cancelReq = true;
            if (m_phase == Phase::Measuring) lcrServiceRequestCancel();
            drawRun("STOP AFTER POINT");
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
    m_freq.onEvent(e);
    drawConfig();
}
