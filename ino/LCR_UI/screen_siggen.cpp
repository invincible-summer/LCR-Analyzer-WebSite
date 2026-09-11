// ============================================================================
// screen_siggen.cpp —— 隐藏诊断页：信号发生器（经 lcr_api wrapper 调 DNT）
// ----------------------------------------------------------------------------
// v4.1.0 重构：旧激励链已删除。本页只向测量服务提交
// SetTone / StopTone job（Worker 内 = lcr_api_set_freq(f) / (0)），
// LCD_CAM+电阻网络 DAC 由 DNT 驱动。离开页面必停激励。
// 测量业务不经过本页（仅诊断用途）。
// ============================================================================

#include "screens.h"
#include "radio_lock.h"

#include <Arduino.h>
#include <stdio.h>

SigGenScreen screenSigGen;

namespace {
constexpr int kCfgX = 10;
constexpr int kFreqY = 40;
}

void SigGenScreen::onEnter()
{
    static bool inited = false;
    if (!inited) {
        m_freq.setup((int32_t)INSTRUMENT_F_MIN_HZ, (int32_t)INSTRUMENT_F_MAX_HZ,
                     5, 1000);
        inited = true;
    }
    m_running = false;
    m_pending = false;
    m_actualHz = 0;
    drawStatic();
}

void SigGenScreen::drawStatic()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("DIAG  SIGNAL GEN", false);

    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString("FREQ Hz", kCfgX, kFreqY - 12);
    drawFreq();
    drawStatus();
    ui::bottomHint("ENC:EDIT OK:TOGGLE BACK:EXIT");
}

void SigGenScreen::drawFreq()
{
    m_freq.draw(kCfgX, kFreqY, 26, !m_running);
}

void SigGenScreen::drawStatus()
{
    char buf[36];
    tft.fillRect(0, 80, tft.width(), 30, ui::C_BG);
    tft.setTextFont(1);
    if (m_running) {
        tft.setTextColor(ui::C_OK, ui::C_BG);
        snprintf(buf, sizeof(buf), "OUT  actual %.6g Hz", m_actualHz);
        tft.drawString(buf, kCfgX, 84);
    } else if (m_pending) {
        tft.setTextColor(ui::C_CH2, ui::C_BG);
        tft.drawString("STARTING...", kCfgX, 84);
    } else {
        tft.setTextColor(ui::C_DIM, ui::C_BG);
        tft.drawString("OUTPUT STOPPED", kCfgX, 84);
    }
    if (millis() < m_errUntilMs) {
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        tft.drawString("FREQ SET FAIL", kCfgX, 96);
    }
}

void SigGenScreen::startOutput()
{
    if (m_pending) return;
    LcrJob job{};
    job.kind = LcrJobKind::SetTone;
    job.frequencyHz = (double)m_freq.value();
    if (!lcrServiceSubmit(job)) return;
    m_pending = true;
    radioLockNotifyMeasurementActive(true);   // 激励窗口开启（射频静默）
    drawStatus();
}

void SigGenScreen::stopOutput()
{
    if (m_pending) return;
    LcrJob job{};
    job.kind = LcrJobKind::StopTone;
    if (!lcrServiceSubmit(job)) return;
    m_pending = true;
    m_running = false;
    drawFreq();
    drawStatus();
}

// 消费 SetTone/StopTone 事件（其它事件直接吞掉，保证队列不积压）
void SigGenScreen::pumpEvents()
{
    LcrEvent ev;
    while (lcrServiceTakeEvent(ev)) {
        if (ev.kind == LcrJobKind::SetTone && m_pending) {
            m_pending = false;
            if (ev.backendStatus == 0) {
                m_running = true;
                m_actualHz = ev.actualHz;
            } else {
                radioLockNotifyMeasurementActive(false);
                m_errUntilMs = millis() + 2000;
            }
            drawFreq();
            drawStatus();
        } else if (ev.kind == LcrJobKind::StopTone && m_pending) {
            m_pending = false;
            m_running = false;
            radioLockNotifyMeasurementActive(false);   // 激励窗口结束
            drawFreq();
            drawStatus();
        }
        // 其它事件（过期 job）：忽略
    }
}

void SigGenScreen::onTick() { pumpEvents(); }

void SigGenScreen::onEvent(InputEvent e)
{
    switch (e) {
    case InputEvent::Ok:
        if (m_running) stopOutput();
        else startOutput();
        return;
    case InputEvent::Back:
        stopOutput();                    // 离开页面必停激励（提交 StopTone）
        // 有界等待 StopTone 事件：确保 radio lock 在本页内解除，
        // 不把“测量活动”标志泄漏给后续界面（DNT 停激励很快）。
        {
            const uint32_t t0 = millis();
            while (m_pending && millis() - t0 < 1000) {
                pumpEvents();
                delay(2);
            }
            if (m_pending)               // 超时兜底：事件稍后由别的界面吞掉
                radioLockNotifyMeasurementActive(false);
        }
        screens.pop();
        return;
    default:
        break;
    }
    if (!m_running && !m_pending) m_freq.onEvent(e);   // 运行中不改频率
    drawFreq();
}
