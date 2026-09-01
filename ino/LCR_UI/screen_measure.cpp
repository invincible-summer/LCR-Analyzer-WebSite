// ============================================================================
// screen_measure.cpp —— 单频点复阻抗 / 双端口测量界面
// ----------------------------------------------------------------------------
// 配置页：
//   * 频率数字位编辑（与信号发生器一致的交互）；
//   * 复选框选择测量对象（任务书要求：编码器旋转切换）：
//       [ ] 1-PORT  测量待测元件/单端口网络的复阻抗
//       [x] 2-PORT  测量双端口网络的输入输出关系（增益/相位）
//   * OK 开始测量。
// 结果页：
//   * 1-PORT 显示 |Z|、辐角、R、X、容性/感性与等效 C/L、等效电阻；
//   * 2-PORT 显示增益 dB、相位差、原始幅度比；
//   * 末行上方保留一行空行（任务书要求「记得预留一行」，供后续扩展）。
//   * OK 重测（同参数），BACK 返回配置页。
// ============================================================================

#include "screens.h"
#include "bt_link.h"
#include "partner_api.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>

MeasureScreen screenMeasure;

namespace {
constexpr int kFreqX = 24, kFreqY = 44;      // 频率编辑器位置
constexpr int kModeX = 24, kModeY = 130;     // 复选框位置
constexpr int kRowY0 = 46;                   // 结果页第一行 y
constexpr int kRowDY = 28;                   // 结果页行高（含 font4）
constexpr int kLabelW = 92;                  // 行标签区宽度
}  // namespace

// ---------------------------------------------------------------------------
void MeasureScreen::onEnter()
{
    static bool inited = false;
    if (!inited) {
        m_freq.setup(FREQ_MIN_HZ, FREQ_MAX_HZ, 5, 1000);
        m_mode.setup("1-PORT   Z of DUT", "2-PORT   Gain/Phase", false);
        inited = true;
    }
    m_field = F_FREQ;
    m_phase = PhConfig;
    drawConfig();
}

// ---------------------------------------------------------------------------
void MeasureScreen::drawConfig()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("IMPEDANCE @ 1 FREQ", bt.connected());

    const bool freqFocus = (m_field == F_FREQ);
    tft.setTextFont(1);
    tft.setTextColor(freqFocus ? ui::C_ACCENT : ui::C_DIM, ui::C_BG);
    tft.drawString("FREQ (Hz)", kFreqX, kFreqY - 12);
    m_freq.draw(kFreqX, kFreqY, 48, freqFocus);

    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString("MEASURE TARGET", kModeX, kModeY - 12);
    m_mode.draw(kModeX, kModeY, m_field == F_MODE);

    ui::bottomHint("ENC:EDIT/TOGGLE  OK:MEASURE  BACK:EXIT");
}

// ---------------------------------------------------------------------------
// 阻塞执行一次测量：采样 -> 幅相计算 -> 复阻抗/增益计算 -> 显示
void MeasureScreen::runMeasurement()
{
    const bool onePort = !m_mode.value();          // 复选框选中 = 双端口

    // 测量提示（同步 TFT 调用，确保阻塞采样前已上屏）
    tft.fillScreen(ui::C_BG);
    ui::topBar(onePort ? "Z  1-PORT" : "H  2-PORT", bt.connected());
    tft.setTextFont(4);
    tft.setTextColor(ui::C_ACCENT, ui::C_BG);
    tft.drawCentreString("MEASURING...", tft.width() / 2, 100, 4);

    // ---- 队友接口：采样 ---------------------------------------------------
    m_smp = measureImpedanceAtFreq(m_freq.value(), onePort);
    m_rp = computeRatioPhase(m_smp);
    if (!m_rp.ok || m_smp.sampleLength < 8) {
        m_phase = PhResult;                        // 停留在结果页显示错误
        drawError("SAMPLE/FIT FAILED");
        return;
    }

    // ---- 幅度比 + 相位差 -> 复阻抗 / 增益相位 ------------------------------
    // 字段顺序即 partner_api.h 中 ImpedanceCalcInput 的声明顺序（聚合初始化）
    ImpedanceCalcInput in = {
        m_rp.amplitudeRatio,                       // amplitudeRatio（定义见契约）
        m_rp.phaseDiffDeg,                         // phaseDiff（度）
        m_smp.transimpedanceGain,
        m_smp.voltageGain,
        m_smp.currentGain,
        m_smp.actualFreq,
    };
    if (onePort)
        m_z = calculateImpedance(in);
    else
        m_gp = calculateGainPhase(in);

    m_phase = PhResult;
    drawResult();

    // ---- 蓝牙通路：单点也按 api_contract 包装成一次单点扫描上传 -----------
    if (BT_AUTO_UPLOAD && bt.connected()) {
        const double fList[1] = {(double)m_freq.value()};
        bt.sendScanStart(BT_DEVICE_NAME, fList, 1, onePort ? "1pt-Z" : "1pt-H");
        bt.sendPoint(m_smp, m_rp.amplitudeRatio, m_rp.phaseDiffDeg, onePort);
        bt.sendScanEnd(1);
    }
}

