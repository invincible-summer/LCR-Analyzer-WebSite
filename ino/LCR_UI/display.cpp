// ============================================================================
// display.cpp —— 显示基础层实现
// ============================================================================

#include "display.h"

#include "hw_config.h"

#include <Arduino.h>
#include <stdio.h>

TFT_eSPI tft = TFT_eSPI();

// 本文件大量使用主题色常量，引入 ui 命名空间简化书写
using namespace ui;

// ---------------------------------------------------------------------------
void ui::begin()
{
    tft.init();
    tft.setRotation(TFT_ROTATION);
    if (TFT_PIN_BL >= 0) {          // 可程控背光（默认 -1 常亮）
        pinMode(TFT_PIN_BL, OUTPUT);
        digitalWrite(TFT_PIN_BL, HIGH);
    }
    tft.fillScreen(C_BG);
}

// ---------------------------------------------------------------------------
void ui::topBar(const char* title, bool btOn)
{
    const int W = tft.width();
    const int H = 22;
    tft.fillRect(0, 0, W, H, C_PANEL);
    tft.setTextFont(2);
    tft.setTextColor(C_FG, C_PANEL);
    tft.drawString(title, 6, 3);

    // 右侧蓝牙状态标记：连接时实心绿色 BT，未连接暗色
    tft.setTextColor(btOn ? C_OK : C_DIM, C_PANEL);
    tft.drawRightString(btOn ? "BT*" : "BT", W - 6, 3, 2);   // (str,x,y,font)
    tft.drawFastHLine(0, H, W, C_AXIS);   // 顶栏底部分隔线
}

// ---------------------------------------------------------------------------
void ui::bottomHint(const char* hint)
{
    const int W = tft.width();
    const int y = tft.height() - 18;
    tft.fillRect(0, y, W, 18, C_BG);
    tft.setTextFont(1);
    tft.setTextColor(C_DIM, C_BG);
    tft.drawCentreString(hint, W / 2, y + 5, 1);
}

// ---------------------------------------------------------------------------
void ui::progressBar(int x, int y, int w, int h, double frac, uint16_t color)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    tft.drawRect(x, y, w, h, C_AXIS);
    tft.fillRect(x + 2, y + 2, (int)((w - 4) * frac), h - 4, color);
}

// ---------------------------------------------------------------------------
void ui::row(int x, int y, int w, const char* label, const char* value, uint16_t color)
{
    tft.setTextFont(2);
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString(label, x, y + 5);           // 标签与数值垂直居中对齐
    tft.setTextFont(4);
    tft.setTextColor(color, C_BG);
    tft.drawRightString(value, x + w, y, 4);
}

// ---------------------------------------------------------------------------
const char* ui::fmtEng(double v, const char* unit, char* buf, int len, int prec)
{
    static const char* pre[] = {"G", "M", "k", "", "m", "u", "n"};
    double s = v < 0 ? -v : v;                // 跟踪缩放后的幅值
    int scale = 3;                             // 前缀表下标：3 对应 ''（10^0）
    while (s >= 1000.0 && scale > 0) { s /= 1000.0; v /= 1000.0; --scale; }
    while (s > 0.0 && s < 1.0 && scale < 6) { s *= 1000.0; v *= 1000.0; ++scale; }
    snprintf(buf, len, "%.*g%s%s", prec, v, pre[scale], unit);
    return buf;
}

const char* ui::fmtFreq(double hz, char* buf, int len)
{
    if (hz >= 1e6)      snprintf(buf, len, "%.3fMHz", hz * 1e-6);
    else if (hz >= 1e3) snprintf(buf, len, "%.3fkHz", hz * 1e-3);
    else                snprintf(buf, len, "%.1fHz", hz);
    return buf;
}

const char* ui::fmtDeg(double deg, char* buf, int len)
{
    snprintf(buf, len, "%+.1fdeg", deg);   // 内置字体无 ° 符号，用 deg
    return buf;
}

// ============================================================================
// DigitEditor
// ---------------------------------------------------------------------------
void DigitEditor::setup(int32_t vmin, int32_t vmax, int ndigits, int32_t v)
{
    m_vmin = vmin;
    m_vmax = vmax;
    m_ndigits = ndigits > 7 ? 7 : ndigits;
    m_value = clampValue(v);
    m_pos = m_ndigits - 1;                    // 光标默认在个位
    syncFromValue();
}

