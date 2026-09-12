// ============================================================================
// screen_siggen.cpp —— 隐藏诊断页：信号发生器（经 lcr_api wrapper 调 DNT）
// ----------------------------------------------------------------------------
// v4.1.0 重构：旧激励链已删除。本页只向测量服务提交
// SetTone / StopTone job（Worker 内 = lcr_api_set_freq(f) / (0)），
// LCD_CAM+电阻网络 DAC 由 DNT 驱动。离开页面必停激励。
//
// 运行时约束：UI 不得同步等待硬件 completion。Back 只置退出请求；若
// SetTone 正在执行，先等其 completion，再异步提交 StopTone。只有收到
// 与当前 pending job 的 id+kind 都匹配的 StopTone completion 后，才解除
// radio lock 并退出页面。任何超时都不得把“未确认停机”伪装成已停机。
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
    m_exitRequested = false;
    m_pendingId = 0;
    m_pendingKind = LcrJobKind::ServiceInit;
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
    m_freq.draw(kCfgX, kFreqY, 26, !m_running && !m_pending && !m_exitRequested);
}

void SigGenScreen::drawStatus()
{
    char buf[36];
    tft.fillRect(0, 80, tft.width(), 30, ui::C_BG);
    tft.setTextFont(1);

    // 退出请求和 StopTone pending 都以 STOPPING 表示；m_running 在真正收到
    // StopTone completion 前保持 true，内部状态始终反映硬件“尚未确认停止”。
    if (m_exitRequested || (m_pending && m_pendingKind == LcrJobKind::StopTone)) {
        tft.setTextColor(ui::C_CH2, ui::C_BG);
        tft.drawString("STOPPING...", kCfgX, 84);
    } else if (m_pending && m_pendingKind == LcrJobKind::SetTone) {
        tft.setTextColor(ui::C_CH2, ui::C_BG);
        tft.drawString("STARTING...", kCfgX, 84);
    } else if (m_running) {
        tft.setTextColor(ui::C_OK, ui::C_BG);
        snprintf(buf, sizeof(buf), "OUT  actual %.6g Hz", m_actualHz);
        tft.drawString(buf, kCfgX, 84);
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
    if (m_pending || m_exitRequested) return;
    LcrJob job{};
    job.kind = LcrJobKind::SetTone;
    job.frequencyHz = (double)m_freq.value();
    if (!lcrServiceSubmit(job)) return;

    m_pending = true;
    m_pendingId = job.id;
    m_pendingKind = LcrJobKind::SetTone;
    // 从 SetTone 成功提交起就禁止 BLE，覆盖“硬件即将开始激励”的整个窗口。
    radioLockNotifyMeasurementActive(true);
    drawFreq();
    drawStatus();
}

void SigGenScreen::stopOutput()
{
    if (m_pending || !m_running) return;
    LcrJob job{};
    job.kind = LcrJobKind::StopTone;
    if (!lcrServiceSubmit(job)) return;

    m_pending = true;
    m_pendingId = job.id;
    m_pendingKind = LcrJobKind::StopTone;
    // 不在提交时清 m_running。只有 StopTone completion 才能证明硬件已停。
    drawFreq();
    drawStatus();
}

// 消费 SetTone/StopTone 事件。只有 id+kind 与当前 pending job 同时匹配才
// 能改变本页状态；其它过期/无关事件只排出队列，不能误完成当前动作。
void SigGenScreen::pumpEvents()
{
    LcrEvent ev;
    while (lcrServiceTakeEvent(ev)) {
        if (!m_pending || ev.id != m_pendingId || ev.kind != m_pendingKind)
            continue;

        const LcrJobKind completedKind = m_pendingKind;
        m_pending = false;
        m_pendingId = 0;
        m_pendingKind = LcrJobKind::ServiceInit;

        if (completedKind == LcrJobKind::SetTone) {
            if (ev.backendStatus == 0 && ev.actualHz > 0.0) {
                m_running = true;
                m_actualHz = ev.actualHz;
            } else {
                m_running = false;
                m_actualHz = 0;
                // SetTone 没有成功建立输出；此时才能安全解除激励窗口锁。
                radioLockNotifyMeasurementActive(false);
                m_errUntilMs = millis() + 2000;
            }
        } else if (completedKind == LcrJobKind::StopTone) {
            m_running = false;
            m_actualHz = 0;
            // StopTone completion 是唯一允许把 active -> false 的成功停机证据。
            radioLockNotifyMeasurementActive(false);
        }

        drawFreq();
        drawStatus();
    }
}

void SigGenScreen::onTick()
{
    pumpEvents();

    if (!m_exitRequested) return;
    if (m_pending) return;                    // 等当前 SetTone/StopTone completion

    if (m_running) {
        // Back 可能发生在 SetTone pending 期间。SetTone 成功后会走到这里，
        // 再提交 StopTone；队列暂满则下一圈自动重试，UI 始终非阻塞。
        stopOutput();
        return;
    }

    // 无 pending 且确认没有输出，才允许离开诊断页。
    screens.pop();
}

void SigGenScreen::onEvent(InputEvent e)
{
    if (m_exitRequested) return;              // 退出流程开始后不接受新动作

    switch (e) {
    case InputEvent::Ok:
        if (m_pending) return;
        if (m_running) stopOutput();
        else startOutput();
        return;
    case InputEvent::Back:
        m_exitRequested = true;
        drawFreq();
        drawStatus();
        // 不同步等待、不强制解锁。onTick 会等当前 job 完成；如硬件仍在输出，
        // 再提交 StopTone；只有 StopTone completion 后才 pop。
        if (!m_pending && !m_running)
            screens.pop();
        return;
    default:
        break;
    }

    if (!m_running && !m_pending) m_freq.onEvent(e);   // 运行中不改频率
    drawFreq();
}
