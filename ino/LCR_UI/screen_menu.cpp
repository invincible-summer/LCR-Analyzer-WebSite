// ============================================================================
// screen_menu.cpp —— 主菜单（四个可见入口）
// ----------------------------------------------------------------------------
//   1 Component R/C/L     单元件自动识别与测量
//   2 One-Port Z Sweep    单端口扫频 -> BLE -> 网站拟合
//   3 Two-Port H Sweep    双端口扫频 -> BLE -> 网站曲线
//   4 Signal Generator    诊断信号发生器（正常菜单入口，不再使用隐藏手势）
// ============================================================================

#include "screens.h"
#include "radio_manager.h"

#include <Arduino.h>

ScreenManager screens;
MainMenuScreen screenMenu;

namespace {
const char* const kItems[] = {
    "1 Component R/C/L",
    "2 One-Port Z Sweep",
    "3 Two-Port H Sweep",
    "4 Signal Generator",
};
constexpr int kNItems = 4;
constexpr int kItemY0 = 34;
constexpr int kItemDY = 25;
}  // namespace

// ---------------------------------------------------------------------------
void MainMenuScreen::drawItem(int i, bool selected)
{
    const int W = tft.width();
    const int y = kItemY0 + i * kItemDY;

    tft.fillRect(5, y, W - 10, 20, selected ? ui::C_PANEL : ui::C_BG);
    tft.fillRect(5, y, 3, 20, selected ? ui::C_ACCENT : ui::C_GRID);

    tft.setTextFont(1);  // 128px 宽度下使用 font1，避免长菜单项越界
    tft.setTextColor(selected ? ui::C_FG : ui::C_DIM,
                     selected ? ui::C_PANEL : ui::C_BG);
    tft.drawString(kItems[i], 12, y + 6);
}

// ---------------------------------------------------------------------------
void MainMenuScreen::onEnter()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("LCR METER", radio.state() != RadioState::Off);

    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString("10Hz-10kHz", 6, 21);

    for (int i = 0; i < kNItems; ++i) drawItem(i, i == m_sel);
    ui::bottomHint("ENC:SEL OK:ENTER");
}

void MainMenuScreen::onEvent(InputEvent e)
{
    int next = m_sel;
    switch (e) {
    case InputEvent::EncInc:
    case InputEvent::Down:
        next = (m_sel + 1) % kNItems;
        break;
    case InputEvent::EncDec:
    case InputEvent::Up:
        next = (m_sel + kNItems - 1) % kNItems;
        break;
    case InputEvent::Ok:
        switch (m_sel) {
        case 0: screens.push(&screenComponent); break;
        case 1: screens.push(&screenOnePort);   break;
        case 2: screens.push(&screenTwoPort);   break;
        case 3: screens.push(&screenSigGen);    break;
        }
        return;
    default:
        return;
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
        ui::topBar("LCR METER", last != RadioState::Off);
    }
}
