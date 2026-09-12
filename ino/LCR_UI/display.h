// ============================================================================
// display.h —— TFT 显示基础层：初始化 / 主题配色 / 通用绘制 / 输入控件
// ----------------------------------------------------------------------------
// * 屏幕业务代码通过本文件提供的原语画界面；
// * 控件自带事件处理与绘制，界面只负责焦点分配；
// * 128x160 ST7735S 使用内置英文字体，界面文案保持 ASCII/英文。
// ============================================================================

#pragma once

#include <TFT_eSPI.h>

#include "input.h"

extern TFT_eSPI tft;

#define UI_RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

namespace ui {
inline constexpr uint16_t C_BG     = UI_RGB565(12, 16, 24);
inline constexpr uint16_t C_PANEL  = UI_RGB565(28, 36, 50);
inline constexpr uint16_t C_FG     = UI_RGB565(232, 236, 240);
inline constexpr uint16_t C_DIM    = UI_RGB565(120, 130, 145);
inline constexpr uint16_t C_ACCENT = UI_RGB565(0, 170, 255);
inline constexpr uint16_t C_OK     = UI_RGB565(0, 200, 110);
inline constexpr uint16_t C_ERR    = UI_RGB565(255, 80, 80);
inline constexpr uint16_t C_GRID   = UI_RGB565(46, 56, 72);
inline constexpr uint16_t C_AXIS   = UI_RGB565(140, 150, 165);
inline constexpr uint16_t C_CH1    = UI_RGB565(86, 148, 214);
inline constexpr uint16_t C_CH2    = UI_RGB565(255, 150, 40);

void begin();
void topBar(const char* title, bool btOn);
void bottomHint(const char* hint);
void progressBar(int x, int y, int w, int h, double frac, uint16_t color);
void row(int x, int y, int w, const char* label, const char* value, uint16_t color);
const char* fmtEng(double v, const char* unit, char* buf, int len, int prec = 3);
const char* fmtFreq(double hz, char* buf, int len);
const char* fmtDeg(double deg, char* buf, int len);
}  // namespace ui

// ============================================================================
// 数字位编辑器
// Up/Down 移动光标；编码器对当前十进制位做 +/-1 个位权的整数加减。
// 因而 9->0 会向高位进位，0->9 会向高位借位；最终再钳位到范围。
// ============================================================================
class DigitEditor {
public:
    void setup(int32_t vmin, int32_t vmax, int ndigits, int32_t v);
    bool onEvent(InputEvent e);
    void setCursor(int pos) { m_pos = (int8_t)pos; }
    int32_t value() const { return m_value; }
    void draw(int x, int y, int fontH, bool focused) const;
    int width(int fontH) const;

private:
    void syncFromValue();
    int32_t clampValue(int32_t v) const {
        return v < m_vmin ? m_vmin : (v > m_vmax ? m_vmax : v);
    }

    int32_t m_value = 0, m_vmin = 0, m_vmax = 1;
    int8_t  m_ndigits = 1, m_pos = 0;
    uint8_t m_digits[7] = {0};
};

class Checkbox {
public:
    void setup(const char* labelOff, const char* labelOn, bool v);
    bool onEvent(InputEvent e);
    bool value() const { return m_value; }
    void toggle() { m_value = !m_value; }
    void draw(int x, int y, bool focused) const;

private:
    const char* m_labelOff = "";
    const char* m_labelOn  = "";
    bool m_value = false;
};
