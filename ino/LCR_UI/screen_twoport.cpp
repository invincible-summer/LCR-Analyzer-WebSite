// ============================================================================
// screen_twoport.cpp —— 模式 3：双端口扫频（H=Vout/Vin）-> seal -> BLE
// ----------------------------------------------------------------------------
// canonical 数据是复数、无量纲的 H。TFT/网站显示均从复 H 推导：
//   gain[dB] = 20*log10(|H|), phase[deg] = atan2(Im H, Re H)。
// W 链为 raw/no-calib，CSV 标注 raw_w_path。主 loop 是 BLE poll 唯一 owner。
// ============================================================================

#include "screens.h"
#include "radio_manager.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>

TwoPortScreen screenTwoPort;

namespace {
constexpr int kCfgX = 5;
constexpr int kCfgY0 = 31;
constexpr int kCfgDY = 36;
constexpr double kMinHMag = 1e-12;   // preview 仅用于 log10 数值保护（-240 dB）
}

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
    ui::topBar("TWO-PORT H", radio.state() != RadioState::Off);

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
    ui::topBar("H SWEEP", false);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString("RADIO OFF", 7, 24);
    tft.drawString("f:", 7, 48);
    tft.drawString("pts:", 7, 66);
    tft.drawString("err:", 7, 84);
    ui::progressBar(7, 108, tft.width() - 14, 12, 0.0, ui::C_ACCENT);
    ui::bottomHint("BACK:STOP AFTER BLOCK");
}

void TwoPortScreen::updateRun(bool stopping)
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

void TwoPortScreen::drawPreview()
{
    const TwoPortDataset* d = sweep.sealedTwoPortDataset();
    if (!d || d->nPoints < 2) return;

    static double f[SWEEP_MAX_POINTS];
    static double gainDb[SWEEP_MAX_POINTS];
    static double phaseDeg[SWEEP_MAX_POINTS];
    double gLo = 1e300, gHi = -1e300;
    double pLo = 1e300, pHi = -1e300;

    const uint16_t n = d->nPoints < SWEEP_MAX_POINTS ? d->nPoints : SWEEP_MAX_POINTS;
    double prevPhase = 0.0;
    for (uint16_t i = 0; i < n; ++i) {
        f[i] = d->points[i].f;
        double mag = hypot(d->points[i].reH, d->points[i].imH);
        if (!isfinite(mag) || mag < kMinHMag) mag = kMinHMag;
        gainDb[i] = 20.0 * log10(mag);

        double ph = atan2(d->points[i].imH, d->points[i].reH) * 180.0 / M_PI;
        if (i > 0) {
            // Bode 连线采用最邻近 unwrap，避免 +180/-180 处贯穿全图的假跳线。
            while (ph - prevPhase > 180.0) ph -= 360.0;
            while (ph - prevPhase < -180.0) ph += 360.0;
        }
        phaseDeg[i] = ph;
        prevPhase = ph;

        if (gainDb[i] < gLo) gLo = gainDb[i];
        if (gainDb[i] > gHi) gHi = gainDb[i];
        if (ph < pLo) pLo = ph;
        if (ph > pHi) pHi = ph;
    }
    if (!(gHi > gLo)) { gLo -= 1.0; gHi += 1.0; }
    else { const double pad = 0.08 * (gHi - gLo); gLo -= pad; gHi += pad; }
    if (!(pHi > pLo)) { pLo -= 10.0; pHi += 10.0; }
    else { const double pad = 0.08 * (pHi - pLo); pLo -= pad; pHi += pad; }

    // Portrait 128x160：左轴 18px，右轴保留约 25px 给 phase 数字。
    m_plot.setup(18, 72, tft.width() - 43, 50);
    m_plot.setXRange(f[0], f[n - 1]);
    m_plot.setMagRange(false, gLo, gHi);
    m_plot.setPhRange(pLo, pHi);
    m_plot.setLegend("dB", "deg");
    m_plot.drawFrame();
    m_plot.drawCurveMag(f, gainDb, n, ui::C_CH1);
    m_plot.drawCurvePh(f, phaseDeg, n, ui::C_CH2);
}

void TwoPortScreen::drawReady()
{
    const TwoPortDataset* d = sweep.sealedTwoPortDataset();
    char buf[32];
    tft.fillScreen(ui::C_BG);
    ui::topBar("H SWEEP SEALED", false);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    if (d) {
        snprintf(buf, sizeof(buf), "points %d err %d", d->nPoints, d->diag.failedPoints);
        tft.drawString(buf, 5, 24);
        snprintf(buf, sizeof(buf), "%luB CRC %08lX", (unsigned long)d->csvLen,
                 (unsigned long)d->crc32);
        tft.drawString(buf, 5, 38);
        tft.drawString("GAIN dB / PHASE deg", 5, 52);
        tft.drawString("H raw ratio", 5, 62);
    } else {
        tft.drawString("NO DATASET", 5, 24);
    }
    drawPreview();
    ui::bottomHint("OK:BLE BACK:CONFIG");
}

void TwoPortScreen::drawBle()
{
    char buf[28];
    tft.fillScreen(ui::C_BG);
    ui::topBar("BLE UPLOAD H", true);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString(radioStateText(radio.state()), 7, 30);
    snprintf(buf, sizeof(buf), "%lu/%lu B", (unsigned long)radio.bytesSent(),
             (unsigned long)radio.bytesTotal());
    tft.drawString(buf, 7, 50);
    ui::progressBar(7, 76, tft.width() - 14, 12,
                    radio.bytesTotal() ? (double)radio.bytesSent() / radio.bytesTotal() : 0.0,
                    ui::C_OK);
    if (radio.transferComplete()) tft.drawString("DONE - SITE CAN PLOT", 7, 102);
    ui::bottomHint("BACK:STOP BLE");
}

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
            tft.drawCentreString("DATA INSUFFICIENT <2", tft.width() / 2, 132, 1);
        } else if (st == SweepState::Cancelled || st == SweepState::Error) {
            m_phase = Phase::Config;
            drawConfig();
        }
        return;
    }
    if (m_phase == Phase::Ble) {
        // radio.poll() intentionally only runs once in LCR_UI.loop().
        static uint32_t lastDraw = 0;
        if (millis() - lastDraw > 200) {
            lastDraw = millis();
            char buf[28];
            tft.setTextFont(1);
            tft.fillRect(7, 30, tft.width() - 14, 58, ui::C_BG);
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
                tft.drawString("DONE - SITE CAN PLOT", 7, 102);
            }
        }
    }
}

void TwoPortScreen::onEvent(InputEvent e)
{
    if (m_phase == Phase::Run) {
        if (e == InputEvent::Back) sweep.cancel();
        return;
    }
    if (m_phase == Phase::Ready) {
        if (e == InputEvent::Ok) {
            const TwoPortDataset* d = sweep.sealedTwoPortDataset();
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
