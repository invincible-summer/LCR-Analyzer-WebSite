// ============================================================================
// screen_menu.cpp —— 主菜单（三个产品模式 + 隐藏诊断入口）
// ----------------------------------------------------------------------------
//   1 Component R/C/L     单元件自动识别与测量
//   2 One-Port Z Sweep    单端口扫频 -> BLE -> 网站拟合
//   3 Two-Port H Sweep    双端口扫频 -> BLE -> 网站曲线
// 隐藏页：3 秒内连按 3 次 Up 进入 Diagnostics（信号发生器等调试工具）。
// ============================================================================

#include "screens.h"
#include "radio_manager.h"

#include <Arduino.h>

ScreenManager screens;
MainMenuScreen screenMenu;

namespace {
const char* const kItems[] = {
    "1  Component R/C/L",
    "2  One-Port Z Sweep",
    "3  Two-Port H Sweep",
};
constexpr int kNItems = 3;
constexpr int kItemY0 = 30;     // 第一项 y 坐标（160x128 横屏）
constexpr int kItemDY = 24;     // 行距
}  // namespace

// ---------------------------------------------------------------------------
void MainMenuScreen::drawItem(int i, bool selected)
{
    const int W = tft.width();
    const int y = kItemY0 + i * kItemDY;

    tft.fillRect(8, y, W - 16, 20, selected ? ui::C_PANEL : ui::C_BG);
    tft.fillRect(8, y, 3, 20, selected ? ui::C_ACCENT : ui::C_GRID);

    tft.setTextFont(2);
    tft.setTextColor(selected ? ui::C_FG : ui::C_DIM,
                     selected ? ui::C_PANEL : ui::C_BG);
    tft.drawString(kItems[i], 16, y + 2);
}

// ---------------------------------------------------------------------------
void MainMenuScreen::onEnter()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("LCR  METER", radio.state() != RadioState::Off);

    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString("10Hz-10kHz  BLE-after-sweep", 8, 20);

    for (int i = 0; i < kNItems; ++i) drawItem(i, i == m_sel);
    ui::bottomHint("ENC/UD:SELECT  OK:ENTER");
}

void MainMenuScreen::onEvent(InputEvent e)
{
    int next = m_sel;
    switch (e) {
    case InputEvent::EncInc:
    case InputEvent::Down: next = (m_sel + 1) % kNItems; break;
    case InputEvent::EncDec:
    case InputEvent::Up: {
        next = (m_sel + kNItems - 1) % kNItems;
        // 隐藏诊断页解锁：3 秒窗口内连按 3 次 Up
        const uint32_t now = millis();
        if (now - m_diagFirstMs > 3000) { m_diagArmed = 0; m_diagFirstMs = now; }
        if (++m_diagArmed >= 3) {
            m_diagArmed = 0;
            screens.push(&screenSigGen);
            return;
        }
        break;
    }
    case InputEvent::Ok:
        switch (m_sel) {
        case 0: screens.push(&screenComponent); break;
        case 1: screens.push(&screenOnePort);   break;
        case 2: screens.push(&screenTwoPort);   break;
        }
        return;
    default: return;
    }
    if (next != m_sel) {
        drawItem(m_sel, false);
        m_sel = next;
        drawItem(m_sel, true);
    }
}

void MainMenuScreen::onTick()
{
    // 射频状态变化时刷新顶栏（其余区域无需重绘）
    static RadioState last = RadioState::Off;
    if (radio.state() != last) {
        last = radio.state();
        ui::topBar("LCR  METER", last != RadioState::Off);
    }
}
