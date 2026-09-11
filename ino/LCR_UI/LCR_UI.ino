// ============================================================================
// LCR_UI.ino —— ESP32-S3 LCR 仪表主程序（入口 + 装配）· v4.1.0
// ----------------------------------------------------------------------------
// 硬件：自制 ESP32-S3 开发板（N16R8）+ 外接 ST7735S 屏 / 4 按键 / EC11
//       编码器 / PCM5102A 激励 DAC / LCR 模拟前端（引脚矩阵见
//       docs/HARDWARE_MAPPING.md 与 board_profile.cpp）。
//
// 顶层三个用户模式（plan.md §5）：
//   1) 单元件 R/C/L 自动识别与测量（不启动 BLE）
//   2) 单端口阻抗扫频 -> 采样完成后 BLE 上传网站拟合（f,re,im CSV）
//   3) 双端口扫频（H=Vout/Vin）-> BLE 上传网站显示曲线
//   另有隐藏诊断页（信号发生器）。
//
// 模块装配：
//   board_profile  —— 引脚/硬件常量唯一出处
//   excitation_driver / adc_capture —— I2S 激励与 ADC DMA 采集（IDF5 驱动）
//   measurement_engine / sweep_engine —— poll-driven 状态机（非阻塞）
//   component_meter —— R/C/(L+DCR) 三模型分类
//   dataset/csv/CRC + radio_manager/ble —— 封存数据集与 BLE GATT v1
//   input / display / screens / plot / dsp_fit —— 保留的 UI 与 DSP 框架
//
// 主循环模型：非阻塞事件驱动。每圈：扫描输入 -> 分发事件 -> 当前屏
// onTick（推进测量状态机）-> BLE 事件泵。业务逻辑禁止 delay() 时序。
//
// 射频互斥（硬 invariant）：测量窗口内 BLE 完全关闭；dataset seal 后才允许
// startBleForSealedDataset()。固件不初始化 Wi-Fi。
// ============================================================================

#include <Arduino.h>

#include "adc_capture.h"
#include "board_profile.h"
#include "display.h"
#include "excitation_driver.h"
#include "fw_version.h"
#include "input.h"
#include "radio_manager.h"
#include "screens.h"
#include "sweep_engine.h"

// ---------------------------------------------------------------------------
// 引擎装配：真实硬件驱动注入（host 单测注入 mock，见 ino/test/）
// ---------------------------------------------------------------------------
MeasurementEngine engine(excitationDriver, adcCapture, &frontEndGpio,
                         factoryCalibration(),
                         kBoard.nominalCurrentSenseOhm,
                         kBoard.nominalTransimpedanceGain);
SweepEngine sweep(engine);

// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    Serial.printf("\nLCR-UI v%s booting (BLE protocol %d, z-schema v%d, "
                  "h-schema v%d)\n",
                  LCR_FW_VERSION, LCR_BLE_PROTOCOL_VERSION,
                  LCR_Z_CSV_SCHEMA_VERSION, LCR_H_CSV_SCHEMA_VERSION);

    // 注意：启动时不初始化 BLE（plan.md §1.2）——测量完成封存后才允许。
    frontEndGpio.frontEndDisable();    // 前端安全态
    excitationDriver.excitationStop();

    ui::begin();                       // ST7735S（BoardProfile 参数）
    input.begin();                     // 按键 + 编码器
    screens.begin(&screenMenu);        // 进入主菜单
    Serial.println("LCR-UI ready. BLE stays OFF until a sweep is sealed.");
}

// ---------------------------------------------------------------------------
void loop()
{
    // 1. 扫描按键/编码器，把事件分发给当前界面（一圈内清空队列）
    input.poll();
    for (InputEvent e = input.getEvent(); e != InputEvent::None;
         e = input.getEvent())
        screens.handle(e);

    // 2. 当前界面的空闲回调（测量状态机推进、进度刷新等）
    screens.tick();

    // 3. BLE 事件泵（RadioState == Off 时是空操作）
    radio.poll();

    delay(1);                          // 串口栈喘息，降低功耗
}
