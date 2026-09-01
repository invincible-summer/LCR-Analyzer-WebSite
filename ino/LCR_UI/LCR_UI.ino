// ============================================================================
// LCR_UI.ino —— ESP32 本地 LCR 仪表主程序（入口 + 装配）
// ----------------------------------------------------------------------------
// 硬件：ESP32 DevKit + ILI9341 SPI 彩屏 + 4 按键（左/右/返回/确定）+ EC11 编码器
// 功能：1) 信号发生器  2) 单频点复阻抗/双端口测量  3) 幅频/相频特性扫频
//       另保留经典蓝牙(SPP)通路，把测量数据转发给 LCR 网站后端。
//
// 模块装配关系（详见 ino/README.md）：
//   input    —— 按键/编码器 -> InputEvent 事件队列
//   display  —— TFT 初始化/主题/控件（DigitEditor、Checkbox）
//   screens  —— 屏幕栈与四个功能界面
//   analysis —— 正弦拟合 -> 幅度比/相位差 -> 调用队友接口求 Z / 增益
//   bt_link  —— 蓝牙数据通路（api_contract 行协议）
//   partner_stubs —— 队友接口 weak 参考实现（真实实现加入后自动覆盖）
//
// 主循环模型：非阻塞事件驱动。每圈：扫描输入 -> 分发事件 -> 当前屏 onTick
// -> 蓝牙收发冲刷。队友的采样函数是同步阻塞调用，由界面在 onTick/onEvent
// 中按需调用（单频点一次；扫频每圈一个频点，保持界面可响应）。
// ============================================================================

#include <Arduino.h>

#include "bt_link.h"
#include "display.h"
#include "hw_config.h"
#include "input.h"
#include "screens.h"

// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    Serial.println("\nLCR-UI booting...");

    ui::begin();               // TFT 彩屏
    input.begin();             // 按键 + 编码器
    bt.begin();                // 蓝牙 SPP 通路（保留网站数据链路）

    screens.begin(&screenMenu);   // 进入主菜单
    Serial.printf("LCR-UI ready. BT name: %s\n", BT_DEVICE_NAME);
}

// ---------------------------------------------------------------------------
void loop()
{
    // 1. 扫描按键/编码器，把事件分发给当前界面（一圈内清空队列）
    input.poll();
    for (InputEvent e = input.getEvent(); e != InputEvent::None;
         e = input.getEvent())
        screens.handle(e);

    // 2. 当前界面的空闲回调（扫频逐点推进、状态刷新等）
    screens.tick();

    // 3. 蓝牙通路：冲刷发送缓冲 + 处理下行命令（非阻塞）
    bt.poll();

    delay(1);                  // 串口/网络栈喘息，降低功耗
}
