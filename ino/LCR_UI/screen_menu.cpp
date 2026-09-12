// ============================================================================
// screen_menu.cpp —— 主菜单（五个可见入口）
// ============================================================================

#include "screens.h"
#include "radio_manager.h"

#include <Arduino.h>

ScreenManager screens;
MainMenuScreen screenMenu;

namespace {
const char* const kItems[] = {
    "1 Unknown Component",
    "2 Single Freq LCR",
    "3 One-Port Z Sweep",
    "4 Two-Port H Sweep",
    "5 Signal Generator",
};
constexpr int kNItems = 5;
constexpr int kItemY0 = 30;
constexpr int kItemDY = 23;
}  // namespace

void MainMenuScreen::drawItem(int i, bool selected)
{
    const int W = tft.width();
    const int y = kItemY0 + i * kItemDY;
    tft.fillRect(5, y, W - 10, 20, selected ? ui::C_PANEL : ui::C_BG);
    tft.fillRect(5, y, 3, 20, selected ? ui::C_ACCENT : ui::C_GRID);
    tft.setTextFont(1);
    tft.setTextColor(selected ? ui::C_FG : ui::C_DIM,
                     selected ? ui::C_PANEL : ui::C_BG);
    tft.drawString(kItems[i], 12, y + 6);
}

void MainMenuScreen::onEnter()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("LCR METER", radio.state() != RadioState::Off);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString("10Hz-10kHz", 6, 20);
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
        case 0: screens.push(&screenComponent);   break;
        case 1: screens.push(&screenSinglePoint); break;
        case 2: screens.push(&screenOnePort);     break;
        case 3: screens.push(&screenTwoPort);     break;
        case 4: screens.push(&screenSigGen);      break;
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
    static RadioState last = RadioState::Off;
    if (radio.state() != last) {
        last = radio.state();
        ui::topBar("LCR METER", last != RadioState::Off);
    }
}
