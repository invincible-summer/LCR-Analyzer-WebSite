// ============================================================================
// screen_component.cpp —— 模式 1：单元件 R/C/L 自动识别与测量
// ----------------------------------------------------------------------------
// 5 个几何分布频点（默认 100 Hz ~ 2 kHz 质量保证频段）全部走同一个
// MeasurementEngine(OnePortImpedance)，再由 classifyComponent 在
// R / C / (L+DCR) 三模型中比较。不确定时明确显示 UNKNOWN / OUT OF RANGE。
// 此模式不启动 BLE（plan.md §5.1）。
//
// 测量全程 onTick 驱动（poll-driven）：Back 立即 cancel -> safe-off。
// ============================================================================

#include "screens.h"

#include <Arduino.h>
#include <math.h>

ComponentScreen screenComponent;

namespace {
constexpr int kCfgX = 10;
constexpr int kCfgY0 = 34, kCfgDY = 34;
}  // namespace

// ---------------------------------------------------------------------------
void ComponentScreen::onEnter()
{
    static bool inited = false;
    if (!inited) {
        m_f0.setup(50, 5000, 4, 100);
        m_f1.setup(100, (int32_t)DUAL_CHANNEL_QUALITY_FMAX_HZ, 4,
                   (int32_t)DUAL_CHANNEL_QUALITY_FMAX_HZ);
        inited = true;
    }
    m_phase = Phase::Config;
    m_field = 0;
    m_nextIdx = 0;
    m_nPts = 0;
    drawConfig();
}

void ComponentScreen::drawConfig()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("COMPONENT  R/C/L", false);

    DigitEditor* eds[2] = {&m_f0, &m_f1};
    const char* labels[2] = {"F START", "F STOP"};
    for (int i = 0; i < 2; ++i) {
        const bool focused = (m_field == i);
        tft.setTextFont(1);
        tft.setTextColor(focused ? ui::C_ACCENT : ui::C_DIM, ui::C_BG);
        tft.drawString(labels[i], kCfgX, kCfgY0 + i * kCfgDY + 8);
        eds[i]->draw(kCfgX + 66, kCfgY0 + i * kCfgDY, 26, focused);
        tft.drawString("Hz", kCfgX + 128, kCfgY0 + i * kCfgDY + 8);
    }

    if (millis() < m_errUntilMs) {
        tft.setTextFont(1);
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        tft.drawCentreString("NEED F0<F1 IN 50..2000", tft.width() / 2, 104, 1);
    }
    ui::bottomHint("ENC:EDIT UD:FLD OK:MEAS BACK:EXIT");
}

// ---------------------------------------------------------------------------
bool ComponentScreen::startMeasure()
{
    const int32_t f0 = m_f0.value(), f1 = m_f1.value();
    if (f0 >= f1) {
        m_errUntilMs = millis() + 2000;
        drawConfig();
        return false;
    }
    // 5 个对数分布频点（plan.md §5.1：默认 3~5 个几何分布有效频点）
    SweepConfig cfg{};
    cfg.kind = MeasurementKind::OnePortImpedance;
    cfg.fStartHz = (double)f0;
    cfg.fStopHz = (double)f1;
    cfg.pointsPerDecade = 12;
    cfg.maxPoints = 5;
    cfg.logSpacing = true;
    m_nPlan = (uint8_t)buildFrequencyPlan(cfg, m_plan, 5);

    m_nPts = 0;
    m_nextIdx = 0;
    m_phase = Phase::Run;
    drawRun();
    return true;
}

void ComponentScreen::drawRun()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("COMPONENT  R/C/L", false);
    tft.setTextFont(2);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawCentreString("MEASURING...", tft.width() / 2, 44, 2);
    ui::progressBar(20, 70, tft.width() - 40, 10, 0.0, ui::C_ACCENT);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawCentreString("0/0", tft.width() / 2, 88, 1);
    ui::bottomHint("BACK:ABORT");
}

void ComponentScreen::updateRunProgress()
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d/%d  %sHz", (int)m_nextIdx, (int)m_nPlan,
             m_nextIdx < m_nPlan ? "" : "");
    tft.fillRect(0, 86, tft.width(), 14, ui::C_BG);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_FG, ui::C_BG);
    tft.drawCentreString(buf, tft.width() / 2, 88, 1);
    ui::progressBar(20, 70, tft.width() - 40, 10,
                    m_nPlan ? (double)m_nextIdx / m_nPlan : 0.0, ui::C_ACCENT);
}

