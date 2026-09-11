// ============================================================================
// screen_twoport.cpp —— 模式 3：双端口扫频（H=Vout/Vin）-> seal -> BLE
// ----------------------------------------------------------------------------
// 全部测量经 SweepEngine -> ILcrService -> DNT lcr_api_sweep_w。
// canonical 数据是复数传递函数：re_h/im_h 由 API 的 h_mag/phase 纯数学
// 换算（H = |H|e^{jφ}）；网站侧从复 H 推导 Bode/Nyquist。
// W 链在 DNT 中为 raw chain / no calib —— CSV 头如实标注 raw_w_path，
// 不复用单端口校准状态暗示双端口已校准（plan.md §8.1）。
// ============================================================================

#include "screens.h"
#include "radio_manager.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>

TwoPortScreen screenTwoPort;

namespace {
constexpr int kCfgX = 8;
constexpr int kCfgY0 = 30, kCfgDY = 30;
}

// ---------------------------------------------------------------------------
void TwoPortScreen::onEnter()
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

void TwoPortScreen::drawConfig()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("TWO-PORT  H SWEEP", radio.state() != RadioState::Off);

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

bool TwoPortScreen::startSweep()
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
    cfg.kind = MeasurementKind::TwoPortTransfer;
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

void TwoPortScreen::drawRun()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("H SWEEP 2-PORT", false);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString("RADIO OFF  BLE AFTER SWEEP", 8, 22);
    tft.drawString("f:", 8, 40);
    tft.drawString("pts:", 8, 54);
    tft.drawString("err:", 8, 68);
    ui::progressBar(8, 88, tft.width() - 16, 10, 0.0, ui::C_ACCENT);
    ui::bottomHint("BACK:STOP AFTER BLOCK");
}

void TwoPortScreen::updateRun(bool stopping)
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

void TwoPortScreen::drawPreview()
{
    const TwoPortDataset* d = sweep.sealedTwoPortDataset();
    if (!d || d->nPoints < 2) return;
    static double f[SWEEP_MAX_POINTS], g[SWEEP_MAX_POINTS];
    double lo = 1e9, hi = -1e9;
    for (uint16_t i = 0; i < d->nPoints && i < SWEEP_MAX_POINTS; ++i) {
        f[i] = d->points[i].f;
        // Bode 幅度从复 H 推导（与网站一致）：20*log10|H|
        g[i] = 20.0 * log10(hypot(d->points[i].reH, d->points[i].imH));
        if (g[i] < lo) lo = g[i];
        if (g[i] > hi) hi = g[i];
    }
    m_plot.setup(24, 60, tft.width() - 44, 44);
    m_plot.setXRange(f[0], f[d->nPoints - 1]);
    m_plot.setMagRange(false, lo, hi + 0.5);
    m_plot.setLegend("gain dB", "");
    m_plot.drawFrame();
    m_plot.drawCurveMag(f, g, d->nPoints, ui::C_CH1);
}

void TwoPortScreen::drawReady()
{
    const TwoPortDataset* d = sweep.sealedTwoPortDataset();
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
        tft.drawString("H raw (no calib)", 8, 48);
    } else {
        tft.drawString("NO DATASET", 8, 24);
    }
    drawPreview();                       // 轻量 preview（网站才是完整真源）
    ui::bottomHint("OK:BLE UPLOAD  BACK:CONFIG");
}

void TwoPortScreen::drawBle()
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
        tft.drawString("DONE - SITE CAN PLOT", 8, 78);
    ui::bottomHint("BACK:STOP BLE+EXIT");
}

// ---------------------------------------------------------------------------
void TwoPortScreen::onTick()
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
            tft.drawCentreString("DATA INSUFFICIENT (<2 PTS)", tft.width() / 2, 108, 1);
        } else if (st == SweepState::Cancelled || st == SweepState::Error) {
            m_phase = Phase::Config;
            drawConfig();
        }
        return;
    }
    if (m_phase == Phase::Ble) {
        radio.poll();
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
                tft.drawString("DONE - SITE CAN PLOT", 8, 78);
            }
        }
    }
}

// ---------------------------------------------------------------------------
void TwoPortScreen::onEvent(InputEvent e)
{
    if (m_phase == Phase::Run) {
        if (e == InputEvent::Back) sweep.cancel();
        return;
    }
    if (m_phase == Phase::Ready) {
        switch (e) {
        case InputEvent::Ok: {
            const TwoPortDataset* d = sweep.sealedTwoPortDataset();
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
            radio.stopBle();
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