void DigitEditor::syncFromValue()
{
    int32_t v = m_value;
    for (int i = m_ndigits - 1; i >= 0; --i) {
        m_digits[i] = (uint8_t)(v % 10);
        v /= 10;
    }
}

bool DigitEditor::onEvent(InputEvent e)
{
    switch (e) {
    case InputEvent::Left:
        if (m_pos > 0) { --m_pos; return true; }
        return false;                          // 已在最左位：交还界面层
    case InputEvent::Right:
        if (m_pos < m_ndigits - 1) { ++m_pos; return true; }
        return false;                          // 已在最右位：交还界面层
    case InputEvent::EncInc:
    case InputEvent::EncDec: {
        // 当前位 0-9 循环：9 加 1 回 0（不进位），0 减 1 回 9（不借位）
        const bool up = (e == InputEvent::EncInc);
        const int8_t d = (int8_t)m_digits[m_pos];
        // 权 = 10^(ndigits-1-pos)
        int32_t p = 1;
        for (int i = 0; i < m_ndigits - 1 - m_pos; ++i) p *= 10;
        const int32_t step =
            up ? (d == 9 ? -9 : 1) : (d == 0 ? 9 : -1);
        m_value = clampValue(m_value + step * p);
        syncFromValue();
        return true;
    }
    default:
        return false;
    }
}

int DigitEditor::width(int fontH) const
{
    const int dw = (fontH >= 40) ? tft.textWidth("8", 7) : tft.textWidth("8", 4);
    const int gap = 4;
    return m_ndigits * dw + (m_ndigits - 1) * gap;
}

void DigitEditor::draw(int x, int y, int fontH, bool focused) const
{
    const int font = (fontH >= 40) ? 7 : 4;
    const int dw = (fontH >= 40) ? tft.textWidth("8", 7) : tft.textWidth("8", 4);
    const int gap = 4;
    const int digitH = (fontH >= 40) ? 48 : 26;

    for (int i = 0; i < m_ndigits; ++i) {
        const int dx = x + i * (dw + gap);
        // 前导零画成暗色（更接近仪器风格），从首个非零位起亮色
        bool leading = true;
        for (int j = 0; j < i; ++j) if (m_digits[j] != 0) { leading = false; break; }
        if (m_digits[i] != 0 || i == m_ndigits - 1) leading = false;
        const uint16_t col = focused ? ui::C_FG : (leading ? ui::C_GRID : ui::C_DIM);

        tft.setTextFont(font);
        tft.setTextColor(col, ui::C_BG);
        char s[2] = {(char)('0' + m_digits[i]), 0};
        tft.drawString(s, dx, y);

        if (focused && i == m_pos) {          // 当前编辑位：下方高亮光标条
            tft.fillRect(dx - 2, y + digitH + 4, dw + 4, 5, ui::C_ACCENT);
        } else if (focused) {
            tft.fillRect(dx - 2, y + digitH + 4, dw + 4, 5, ui::C_GRID);
        }
    }
}

// ============================================================================
// Checkbox
// ---------------------------------------------------------------------------
void Checkbox::setup(const char* labelOff, const char* labelOn, bool v)
{
    m_labelOff = labelOff;
    m_labelOn = labelOn;
    m_value = v;
}

bool Checkbox::onEvent(InputEvent e)
{
    if (e == InputEvent::EncInc || e == InputEvent::EncDec) {
        toggle();
        return true;
    }
    return false;
}

void Checkbox::draw(int x, int y, bool focused) const
{
    const int box = 16;
    // 勾选框
    if (focused) tft.drawRect(x - 2, y - 2, box + 4, box + 4, C_ACCENT);
    tft.drawRect(x, y, box, box, C_FG);
    if (m_value) tft.fillRect(x + 3, y + 3, box - 6, box - 6, C_OK);
    // 文本
    tft.setTextFont(2);
    tft.setTextColor(focused ? C_FG : C_DIM, C_BG);
    tft.drawString(m_value ? m_labelOn : m_labelOff, x + box + 8, y);
}
