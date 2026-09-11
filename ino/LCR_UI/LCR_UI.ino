// ============================================================================
// LCR_UI.ino —— ESP32-S3 LCR 仪表主程序（入口 + 装配）· v4.1.0
// ----------------------------------------------------------------------------
// 硬件：自制 ESP32-S3 开发板（N16R8）+ LCR 模拟前端（LCD_CAM 8bit 并行
//       正弦 -> 电阻网络 DAC；74HC595 -> TIA/V/I 增益/双端口控制；
//       两路 ADC）+ 外接 ST7735S 屏 / 4 按键 / EC11 编码器。
//       测量引脚全部在 DO_NOT_TOUCH_* 头文件中固化（不可修改）；
//       UI 引脚见 board_profile.cpp（真源 docs/HARDWARE_MAPPING.md）。
//
// 顶层三个用户模式（plan.md §0）：
//   1) 单元件 R/C/L 自动识别与测量（不启动 BLE）
//   2) 单端口扫频 -> 全部采样完成 -> seal -> BLE -> 网站拟合
//   3) 双端口扫频 -> 全部采样完成 -> seal -> BLE -> 网站画曲线
//   另有隐藏诊断页（信号发生器，经 wrapper 调 DNT set_freq）。
//
// 测量架构（唯一依赖边）：
//   UI / SweepEngine / Dataset / BLE
//       -> lcr_api.h（应用层唯一测量接口）
//       -> lcr_api.cpp（唯一 include DO_NOT_TOUCH_lcr_api.h 的编译单元，
//                      内含独立 FreeRTOS 测量 Worker）
//       -> DO_NOT_TOUCH_lcr_api.h（已验证硬件链）
//
// 本文件只做装配，不做任何测量：
//   不配 ADC、不配 LCD_CAM、不写 74HC595、不建 sine buffer、
//   不从 raw ADC 算 Z、不校准、不自动量程。
//
// 主循环模型：非阻塞事件驱动。每圈：扫描输入 -> 分发事件 -> 当前屏
// onTick（推进编排状态机并消费测量事件）-> BLE 事件泵 -> 短 yield。
//
// 射频互斥（硬 invariant）：测量窗口（Starting/Measuring/Stopping）内
// BLE 完全关闭；dataset seal 后才允许 startBleForSealedDataset()。
// 固件不初始化 Wi-Fi。
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

// ---------------------------------------------------------------------------
// 引擎装配：真实测量服务在 lcr_api.cpp 的 FreeRTOS Worker 内（host 单测
// 注入 MockLcrService，见 ino/test/）
// ---------------------------------------------------------------------------
SweepEngine sweep(lcrService());

// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    Serial.printf("\nLCR-UI v%s booting (BLE protocol %d, z-schema v%d, "
                  "h-schema v%d)\n",
                  LCR_FW_VERSION, LCR_BLE_PROTOCOL_VERSION,
                  LCR_Z_CSV_SCHEMA_VERSION, LCR_H_CSV_SCHEMA_VERSION);

    ui::begin();                       // ST7735S（BoardProfile 参数）
    input.begin();                     // 按键 + 编码器
    tft.fillScreen(ui::C_BG);
    tft.setTextFont(2);
    tft.setTextColor(ui::C_DIM, ui::C_BG);
    tft.drawCentreString("LCR CORE INIT...", tft.width() / 2, 56, 2);

    // ★ 测量核心初始化在 Worker task 内执行（ADC task-affinity 约束，
    //   见 lcr_api.h 文件头）；这一次性的 boot wait 不影响运行时非阻塞。
    if (!lcrServiceBegin()) {
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
        while (1) delay(1000);         // 初始化失败：停在错误页，不入菜单
    }
    // 清空 boot 事件（ServiceInit），避免占用事件队列
    {
        LcrEvent ev;
        while (lcrServiceTakeEvent(ev)) {}
    }

    // 注意：启动时不初始化 BLE（测量期间射频静默）——seal 后才允许
    Serial.println("LCR-UI ready. BLE stays OFF until a sweep is sealed.");
    screens.begin(&screenMenu);        // 进入主菜单
}

// ---------------------------------------------------------------------------
void loop()
{
    // 1. 扫描按键/编码器，把事件分发给当前界面（一圈内清空队列）
    input.poll();
    for (InputEvent e = input.getEvent(); e != InputEvent::None;
         e = input.getEvent())
        screens.handle(e);

    // 2. 当前界面的空闲回调（编排状态机推进、测量事件消费、进度刷新）
    screens.tick();

    // 3. BLE 事件泵（RadioState == Off 时是空操作）
    radio.poll();

    delay(1);                          // 串口栈喘息，降低功耗
}
