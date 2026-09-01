// ============================================================================
// screen_siggen.cpp —— 信号发生器界面（实验室信号源风格）
// ----------------------------------------------------------------------------
// 交互设计（对应任务书）：
//   * 左/右键移动「数位光标」或切换字段（频率 → 波形 → 占空比）；
//   * 编码器旋转：调整当前数位的数字（0-9 循环）/ 切换波形 / 调占空比数位；
//   * OK：开始输出；再按暂停（内部以 freq=0 调用队友接口）；再按恢复输出；
//   * BACK：若在输出则先停止，再返回主菜单。
//   * 输出中允许修改参数，下一次「恢复输出」时生效（状态行提示 PENDING）。
// 屏幕布局（320x240 横屏）：
//   顶栏 / 大号频率(font7 七段码) + 波形预览 / WAVE 行 / DUTY 行 / 状态行 / 提示行
// ============================================================================

#include "screens.h"
#include "bt_link.h"
#include "partner_api.h"

#include <Arduino.h>
#include <stdio.h>

SigGenScreen screenSigGen;

namespace {
constexpr int kFreqX  = 24;    // 频率编辑器左上角
constexpr int kFreqY  = 44;
constexpr int kPrevX  = 224;   // 波形预览框
constexpr int kPrevY  = 38;
constexpr int kPrevW  = 84;
constexpr int kPrevH  = 54;
constexpr int kWaveY  = 108;   // WAVE 行
constexpr int kDutyY  = 150;   // DUTY 行
constexpr int kLabelX = 24;    // 行标签 x
constexpr int kValueX = 100;   // 行数值 x
constexpr int kStatY  = 192;   // 状态行

const char* const kWaveNames[3] = {"SINE", "SQUARE", "TRIANGLE"};
}  // namespace

// ---------------------------------------------------------------------------
void SigGenScreen::onEnter()
{
    // 首次进入初始化控件（static 实例会保留上次状态，故只 setup 一次）
    static bool inited = false;
    if (!inited) {
        m_freq.setup(FREQ_MIN_HZ, FREQ_MAX_HZ, 5, 1000);
        m_duty.setup(0, 100, 3, 50);
        inited = true;
    }
    m_field = F_FREQ;
    drawStatic();
}

// ---------------------------------------------------------------------------
void SigGenScreen::drawStatic()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("SIGNAL GENERATOR", bt.connected());

    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString("FREQ (Hz)", kFreqX, kFreqY - 12);

    drawFreq();
    drawPreview();
    drawWave();
    drawDuty();
    drawStatus();
    ui::bottomHint("ENC:EDIT <>:FIELD OK:RUN/STOP BACK:EXIT");
}

// ---------------------------------------------------------------------------
void SigGenScreen::drawFreq()
{
    const bool focused = (m_field == F_FREQ);
    tft.setTextFont(1);
    tft.setTextColor(focused ? ui::C_ACCENT : ui::C_DIM, ui::C_BG);
    tft.drawString("FREQ (Hz)", kFreqX, kFreqY - 12);

    // 先清旧（含光标条），再画（清屏宽度不超过波形预览框左缘）
    tft.fillRect(kFreqX - 4, kFreqY, 190, 60, ui::C_BG);
    m_freq.draw(kFreqX, kFreqY, 48, focused);
    tft.setTextFont(2);
    tft.setTextColor(m_running ? ui::C_OK : ui::C_DIM, ui::C_BG);
    tft.drawString(m_running ? "RUN" : "STOP", kFreqX + 160, kFreqY + 16);
}

void SigGenScreen::drawWave()
{
    const bool focused = (m_field == F_WAVE);
    tft.setTextFont(1);
    tft.setTextColor(focused ? ui::C_ACCENT : ui::C_DIM, ui::C_BG);
    tft.drawString("WAVE", kLabelX, kWaveY + 8);

    tft.fillRect(kValueX - 4, kWaveY, 180, 30, ui::C_BG);
    tft.setTextFont(4);
    tft.setTextColor(ui::C_FG, ui::C_BG);
    tft.drawString(kWaveNames[m_wave], kValueX, kWaveY);
    if (focused)
        tft.fillRect(kValueX - 2, kWaveY + 28, 130, 4, ui::C_ACCENT);
    drawPreview();                          // 波形变化时预览同步
}

void SigGenScreen::drawDuty()
{
    const bool dim = (m_wave == Sine);      // 正弦无占空比概念
    const bool focused = (m_field == F_DUTY) && !dim;
    tft.setTextFont(1);
    tft.setTextColor(focused ? ui::C_ACCENT : ui::C_DIM, ui::C_BG);
    tft.drawString("DUTY %", kLabelX, kDutyY + 8);

    tft.fillRect(kValueX - 4, kDutyY, 120, 32, ui::C_BG);
    if (!dim) {
        m_duty.draw(kValueX, kDutyY, 26, focused);
    } else {
        tft.setTextFont(2);
        tft.setTextColor(ui::C_GRID, ui::C_BG);
        tft.drawString("-- (sine)", kValueX + 4, kDutyY + 6);
    }
}