void ComponentScreen::drawResult()
{
    char buf[48], v1[20], v2[20];
    tft.fillScreen(ui::C_BG);
    const bool ok = m_est.type == ComponentEstimate::Type::Resistor ||
                    m_est.type == ComponentEstimate::Type::Capacitor ||
                    m_est.type == ComponentEstimate::Type::Inductor;
    ui::topBar(ok ? "RESULT" : "INCONCLUSIVE", false);

    tft.setTextFont(4);
    tft.setTextColor(ok ? ui::C_OK : ui::C_ERR, ui::C_BG);
    tft.drawCentreString(componentTypeText(m_est.type), tft.width() / 2, 24, 4);

    tft.setTextFont(2);
    switch (m_est.type) {
    case ComponentEstimate::Type::Resistor:
        ui::row(10, 56, tft.width() - 20, "R",
                ui::fmtEng(m_est.rOhm, "Ohm", v1, sizeof(v1)), ui::C_FG);
        break;
    case ComponentEstimate::Type::Capacitor:
        ui::row(10, 56, tft.width() - 20, "C",
                ui::fmtEng(m_est.cFarad, "F", v1, sizeof(v1)), ui::C_FG);
        break;
    case ComponentEstimate::Type::Inductor:
        ui::row(10, 56, tft.width() - 20, "L",
                ui::fmtEng(m_est.lHenry, "H", v1, sizeof(v1)), ui::C_FG);
        ui::row(10, 76, tft.width() - 20, "DCR",
                ui::fmtEng(m_est.dcrOhm, "Ohm", v2, sizeof(v2)), ui::C_FG);
        break;
    default:
        tft.setTextColor(ui::C_DIM, ui::C_BG);
        snprintf(buf, sizeof(buf), "%s (n=%u)", m_est.reason, m_est.nValid);
        tft.drawCentreString(buf, tft.width() / 2, 60, 2);
        break;
    }

    // quality: GOOD/WARN（残差距门限的裕度；nValid 不足也降级）
    const bool good = ok && m_est.nValid >= 4 &&
                      m_est.bestWrmse < defaultComponentGates().maxRelWrmse * 0.5;
    tft.setTextFont(1);
    tft.setTextColor(good ? ui::C_OK : (ok ? ui::C_CH2 : ui::C_DIM), ui::C_BG);
    snprintf(buf, sizeof(buf), "quality: %s  wRMSE %.2f%%",
             good ? "GOOD" : (ok ? "WARN" : "-"), m_est.bestWrmse * 100.0);
    tft.drawCentreString(buf, tft.width() / 2, 104, 1);

    ui::bottomHint("OK:REMEASURE  BACK:CONFIG");
}

// ---------------------------------------------------------------------------
void ComponentScreen::onTick()
{
    if (m_phase != Phase::Run) return;

    if (engine.active()) {
        engine.poll(micros());
        if (engine.resultReady()) {
            OnePortPoint p;
            if (engine.takeResult(p) && m_nPts < 5) m_pts[m_nPts++] = p;
            engine.poll(micros());        // 推过 SafeOff -> Idle
            ++m_nextIdx;
            updateRunProgress();
        }
        return;
    }

    // 引擎空闲：失败收场 -> 记一个空点继续；或启动下一个频点
    if (m_nextIdx < m_nPlan) {
        MeasurementRequest req{};
        req.kind = MeasurementKind::OnePortImpedance;
        req.requestedHz = m_plan[m_nextIdx];
        req.settleCycles = 8;
        req.captureCycles = 12;
        req.minSamplesPerChannel = 200;
        engine.start(req);
        return;
    }

    m_est = classifyComponent(m_pts, m_nPts);
    m_phase = Phase::Result;
    drawResult();
}

// ---------------------------------------------------------------------------
void ComponentScreen::onEvent(InputEvent e)
{
    if (m_phase == Phase::Run) {
        if (e == InputEvent::Back) engine.cancel();   // 下一次 poll 即 safe-off
        return;
    }

    if (m_phase == Phase::Result) {
        switch (e) {
        case InputEvent::Ok: startMeasure(); return;
        case InputEvent::Back: onEnter(); return;
        default: return;
        }
    }

    // ---- 配置页 ------------------------------------------------------------
    if (e == InputEvent::Ok) { startMeasure(); return; }
    if (e == InputEvent::Back) { screens.pop(); return; }

    DigitEditor* eds[2] = {&m_f0, &m_f1};
    if (!eds[m_field]->onEvent(e)) {
        // 编辑器光标到边界：Up/Down 在两编辑器之间切换
        if (e == InputEvent::Down && m_field == 0) { m_field = 1; eds[1]->setCursor(0); }
        else if (e == InputEvent::Up && m_field == 1) { m_field = 0; eds[0]->setCursor(3); }
    }
    drawConfig();
}
