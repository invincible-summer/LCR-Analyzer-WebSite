// ============================================================================
// screen_oneport.cpp —— 模式 2：单端口扫频 -> seal -> BLE 上传网站拟合
// ----------------------------------------------------------------------------
// 硬约束（plan.md §7/§13）：
//   * sweep 开始前若 BLE 开启：disconnect -> deinit -> Off 确认后才启动；
//   * 测量窗口（含 chunk 之间）射频完全静默；
//   * 全部 chunk 完成 + StopTone 完成 + seal 后，用户按键才开 BLE；
//   * 上传内容是网站拟合所需的 f,re,im CSV（f_act），不是原始波形；
//   * 有效点 <4 不进入 BLE（DATA INSUFFICIENT，允许重新测量）。
// ============================================================================

#include "screens.h"
#include "radio_manager.h"

#include <Arduino.h>
#include <stdio.h>

OnePortScreen screenOnePort;

namespace {
constexpr int kCfgX = 8;
constexpr int kCfgY0 = 30, kCfgDY = 30;
}

// ---------------------------------------------------------------------------
void OnePortScreen::onEnter()
{
    static bool inited = false;
    if (!inited) {
        m_f0.setup((int32_t)INSTRUMENT_F_MIN_HZ, 9999, 5, 100);
        m_f1.setup(11, (int32_t)INSTRUMENT_F_MAX_HZ, 5, 2000);
        m_ppd.setup(1, 50, 2, 10);
        inited = true;
    }
    m_field = 0;
    m_phase = Phase::Config;
    drawConfig();
}

void OnePortScreen::drawConfig()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("ONE-PORT  Z SWEEP", radio.state() != RadioState::Off);

    DigitEditor* eds[3] = {&m_f0, &m_f1, &m_ppd};
    const char* labels[3] = {"START Hz", "STOP Hz", "PTS/DEC"};
    for (int i = 0; i < 3; ++i) {
        const bool focused = (m_field == i);
        tft.setTextFont(1);
        tft.setTextColor(focused ? ui::C_ACCENT : ui::C_DIM, ui::C_BG);
        tft.drawString(labels[i], kCfgX, kCfgY0 + i * kCfgDY + 8);
        eds[i]->draw(kCfgX + 78, kCfgY0 + i * kCfgDY, 26, focused);
    }

    if (millis() < m_errUntilMs) {
        tft.setTextFont(1);
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        tft.drawCentreString("NEED F0<F1 IN 10..10k", tft.width() / 2, 122, 1);
    }
    ui::bottomHint("ENC:EDIT UD:FLD OK:RUN BACK:EXIT");
}

bool OnePortScreen::startSweep()
{
    const int32_t f0 = m_f0.value(), f1 = m_f1.value();
    if (f0 >= f1 || f0 < (int32_t)INSTRUMENT_F_MIN_HZ ||
        f1 > (int32_t)INSTRUMENT_F_MAX_HZ) {
        m_errUntilMs = millis() + 2000;
        drawConfig();
        return false;
    }
    // 强 invariant：已连接 BLE 时开始新测量，先 disconnect/deinit 再采集
    if (radio.state() != RadioState::Off) {
        radio.stopBle();
        if (radio.state() != RadioState::Off) return false;
    }

    SweepConfig cfg{};
    cfg.kind = MeasurementKind::OnePortImpedance;
    cfg.fStartHz = (double)f0;
    cfg.fStopHz = (double)f1;
    cfg.pointsPerDecade = (uint16_t)m_ppd.value();
    cfg.maxPoints = SWEEP_MAX_POINTS;
    if (sweep.start(cfg) != SweepStatus::Ok) {
        m_errUntilMs = millis() + 2000;
        drawConfig();
        return false;
    }
    m_phase = Phase::Run;
    drawRun();
    return true;
}

void OnePortScreen::drawRun()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("Z SWEEP 1-PORT", false);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString("RADIO OFF  BLE AFTER SWEEP", 8, 22);
    tft.drawString("f:", 8, 40);
    tft.drawString("pts:", 8, 54);
    tft.drawString("err:", 8, 68);
    ui::progressBar(8, 88, tft.width() - 16, 10, 0.0, ui::C_ACCENT);
    ui::bottomHint("BACK:STOP AFTER BLOCK");
}

void OnePortScreen::updateRun(bool stopping)
{
    char buf[28];
    tft.setTextFont(1);
    tft.setTextColor(ui::C_FG, ui::C_BG);
    snprintf(buf, sizeof(buf), "%.6g", sweep.currentFreqHz());
    tft.drawString(buf, 34, 40);
    snprintf(buf, sizeof(buf), "%d/%d", (int)sweep.completedPoints(),
             (int)sweep.totalPoints());
    tft.drawString(buf, 34, 54);
    snprintf(buf, sizeof(buf), "%d", (int)sweep.errorCount());
    tft.drawString(buf, 40, 68);
    if (stopping) {
        tft.fillRect(8, 22, tft.width() - 16, 12, ui::C_BG);
        tft.setTextColor(ui::C_CH2, ui::C_BG);
        tft.drawString("STOPPING AFTER BLOCK", 8, 22);
    }
    ui::progressBar(8, 88, tft.width() - 16, 10,
                    sweep.totalPoints()
                        ? (double)sweep.completedPoints() / sweep.totalPoints()
                        : 0.0,
                    ui::C_ACCENT);
}

