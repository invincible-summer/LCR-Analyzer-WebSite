// ============================================================================
// LCR_UI.ino —— ESP32-S3 LCR 仪表主程序（入口 + 装配）· v4.1.1
// ----------------------------------------------------------------------------
// 硬件：ESP32-S3-WROOM-1-N16R8 + 已验证 LCR 模拟前端 + ST7735S +
//       4 按键 + EC11。测量引脚由 DO_NOT_TOUCH_* 固化；UI/诊断引脚见
//       board_profile.cpp。
//
// 用户入口：
//   1) Unknown Component：5 点未知元件识别；
//   2) Single Freq LCR：一个确定频率测量一次；
//   3) One-Port Z Sweep；4) Two-Port H Sweep；5) Signal Generator。
//
// 测量架构：UI -> lcr_api.h -> lcr_api.cpp 独立 Worker -> DNT API。
// 运行期主循环保持事件驱动；同步等待只存在于一次性的 boot 初始化阶段。
// ============================================================================

#include <Arduino.h>

#include "board_profile.h"
#include "display.h"
#include "fw_version.h"
#include "input.h"
#include "lcr_api.h"
#include "radio_manager.h"
#include "screens.h"
#include "sweep_engine.h"

SweepEngine sweep(lcrService());

void setup()
{
    Serial.begin(115200);

    // 与 DO_NOT_TOUCH_EXAMPLE.ino.example 相同的 GPIO4 诊断开关：
    // INPUT_PULLUP；HIGH(悬空/3.3V)=开诊断，LOW(GND)=关诊断。
    // 必须在 Worker 内的 lcr_api_init() 之前采样并锁存。
    pinMode(kBoard.diagEnable, INPUT_PULLUP);
    delay(20);  // 仅 boot，一次性等待上拉稳定；运行期状态机不使用阻塞 delay
    const bool diagnosticsEnabled = digitalRead(kBoard.diagEnable) == HIGH;

    Serial.printf("\nLCR-UI v%s booting (BLE protocol %d, z-schema v%d, "
                  "h-schema v%d, DNT diag %s)\n",
                  LCR_FW_VERSION, LCR_BLE_PROTOCOL_VERSION,
                  LCR_Z_CSV_SCHEMA_VERSION, LCR_H_CSV_SCHEMA_VERSION,
                  diagnosticsEnabled ? "ON" : "OFF");

    ui::begin();
    input.begin();
    tft.fillScreen(ui::C_BG);
    tft.setTextFont(2);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawCentreString("LCR CORE INIT...", tft.width() / 2, 56, 2);

    // init 与全部测量同 Worker task；diagnostics latch 在 init 前应用。
    if (!lcrServiceBegin(diagnosticsEnabled)) {
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        tft.drawCentreString("WORKER CREATE FAIL", tft.width() / 2, 56, 2);
        while (1) delay(1000);
    }
    const uint32_t t0 = millis();
    while (!lcrServiceReady() && !lcrServiceInitError() &&
           millis() - t0 < 15000)
        delay(10);
    if (!lcrServiceReady()) {
        tft.setTextColor(ui::C_ERR, ui::C_BG);
        tft.drawCentreString("LCR CORE INIT FAIL", tft.width() / 2, 56, 2);
        tft.setTextFont(1);
        tft.drawCentreString("check ADC/LCD_CAM wiring, reboot",
                             tft.width() / 2, 80, 1);
        while (1) delay(1000);
    }
    {
        LcrEvent ev;
        while (lcrServiceTakeEvent(ev)) {}
    }

    Serial.println("LCR-UI ready. BLE stays OFF until a sweep is sealed.");
    screens.begin(&screenMenu);
}

void loop()
{
    input.poll();
    for (InputEvent e = input.getEvent(); e != InputEvent::None;
         e = input.getEvent())
        screens.handle(e);

    screens.tick();
    radio.poll();
    delay(1);
}
