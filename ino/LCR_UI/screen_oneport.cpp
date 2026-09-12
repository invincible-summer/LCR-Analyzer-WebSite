// ============================================================================
// screen_oneport.cpp —— 模式 2：单端口扫频 -> seal -> BLE 上传网站拟合
// ----------------------------------------------------------------------------
// sweep 开始前若 BLE 开启则 stop/deinit(false)；测量窗口射频静默；全部采样
// + StopTone + seal 后才允许 BLE。主 loop 是 RadioManager::poll() 唯一 owner，
// screen 自身只做 200ms 显示刷新，避免一次 loop 重复 pump 通知队列。
// ============================================================================

#include "screens.h"
#include "radio_manager.h"

#include <Arduino.h>
#include <stdio.h>

OnePortScreen screenOnePort;

namespace {
constexpr int kCfgX = 5;
constexpr int kCfgY0 = 31;
constexpr int kCfgDY = 36;
}

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
    ui::topBar("ONE-PORT Z", radio.state() != RadioState::Off);

    DigitEditor* eds[3] = {&m_f0, &m_f1, &m_ppd};
    const char* labels[3] = {"F0", "F1", "PPD"};
    for (int i = 0; i < 3; ++i) {
        const bool focused = (m_field == i);
        const int y = kCfgY0 + i * kCfgDY;
        tft.setTextFont(1);
        tft.setTextColor(focused ? ui::C_ACCENT : ui::C_DIM, ui::C_BG);
        tft.drawString(labels[i], kCfgX, y + 8);
        int ex = tft.width() - eds[i]->width(26) - 5;
        if (ex < 28) ex = 28;
        eds[i]->draw(ex, y, 26, focused);
    }

    if (millis() < m_errUntilMs) {
        tft.setTextFont(1);
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        tft.drawCentreString("NEED F0<F1 10Hz..10k", tft.width() / 2, 132, 1);
    }
    ui::bottomHint("ENC:EDIT UD:FLD OK:RUN");
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
    ui::topBar("Z SWEEP", false);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString("RADIO OFF", 7, 24);
    tft.drawString("f:", 7, 48);
    tft.drawString("pts:", 7, 66);
    tft.drawString("err:", 7, 84);
    ui::progressBar(7, 108, tft.width() - 14, 12, 0.0, ui::C_ACCENT);
    ui::bottomHint("BACK:STOP AFTER BLOCK");
}

void OnePortScreen::updateRun(bool stopping)
{
    char buf[28];
    tft.setTextFont(1);
    tft.setTextColor(ui::C_FG, ui::C_BG);
    tft.fillRect(30, 46, tft.width() - 34, 55, ui::C_BG);
    snprintf(buf, sizeof(buf), "%.6g Hz", sweep.currentFreqHz());
    tft.drawString(buf, 30, 48);
    snprintf(buf, sizeof(buf), "%d/%d", (int)sweep.completedPoints(),
             (int)sweep.totalPoints());
    tft.drawString(buf, 30, 66);
    snprintf(buf, sizeof(buf), "%d", (int)sweep.errorCount());
    tft.drawString(buf, 30, 84);
    if (stopping) {
        tft.fillRect(7, 24, tft.width() - 14, 12, ui::C_BG);
        tft.setTextColor(ui::C_CH2, ui::C_BG);
        tft.drawString("STOPPING...", 7, 24);
    }
    ui::progressBar(7, 108, tft.width() - 14, 12,
                    sweep.totalPoints()
                        ? (double)sweep.completedPoints() / sweep.totalPoints()
                        : 0.0,
                    ui::C_ACCENT);
}

void OnePortScreen::drawReady()
{
    const OnePortDataset* d = sweep.sealedOnePortDataset();
    char buf[34];
    tft.fillScreen(ui::C_BG);
    ui::topBar("Z SWEEP SEALED", false);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    if (d) {
        snprintf(buf, sizeof(buf), "points %d  err %d", d->nPoints, d->diag.failedPoints);
        tft.drawString(buf, 6, 28);
        snprintf(buf, sizeof(buf), "bytes %lu", (unsigned long)d->csvLen);
        tft.drawString(buf, 6, 46);
        snprintf(buf, sizeof(buf), "CRC %08lX", (unsigned long)d->crc32);
        tft.drawString(buf, 6, 64);
        tft.drawString(d->calibrationState, 6, 82);
    } else {
        tft.drawString("NO DATASET", 6, 28);
    }
    tft.drawString("CSV f,re,im Ohm", 6, 108);
    ui::bottomHint("OK:BLE  BACK:CONFIG");
}

