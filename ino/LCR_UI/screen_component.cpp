// ============================================================================
// screen_component.cpp —— 模式 1：单元件 R/C/L 自动识别与测量
// ----------------------------------------------------------------------------
// v4.1.0：5 个几何频点，每点向测量服务提交一个 MeasureAndCalcZ job
// （Worker 内 = lcr_api_measure_z + lcr_api_calc(apply_calib=false)），
// 结束后由 summarizeComponent 做 apiType 一致性判型 + 中位数聚合。
// 本界面不做任何物理测量，也不使用已删除旧链的 raw-ADC 质量权重。
// 此模式不启动 BLE（plan.md §6）。
//
// 取消语义（plan.md §5.3）：Back -> 标记取消 -> 当前单点测量完成 ->
// 不再提交下一点 -> StopTone 完成后才返回配置页（期间显示 STOPPING）。
// ============================================================================

#include "screens.h"
#include "radio_lock.h"

#include <Arduino.h>
#include <math.h>

ComponentScreen screenComponent;

namespace {
constexpr int kCfgX = 10;
constexpr int kCfgY0 = 34, kCfgDY = 34;
}

// ---------------------------------------------------------------------------
void ComponentScreen::onEnter()
{
    static bool inited = false;
    if (!inited) {
        m_f0.setup(50, 5000, 4, 100);
        m_f1.setup(100, (int32_t)INSTRUMENT_F_MAX_HZ, 5, 2000);
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
        tft.drawCentreString("NEED F0<F1 IN 50..10k", tft.width() / 2, 104, 1);
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
    // 5 个几何分布频点（plan.md §6.1：3~5 点）
    SweepConfig cfg{};
    cfg.kind = MeasurementKind::OnePortImpedance;
    cfg.fStartHz = (double)f0;
    cfg.fStopHz = (double)f1;
    cfg.pointsPerDecade = 12;          // 超出后整体压缩为 5 点均匀几何网格
    cfg.maxPoints = 5;
    m_nPlan = (uint8_t)buildFrequencyPlan(cfg, m_plan, 5);
    if (m_nPlan < 3) {                 // 网格异常（理论不可达）
        m_errUntilMs = millis() + 2000;
        drawConfig();
        return false;
    }

    m_nPts = 0;
    m_nextIdx = 0;
    m_pendingId = 0;
    m_cancelReq = false;
    m_phase = Phase::Run;
    radioLockNotifyMeasurementActive(true);   // 测量窗口开启（BLE 必须静默）
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
    if (!lcrServiceSubmit(job)) return false;   // 队列满：下轮 onTick 重试
    m_pendingId = job.id;
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
    ui::bottomHint("BACK:STOP AFTER POINT");
}

void ComponentScreen::updateRunProgress(bool stopping)
{
    char buf[40];
    tft.fillRect(0, 86, tft.width(), 14, ui::C_BG);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_FG, ui::C_BG);
    if (stopping)
        snprintf(buf, sizeof(buf), "STOPPING...");
    else
        snprintf(buf, sizeof(buf), "%d/%d", (int)m_nextIdx, (int)m_nPlan);
    tft.drawCentreString(buf, tft.width() / 2, 88, 1);
    ui::progressBar(20, 70, tft.width() - 40, 10,
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
    tft.drawCentreString(componentTypeText(m_est.type), tft.width() / 2, 22, 4);

    tft.setTextFont(2);
    switch (m_est.type) {
    case ComponentEstimate::Type::Resistor:
        ui::row(10, 52, tft.width() - 20, "R",
                ui::fmtEng(m_est.rOhm, "Ohm", v1, sizeof(v1)), ui::C_FG);
        break;
    case ComponentEstimate::Type::Capacitor:
        ui::row(10, 52, tft.width() - 20, "C",
                ui::fmtEng(m_est.cFarad, "F", v1, sizeof(v1)), ui::C_FG);
        break;
    case ComponentEstimate::Type::Inductor:
        ui::row(10, 52, tft.width() - 20, "L",
                ui::fmtEng(m_est.lHenry, "H", v1, sizeof(v1)), ui::C_FG);
        tft.setTextFont(2);
        tft.setTextColor(m_est.dcrWarn ? ui::C_ERR : ui::C_FG, ui::C_BG);
        snprintf(buf, sizeof(buf), "%s%s", ui::fmtEng(m_est.dcrOhm, "Ohm", v2, sizeof(v2)),
                 m_est.dcrWarn ? "  WARN<0" : "");
        ui::row(10, 74, tft.width() - 20, "DCR", buf,
                m_est.dcrWarn ? ui::C_ERR : ui::C_FG);
        break;
    default:
        tft.setTextColor(ui::C_DIM, ui::C_BG);
        snprintf(buf, sizeof(buf), "%s (n=%u)", m_est.reason, m_est.nValid);
        tft.drawCentreString(buf, tft.width() / 2, 56, 2);
        break;
    }

    // 数据真实支撑的质量表达（代替旧链的伪 wRMSE）：有效点/一致点计数
    tft.setTextFont(1);
    tft.setTextColor(ok ? ui::C_OK : ui::C_DIM, ui::C_BG);
    snprintf(buf, sizeof(buf), "valid %u/%u   consistent %u/%u",
             m_est.nValid, m_nPlan, m_est.nConsistent, m_est.nValid);
    tft.drawCentreString(buf, tft.width() / 2, 102, 1);

    ui::bottomHint("OK:REMEASURE  BACK:CONFIG");
}

// ---------------------------------------------------------------------------
// 事件泵：消费测量事件。匹配当前 pending id 的 MeasureAndCalcZ / StopTone
// 被处理；其它（过期/取消丢弃补发）直接吞掉，保证队列不积压。
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
            radioLockNotifyMeasurementActive(false);   // 测量窗口结束
            if (m_cancelReq) {
                m_cancelReq = false;
                m_phase = Phase::Config;               // 取消：回配置页
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

// ---------------------------------------------------------------------------
void ComponentScreen::onTick()
{
    if (m_phase != Phase::Run) return;
    pumpEvents();
    if (m_phase != Phase::Run) return;     // pumpEvents 内可能已迁移

    if (m_pendingId != 0) return;          // 等当前点结果

    if (m_nextIdx >= m_nPlan || m_cancelReq) {
        // 全部点完成或取消：停激励（DNT 测量后 tone 仍在输出，必须显式停）
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

// ---------------------------------------------------------------------------
void ComponentScreen::onEvent(InputEvent e)
{
    if (m_phase == Phase::Run) {
        // 取消 = 当前点测量完成后停止（plan.md §5.3，非瞬时）
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

    // ---- 配置页 ------------------------------------------------------------
    if (e == InputEvent::Ok) { startMeasure(); return; }
    if (e == InputEvent::Back) { screens.pop(); return; }

    DigitEditor* eds[2] = {&m_f0, &m_f1};
    if (!eds[m_field]->onEvent(e)) {
        if (e == InputEvent::Down && m_field == 0) { m_field = 1; eds[1]->setCursor(0); }
        else if (e == InputEvent::Up && m_field == 1) { m_field = 0; eds[0]->setCursor(3); }
    }
    drawConfig();
}
