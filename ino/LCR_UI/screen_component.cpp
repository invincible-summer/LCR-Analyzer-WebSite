// ============================================================================
// screen_component.cpp —— 模式 1：单元件 R/C/L 自动识别与测量
// ----------------------------------------------------------------------------
// 5 个几何频点，每点向测量服务提交一个 MeasureAndCalcZ job；结束后由
// summarizeComponent 做 apiType 一致性判型 + 中位数聚合。本界面不启动 BLE。
// 取消语义：Back -> 当前单点完成 -> StopTone completion -> 返回配置页。
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
}

// ---------------------------------------------------------------------------
void ComponentScreen::onEnter()
{
    static bool inited = false;
    if (!inited) {
        // 产品频段真源是 measurement_types.h 的 10 Hz..10 kHz；旧 50 Hz
        // 下限只是 UI 硬编码，不是测量核心限制。
        m_f0.setup((int32_t)INSTRUMENT_F_MIN_HZ, 9999, 5, 100);
        m_f1.setup(11, (int32_t)INSTRUMENT_F_MAX_HZ, 5, 2000);
        inited = true;
    }
    m_phase = Phase::Config;
    m_field = 0;
    m_nextIdx = 0;
    m_nPts = 0;
    m_pendingId = 0;
    m_cancelReq = false;
    drawConfig();
}

void ComponentScreen::drawConfig()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("COMPONENT R/C/L", false);

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
    ui::bottomHint("ENC:EDIT OK:MEAS BACK");
}

// ---------------------------------------------------------------------------
bool ComponentScreen::startMeasure()
{
    const int32_t f0 = m_f0.value(), f1 = m_f1.value();
    if (f0 >= f1 || f0 < (int32_t)INSTRUMENT_F_MIN_HZ ||
        f1 > (int32_t)INSTRUMENT_F_MAX_HZ) {
        m_errUntilMs = millis() + 2000;
        drawConfig();
        return false;
    }
    // 5 个几何分布频点
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

    m_nPts = 0;
    m_nextIdx = 0;
    m_pendingId = 0;
    m_cancelReq = false;
    m_phase = Phase::Run;
    radioLockNotifyMeasurementActive(true);
    submitNext();
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
    ui::topBar("COMPONENT R/C/L", false);
    tft.setTextFont(2);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawCentreString("MEASURING...", tft.width() / 2, 48, 2);
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
    char v1[20], v2[20], buf[48];
    tft.fillScreen(ui::C_BG);
    const bool ok = m_est.type != ComponentEstimate::Type::Unknown;
    ui::topBar(ok ? "RESULT" : "INCONCLUSIVE", false);

    tft.setTextFont(4);
    tft.setTextColor(ok ? ui::C_OK : ui::C_ERR, ui::C_BG);
    tft.drawCentreString(componentTypeText(m_est.type), tft.width() / 2, 24, 4);

    tft.setTextFont(2);
    switch (m_est.type) {
    case ComponentEstimate::Type::Resistor:
        ui::row(7, 62, tft.width() - 14, "R",
                ui::fmtEng(m_est.rOhm, "Ohm", v1, sizeof(v1)), ui::C_FG);
        break;
    case ComponentEstimate::Type::Capacitor:
        ui::row(7, 62, tft.width() - 14, "C",
                ui::fmtEng(m_est.cFarad, "F", v1, sizeof(v1)), ui::C_FG);
        break;
    case ComponentEstimate::Type::Inductor:
        ui::row(7, 58, tft.width() - 14, "L",
                ui::fmtEng(m_est.lHenry, "H", v1, sizeof(v1)), ui::C_FG);
        snprintf(buf, sizeof(buf), "%s%s",
                 ui::fmtEng(m_est.dcrOhm, "Ohm", v2, sizeof(v2)),
                 m_est.dcrWarn ? "!" : "");
        ui::row(7, 84, tft.width() - 14, "DCR", buf,
                m_est.dcrWarn ? ui::C_ERR : ui::C_FG);
        break;
    default:
        tft.setTextColor(ui::C_DIM, ui::C_BG);
        snprintf(buf, sizeof(buf), "%s (n=%u)", m_est.reason, m_est.nValid);
        tft.drawCentreString(buf, tft.width() / 2, 64, 1);
        break;
    }

    tft.setTextFont(1);
    tft.setTextColor(ok ? ui::C_OK : ui::C_DIM, ui::C_BG);
    snprintf(buf, sizeof(buf), "valid %u/%u  consistent %u/%u",
             m_est.nValid, m_nPlan, m_est.nConsistent, m_est.nValid);
    tft.drawCentreString(buf, tft.width() / 2, 120, 1);
    ui::bottomHint("OK:AGAIN BACK:CONFIG");
}

// ---------------------------------------------------------------------------
void ComponentScreen::pumpEvents()
{
    LcrEvent ev;
    while (lcrServiceTakeEvent(ev)) {
        if (ev.id != m_pendingId) continue;
        m_pendingId = 0;

        if (ev.kind == LcrJobKind::MeasureAndCalcZ) {
            const AppZPoint& p = ev.z[0];
            const bool ok = p.apiStatus == 0 && ev.calc.apiStatus == 0 &&
                            isfinite(p.fAct) && p.fAct > 0.0 &&
                            isfinite(p.reOhm) && isfinite(p.imOhm);
            if (ok && m_nPts < 5) {
                m_z[m_nPts] = p;
                m_calc[m_nPts] = ev.calc;
                ++m_nPts;
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
    m_est = summarizeComponent(m_z, m_calc, m_nPts);
    m_phase = Phase::Result;
    drawResult();
}

void ComponentScreen::onTick()
{
    if (m_phase != Phase::Run) return;
    pumpEvents();
    if (m_phase != Phase::Run) return;
    if (m_pendingId != 0) return;

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
        switch (e) {
        case InputEvent::Ok: startMeasure(); return;
        case InputEvent::Back: onEnter(); return;
        default: return;
        }
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