void OnePortScreen::drawBle()
{
    char buf[28];
    tft.fillScreen(ui::C_BG);
    ui::topBar("BLE UPLOAD", true);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString(radioStateText(radio.state()), 7, 30);
    snprintf(buf, sizeof(buf), "%lu/%lu B", (unsigned long)radio.bytesSent(),
             (unsigned long)radio.bytesTotal());
    tft.drawString(buf, 7, 50);
    ui::progressBar(7, 76, tft.width() - 14, 12,
                    radio.bytesTotal() ? (double)radio.bytesSent() / radio.bytesTotal() : 0.0,
                    ui::C_OK);
    if (radio.transferComplete()) tft.drawString("DONE - SITE CAN FIT", 7, 102);
    ui::bottomHint("BACK:STOP BLE");
}

void OnePortScreen::onTick()
{
    if (m_phase == Phase::Run) {
        sweep.poll(millis());
        const SweepState st = sweep.state();
        updateRun(st == SweepState::Stopping);
        if (st == SweepState::TransferReady) {
            m_phase = Phase::Ready;
            drawReady();
        } else if (st == SweepState::Insufficient) {
            m_errUntilMs = millis() + 4000;
            m_phase = Phase::Config;
            drawConfig();
            tft.setTextFont(1);
            tft.setTextColor(ui::C_ERR, ui::C_BG);
            tft.drawCentreString("DATA INSUFFICIENT <4", tft.width() / 2, 132, 1);
        } else if (st == SweepState::Cancelled || st == SweepState::Error) {
            m_phase = Phase::Config;
            drawConfig();
        }
        return;
    }
    if (m_phase == Phase::Ble) {
        // radio.poll() is intentionally NOT called here. LCR_UI.loop owns it.
        static uint32_t lastDraw = 0;
        if (millis() - lastDraw > 200) {
            lastDraw = millis();
            char buf[28];
            tft.setTextFont(1);
            tft.fillRect(7, 30, tft.width() - 14, 56, ui::C_BG);
            tft.setTextColor(ui::C_FG, ui::C_BG);
            tft.drawString(radioStateText(radio.state()), 7, 30);
            snprintf(buf, sizeof(buf), "%lu/%lu B", (unsigned long)radio.bytesSent(),
                     (unsigned long)radio.bytesTotal());
            tft.drawString(buf, 7, 50);
            ui::progressBar(7, 76, tft.width() - 14, 12,
                            radio.bytesTotal() ? (double)radio.bytesSent() / radio.bytesTotal() : 0.0,
                            ui::C_OK);
            if (radio.transferComplete()) {
                tft.setTextColor(ui::C_OK, ui::C_BG);
                tft.drawString("DONE - SITE CAN FIT", 7, 102);
            }
        }
    }
}

void OnePortScreen::onEvent(InputEvent e)
{
    if (m_phase == Phase::Run) {
        if (e == InputEvent::Back) sweep.cancel();
        return;
    }
    if (m_phase == Phase::Ready) {
        if (e == InputEvent::Ok) {
            const OnePortDataset* d = sweep.sealedOnePortDataset();
            if (d && radio.startBleForSealedDataset(*d)) {
                m_phase = Phase::Ble;
                drawBle();
            }
            return;
        }
        if (e == InputEvent::Back) {
            m_phase = Phase::Config;
            drawConfig();
        }
        return;
    }
    if (m_phase == Phase::Ble) {
        if (e == InputEvent::Back) {
            radio.stopBle();
            m_phase = Phase::Config;
            drawConfig();
        }
        return;
    }

    if (e == InputEvent::Ok) { startSweep(); return; }
    if (e == InputEvent::Back) { screens.pop(); return; }

    DigitEditor* eds[3] = {&m_f0, &m_f1, &m_ppd};
    if (!eds[m_field]->onEvent(e)) {
        if (e == InputEvent::Down && m_field < 2) { ++m_field; eds[m_field]->setCursor(0); }
        else if (e == InputEvent::Up && m_field > 0) { --m_field; eds[m_field]->setCursor(m_field < 2 ? 4 : 1); }
    }
    drawConfig();
}
