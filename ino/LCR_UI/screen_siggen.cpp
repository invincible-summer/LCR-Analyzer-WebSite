// ============================================================================
// screen_siggen.cpp —— 隐藏诊断页：信号发生器（I2S 激励直接输出）
// ----------------------------------------------------------------------------
// v4.1：不占顶层菜单（主菜单 3 秒内连按 3 次 Up 进入）。仅诊断用途：
// 验证 I2S/PCM5102 通路、精确频率规划与停止静音。测量业务不经过本页。
// 波形固定 Sine（plan.md §2.3：其它波形只留 diagnostics 地位，本页未开）。
// ============================================================================

#include "screens.h"
#include "excitation_driver.h"

#include <Arduino.h>
#include <stdio.h>

SigGenScreen screenSigGen;

namespace {
constexpr int kCfgX = 10;
constexpr int kFreqY = 40;
}  // namespace

void SigGenScreen::onEnter()
{
    static bool inited = false;
    if (!inited) {
        m_freq.setup((int32_t)INSTRUMENT_F_MIN_HZ, (int32_t)INSTRUMENT_F_MAX_HZ,
                     5, 1000);
        inited = true;
    }
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
    char buf[32];
    tft.fillRect(0, 80, tft.width(), 30, ui::C_BG);
    tft.setTextFont(1);
    if (m_running) {
        tft.setTextColor(ui::C_OK, ui::C_BG);
        snprintf(buf, sizeof(buf), "OUT  actual %.6g Hz", m_actualHz);
        tft.drawString(buf, kCfgX, 84);
    } else {
        tft.setTextColor(ui::C_DIM, ui::C_BG);
        tft.drawString("OUTPUT STOPPED", kCfgX, 84);
    }
    if (millis() < m_errUntilMs) {
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        tft.drawString("EXCITATION FAIL", kCfgX, 96);
    }
}

void SigGenScreen::startOutput()
{
    ExcitationConfig cfg{};
    cfg.requestedHz = (double)m_freq.value();
    cfg.waveform = Waveform::Sine;
    if (excitationDriver.excitationBegin(cfg) != ExcitationStatus::Ok) {
        m_errUntilMs = millis() + 2000;
        m_running = false;
        drawStatus();
        return;
    }
    m_actualHz = excitationDriver.excitationState().actualHz;
    m_running = true;
    drawFreq();
    drawStatus();
}

void SigGenScreen::stopOutput()
{
    excitationDriver.excitationStop();
    m_running = false;
    drawFreq();
    drawStatus();
}

void SigGenScreen::onTick() {}

void SigGenScreen::onEvent(InputEvent e)
{
    switch (e) {
    case InputEvent::Ok:
        if (m_running) stopOutput();
        else startOutput();
        return;
    case InputEvent::Back:
        excitationDriver.excitationStop();     // 离开页面必停激励
        screens.pop();
        return;
    default:
        break;
    }
    if (!m_running) m_freq.onEvent(e);          // 运行中不改频率
    drawFreq();
}