// ---------------------------------------------------------------------------
void MeasureScreen::drawHeader()
{
    char buf[48], act[20];
    ui::fmtFreq(m_smp.actualFreq, act, sizeof(act));
    snprintf(buf, sizeof(buf), "F: %dHz   ACT: %s", m_freq.value(), act);
    tft.setTextFont(2);
    tft.setTextColor(ui::C_FG, ui::C_BG);
    tft.drawString(buf, 10, 26);
}

void MeasureScreen::drawResult()
{
    const bool onePort = !m_mode.value();
    tft.fillScreen(ui::C_BG);
    ui::topBar(onePort ? "Z  1-PORT" : "H  2-PORT", bt.connected());
    drawHeader();

    char vbuf[24];
    int y = kRowY0;
    const int W = tft.width() - 10;

    if (onePort) {
        const double mag = hypot(m_z.realPart, m_z.imagPart);
        const double ang = atan2(m_z.imagPart, m_z.realPart) * 180.0 / M_PI;
        ui::row(10, y, W, "|Z|", ui::fmtEng(mag, "Ohm", vbuf, sizeof(vbuf)), ui::C_CH1);
        y += kRowDY;
        ui::row(10, y, W, "ANGLE", ui::fmtDeg(ang, vbuf, sizeof(vbuf)), ui::C_CH2);
        y += kRowDY;
        ui::row(10, y, W, "R (Re)", ui::fmtEng(m_z.realPart, "Ohm", vbuf, sizeof(vbuf)), ui::C_FG);
        y += kRowDY;
        ui::row(10, y, W, "X (Im)", ui::fmtEng(m_z.imagPart, "Ohm", vbuf, sizeof(vbuf)), ui::C_FG);
        y += kRowDY;
        // 容性 / 感性 + 等效元件值（队友函数已算好 reactanceValue）
        if (m_z.isCapacitive)
            ui::row(10, y, W, "CAP  C~", ui::fmtEng(m_z.reactanceValue, "F", vbuf, sizeof(vbuf)), ui::C_OK);
        else
            ui::row(10, y, W, "IND  L~", ui::fmtEng(m_z.reactanceValue, "H", vbuf, sizeof(vbuf)), ui::C_OK);
        y += kRowDY;
        // 等效电阻：电感串联 / 电容并联（队友函数定义）
        ui::row(10, y, W, "Req", ui::fmtEng(m_z.equivalentResistance, "Ohm", vbuf, sizeof(vbuf)), ui::C_FG);
    } else {
        snprintf(vbuf, sizeof(vbuf), "%+.2f dB", m_gp.gainDb);
        ui::row(10, y, W, "GAIN", vbuf, ui::C_CH1);
        y += kRowDY;
        ui::row(10, y, W, "PHASE", ui::fmtDeg(m_gp.phaseDiff, vbuf, sizeof(vbuf)), ui::C_CH2);
        y += kRowDY;
        ui::row(10, y, W, "RATIO", ui::fmtEng(m_rp.amplitudeRatio, "", vbuf, sizeof(vbuf)), ui::C_FG);
        y += kRowDY;
        // 拟合残差（原始码 RMS）：信号质量指示
        char q[32];
        snprintf(q, sizeof(q), "%.1f/%.1f", m_rp.residRmsOut, m_rp.residRmsIn);
        ui::row(10, y, W, "FIT RMS", q, ui::C_FG);
    }

    // !! 预留行（任务书「记得预留一行」）：当前留空，供后续扩展（如 D/Q、
    //    OSL 校准状态、串号等）。y = kRowY0 + 6*kRowDY 处不要放内容。
    ui::bottomHint("OK:RE-MEASURE  BACK:CONFIG");
}

// ---------------------------------------------------------------------------
void MeasureScreen::drawError(const char* msg)
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("IMPEDANCE @ 1 FREQ", bt.connected());
    tft.setTextFont(4);
    tft.setTextColor(ui::C_ERR, ui::C_BG);
    tft.drawCentreString(msg, tft.width() / 2, 100, 4);
    ui::bottomHint("OK:RETRY  BACK:CONFIG");
}

// ---------------------------------------------------------------------------
void MeasureScreen::onEvent(InputEvent e)
{
    // ---- 结果页 / 错误页 ---------------------------------------------------
    if (m_phase == PhResult) {
        switch (e) {
        case InputEvent::Ok:    runMeasurement();      return;
        case InputEvent::Back:  m_phase = PhConfig; drawConfig(); return;
        default: return;
        }
    }

    // ---- 配置页 ------------------------------------------------------------
    switch (e) {
    case InputEvent::Ok:
        runMeasurement();
        return;
    case InputEvent::Back:
        screens.pop();
        return;
    default:
        break;
    }

    const bool lr = (e == InputEvent::Left || e == InputEvent::Right);

    if (m_field == F_FREQ) {
        if (!m_freq.onEvent(e)) {              // 数位边界 -> 焦点到复选框
            if (!lr) return;
            m_field = F_MODE;
            m_freq.setCursor(e == InputEvent::Right ? 0 : 4);
        }
        drawConfig();                          // 配置页整体重绘（轻量）
    } else if (m_field == F_MODE) {
        if (m_mode.onEvent(e)) {               // 编码器切换 1/2 端口
            drawConfig();
            return;
        }
        if (lr) {                              // 左右 -> 焦点回频率
            m_field = F_FREQ;
            m_freq.setCursor(e == InputEvent::Right ? 0 : 4);
            drawConfig();
        }
    }
}