void OnePortScreen::drawReady()
{
    const OnePortDataset* d = sweep.sealedOnePortDataset();
    char buf[30];
    tft.fillScreen(ui::C_BG);
    ui::topBar("SWEEP  SEALED", false);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    if (d) {
        snprintf(buf, sizeof(buf), "points %d  err %d", d->nPoints,
                 d->diag.failedPoints);
        tft.drawString(buf, 8, 24);
        snprintf(buf, sizeof(buf), "bytes %lu  CRC %08lX",
                 (unsigned long)d->csvLen, (unsigned long)d->crc32);
        tft.drawString(buf, 8, 36);
        tft.drawString(d->calibrationState, 8, 48);
    } else {
        tft.drawString("NO DATASET", 8, 24);
    }
    tft.drawString("CSV: f,re,im (f_act)", 8, 62);
    ui::bottomHint("OK:BLE UPLOAD  BACK:CONFIG");
}

void OnePortScreen::drawBle()
{
    char buf[28];
    tft.fillScreen(ui::C_BG);
    ui::topBar("BLE  UPLOAD", true);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString(radioStateText(radio.state()), 8, 26);
    snprintf(buf, sizeof(buf), "%lu/%lu B", (unsigned long)radio.bytesSent(),
             (unsigned long)radio.bytesTotal());
    tft.drawString(buf, 8, 40);
    ui::progressBar(8, 58, tft.width() - 16, 10,
                    radio.bytesTotal()
                        ? (double)radio.bytesSent() / radio.bytesTotal()
                        : 0.0,
                    ui::C_OK);
    if (radio.transferComplete())
        tft.drawString("DONE - SITE CAN FIT", 8, 78);
    ui::bottomHint("BACK:STOP BLE+EXIT");
}

// ---------------------------------------------------------------------------
void OnePortScreen::onTick()
{
    if (m_phase == Phase::Run) {
        sweep.poll(millis());       // 内部消费测量事件并推进 chunk/StopTone
        const SweepState st = sweep.state();
        updateRun(st == SweepState::Stopping);
        if (st == SweepState::TransferReady) {
            m_phase = Phase::Ready;
            drawReady();
        } else if (st == SweepState::Insufficient) {
            // 有效点 <4：不生成可供网站拟合的数据集（plan.md §7.4）
            m_errUntilMs = millis() + 4000;
            m_phase = Phase::Config;
            drawConfig();
            tft.setTextFont(1);
            tft.setTextColor(ui::C_ERR, ui::C_BG);
            tft.drawCentreString("DATA INSUFFICIENT (<4 PTS)", tft.width() / 2, 108, 1);
        } else if (st == SweepState::Cancelled || st == SweepState::Error) {
            m_phase = Phase::Config;
            drawConfig();
        }
        return;
    }
    if (m_phase == Phase::Ble) {
        radio.poll();
        // 轻量刷新（200ms 节流）
        static uint32_t lastDraw = 0;
        if (millis() - lastDraw > 200) {
            lastDraw = millis();
            char buf[28];
            tft.setTextFont(1);
            tft.setTextColor(ui::C_FG, ui::C_BG);
            tft.fillRect(8, 26, tft.width() - 16, 12, ui::C_BG);
            tft.drawString(radioStateText(radio.state()), 8, 26);
            snprintf(buf, sizeof(buf), "%lu/%lu B", (unsigned long)radio.bytesSent(),
                     (unsigned long)radio.bytesTotal());
            tft.fillRect(8, 40, tft.width() - 16, 12, ui::C_BG);
            tft.drawString(buf, 8, 40);
            ui::progressBar(8, 58, tft.width() - 16, 10,
                            radio.bytesTotal()
                                ? (double)radio.bytesSent() / radio.bytesTotal()
                                : 0.0,
                            ui::C_OK);
            if (radio.transferComplete()) {
                tft.setTextColor(ui::C_OK, ui::C_BG);
                tft.drawString("DONE - SITE CAN FIT", 8, 78);
            }
        }
    }
}

// ---------------------------------------------------------------------------
void OnePortScreen::onEvent(InputEvent e)
{
    if (m_phase == Phase::Run) {
        if (e == InputEvent::Back) sweep.cancel();   // 当前块完成后停止
        return;
    }
    if (m_phase == Phase::Ready) {
        switch (e) {
        case InputEvent::Ok: {
            const OnePortDataset* d = sweep.sealedOnePortDataset();
            if (d && radio.startBleForSealedDataset(*d)) {
                m_phase = Phase::Ble;
                drawBle();
            }
            return;
        }
        case InputEvent::Back:
            m_phase = Phase::Config;
            drawConfig();
            return;
        default: return;
        }
    }
    if (m_phase == Phase::Ble) {
        if (e == InputEvent::Back) {
            radio.stopBle();                     // disconnect -> deinit -> Off
            m_phase = Phase::Config;
            drawConfig();
        }
        return;
    }

    // ---- 配置页 ------------------------------------------------------------
    if (e == InputEvent::Ok) { startSweep(); return; }
    if (e == InputEvent::Back) { screens.pop(); return; }

    DigitEditor* eds[3] = {&m_f0, &m_f1, &m_ppd};
    if (!eds[m_field]->onEvent(e)) {
        if (e == InputEvent::Down && m_field < 2) { ++m_field; eds[m_field]->setCursor(0); }
        else if (e == InputEvent::Up && m_field > 0) { --m_field; eds[m_field]->setCursor(2); }
    }
    drawConfig();
}
