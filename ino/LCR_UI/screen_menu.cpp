// ============================================================================
// screen_menu.cpp —— 主菜单
// ----------------------------------------------------------------------------
// 三个功能入口（任务书）：
//   1. Signal Generator     信号发生器
//   2. Impedance @ Single F 单频点复阻抗测量
//   3. Frequency Response   幅频/相频特性测量
// 操作：编码器旋转或 左/右 键移动选择，OK 进入，BACK 无操作（根界面）。
// ============================================================================

#include "screens.h"
#include "bt_link.h"

#include <Arduino.h>

ScreenManager screens;
MainMenuScreen screenMenu;

namespace {
const char* const kItems[] = {
    "1  Signal Generator",
    "2  Impedance @ 1 Freq",
    "3  Freq Response Sweep",
};
constexpr int kNItems = 3;
constexpr int kItemY0 = 58;     // 第一项 y 坐标
constexpr int kItemDY = 40;     // 行距
}  // namespace

// ---------------------------------------------------------------------------
void MainMenuScreen::drawItem(int i, bool selected)
{
    const int W = tft.width();
    const int y = kItemY0 + i * kItemDY;

    // 选中项：面板底色 + 左侧高亮竖条；未选中：背景 + 暗色竖条
    tft.fillRect(16, y, W - 32, 32, selected ? ui::C_PANEL : ui::C_BG);
    tft.fillRect(16, y, 4, 32, selected ? ui::C_ACCENT : ui::C_GRID);

    tft.setTextFont(2);
    tft.setTextColor(selected ? ui::C_FG : ui::C_DIM,
                     selected ? ui::C_PANEL : ui::C_BG);
    tft.drawString(kItems[i], 30, y + 8);
}

// ---------------------------------------------------------------------------
void MainMenuScreen::onEnter()
{
    tft.fillScreen(ui::C_BG);
    ui::topBar("LCR  METER", bt.connected());

    // 标题下的简短状态行：频率范围提示（任务书约束 10 ~ 10000 Hz）
    tft.setTextFont(1);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawString("10 Hz - 10 kHz  |  4-btn + encoder", 8, 30);

    for (int i = 0; i < kNItems; ++i) drawItem(i, i == m_sel);
    ui::bottomHint("ENC/<>:SELECT  OK:ENTER");
}

void MainMenuScreen::onEvent(InputEvent e)
{
    int next = m_sel;
    switch (e) {
    case InputEvent::EncInc:
    case InputEvent::Right:  next = (m_sel + 1) % kNItems; break;
    case InputEvent::EncDec:
    case InputEvent::Left:   next = (m_sel + kNItems - 1) % kNItems; break;
    case InputEvent::Ok:
        switch (m_sel) {
        case 0: screens.push(&screenSigGen);   break;
        case 1: screens.push(&screenMeasure);  break;
        case 2: screens.push(&screenSweep);    break;
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
    // 蓝牙连接状态变化时刷新顶栏（其余区域无需重绘）
    if (bt.takeStatusChanged())
        ui::topBar("LCR  METER", bt.connected());
}