// ---------------------------------------------------------------------------
// 波形预览：一个周期，占空比对三角/方波生效
void SigGenScreen::drawPreview()
{
    tft.drawRect(kPrevX - 1, kPrevY - 1, kPrevW + 2, kPrevH + 2, ui::C_AXIS);
    tft.fillRect(kPrevX, kPrevY, kPrevW, kPrevH, ui::C_BG);

    const double duty = m_duty.value() / 100.0;
    const int N = 60;
    int px0 = 0, py0 = 0;
    for (int i = 0; i < N; ++i) {
        const double t = (double)i / (N - 1);
        double v;
        switch (m_wave) {
        case Square:   v = (t < (duty > 0 ? duty : 0.5)) ? 0.9 : -0.9; break;
        case Triangle:
            v = (t < duty) ? (2.0 * t / (duty > 0.01 ? duty : 0.01) - 1.0)
                           : (1.0 - 2.0 * (t - duty) / (1.0 - duty + 0.01));
            v *= 0.9;
            break;
        default:       v = 0.9 * sin(2.0 * M_PI * t); break;   // Sine
        }
        const int px = kPrevX + (int)(t * (kPrevW - 1));
        const int py = kPrevY + kPrevH / 2 - (int)(v * (kPrevH / 2 - 4));
        if (i > 0)
            tft.drawLine(px0, py0, px, py, ui::C_CH1);
        px0 = px; py0 = py;
    }
}

// ---------------------------------------------------------------------------
void SigGenScreen::drawStatus()
{
    char buf[40];
    tft.fillRect(kLabelX, kStatY, 280, 24, ui::C_BG);
    tft.setTextFont(2);

    if (millis() < m_errUntilMs) {                       // 启动失败提示（2 秒）
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        tft.drawString("START FAILED!", kLabelX, kStatY);
        return;
    }
    if (m_running) {                                     // 输出中：显示实际频率
        ui::fmtFreq(m_actualHz, buf, sizeof(buf));
        char line[48];
        snprintf(line, sizeof(line), "OUTPUT  %s", buf);
        tft.setTextColor(ui::C_OK, ui::C_BG);
        tft.drawString(line, kLabelX, kStatY);
    } else {
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        tft.drawString("STOPPED", kLabelX, kStatY);
    }
}

// ---------------------------------------------------------------------------
// 调用队友接口产生输出；失败（返回 0）则给出限时错误提示
void SigGenScreen::startOutput()
{
    const double f = generateWave(m_freq.value(), (WaveType)m_wave,
                                  m_duty.value() / 100.0);
    if (f <= 0) {
        m_errUntilMs = millis() + 2000;
    } else {
        m_running = true;
        m_actualHz = f;
    }
    drawStatus();
    drawFreq();          // RUN/STOP 标记同步
}

// 暂停输出：任务书约定传 freq=0
void SigGenScreen::stopOutput()
{
    generateWave(0, (WaveType)m_wave, 0);
    m_running = false;
    drawStatus();
    drawFreq();
}

// ---------------------------------------------------------------------------
void SigGenScreen::onEvent(InputEvent e)
{
    switch (e) {
    case InputEvent::Ok:
        m_running ? stopOutput() : startOutput();
        return;
    case InputEvent::Back:
        if (m_running) stopOutput();
        screens.pop();
        return;
    default:
        break;
    }

    // ---- 字段编辑 --------------------------------------------------------
    // 返回值 false 仅出现在 Left/Right 越过数位边界：此时把焦点转移到相邻字段；
    // 其余情况（光标移动 / 编码器改数）重画当前控件即可。
    const int dutySkip = (m_wave == Sine) ? 1 : 0;
    int next = m_field;

    if (m_field == F_FREQ || m_field == F_DUTY) {
        DigitEditor& ed = (m_field == F_FREQ) ? m_freq : m_duty;
        const bool lr = (e == InputEvent::Left || e == InputEvent::Right);
        if (!ed.onEvent(e)) {             // 数位光标到达边界 -> 转移焦点
            if (!lr) return;
            if (m_field == F_FREQ) {
                next = (e == InputEvent::Right) ? F_WAVE
                                                : (dutySkip ? F_WAVE : F_DUTY);
                m_freq.setCursor((e == InputEvent::Right) ? 0 : 4);  // 进入侧
            } else {
                next = (e == InputEvent::Right) ? F_FREQ : F_WAVE;
                m_duty.setCursor((e == InputEvent::Right) ? 0 : 2);
            }
        }
    } else if (m_field == F_WAVE) {
        if (e == InputEvent::EncInc || e == InputEvent::EncDec) {
            const int dir = (e == InputEvent::EncInc) ? 1 : -1;
            m_wave = (m_wave + dir + 3) % 3;
            if (m_wave == Sine && m_field == F_DUTY)
                m_field = F_FREQ;          // 切回正弦时占空比失效，焦点回频率
            drawWave();
            drawDuty();                   // 正弦时 DUTY 变灰
            return;
        }
        if (e != InputEvent::Left && e != InputEvent::Right) return;
        next = (e == InputEvent::Right) ? (dutySkip ? F_FREQ : F_DUTY) : F_FREQ;
        if (next == F_FREQ) m_freq.setCursor((e == InputEvent::Right) ? 0 : 4);
    }

    if (next != m_field) {                // 焦点转移：重画新旧字段
        m_field = next;
        drawFreq();
        drawWave();
        drawDuty();
    } else {                              // 数值/数位光标变化：重画当前控件
        if (m_field == F_FREQ) drawFreq();
        else if (m_field == F_DUTY) drawDuty();
    }
}
