// ============================================================================
// board_profile.cpp —— UI 外设引脚矩阵（唯一允许出现裸 GPIO 的业务文件）
// ----------------------------------------------------------------------------
// 测量 GPIO 最终接线（应用层/CI 一律禁用；真源为 DO_NOT_TOUCH 头文件）：
//   GPIO1/2                    ADC 电流/电压通道（DNT lcr_adc）
//   GPIO6/7/15/16/17/18/8/9   LCD_CAM 并行正弦 DAC D0..D7（DNT sinwave）
//   GPIO21/19/20               74HC595 SRCLK/SER/RCLK（DNT lcr_measure）
//
// 其它不占用引脚（ESP32-S3 / N16R8 限制）：
//   GPIO0/3/45/46 strap；GPIO26-32 模组内 Flash/PSRAM；
//   GPIO33-37 八线 PSRAM；GPIO43/44 UART0（CH340X 烧录/日志）。
// GPIO19/20 虽兼作原生 USB D-/D+，但最终测量接线已明确占用；产品构建保持
// CDCOnBoot=default/Disabled，禁止再启用原生 USB CDC 与该接线争用。
// ============================================================================

#include "board_profile.h"

const BoardProfile kBoard = {
    // ---- Display: ST7735S 128x160, 4-wire SPI, 原生竖屏 128x160 ------------
    // ST7735S GM=11 的可见 RAM 地址是 128 columns x 160 rows。用户界面以
    // 这一原生几何为唯一布局坐标系，不再把 160x128 横屏当作面板分辨率。
    // 最终测量接线释放 GPIO10-14，因此 TFT 使用 H4 连续排针：
    //   CS=H4-16(GPIO10) MOSI=H4-17(GPIO11) SCK=H4-18(GPIO12)
    //   RST=H4-19(GPIO13) DC=H4-20(GPIO14)；MISO 不使用。
    .tftCs   = 10,
    .tftDc   = 14,
    .tftRst  = 13,
    .spiSck  = 12,
    .spiMosi = 11,
    .spiMiso = PIN_UNUSED,   // 屏幕只写
    // ST7735S datasheet: 写时钟周期最小 66ns（~15.15MHz 上限）-> 产品 10MHz；
    // 升频前必须逻辑分析仪 + 实屏压力测试。
    .tftSpiHz = 10000000,
    .tftWidth  = 128,
    .tftHeight = 160,
    .tftXOffset = 0,
    .tftYOffset = 0,
    .tftRotation = 0,        // portrait: tft.width()==128, tft.height()==160
    .tftInvert = false,

    // ---- Human input: 4 按键 + EC11（全部经 H5 外接，按下接地 + 内部上拉）---
    // GPIO39-42 属 JTAG 默认组，本固件不启用 pad JTAG。
    .keyUp   = 47,   // H5-17  GPIO47
    .keyDown = 48,   // H5-16  GPIO48
    .keyBack = 41,   // H5-7   GPIO41（MTMS/JTAG 组，作普通输入）
    .keyOk   = 42,   // H5-6   GPIO42（MTDI/JTAG 组，作普通输入）
    .encA    = 38,   // H5-10  GPIO38
    .encB    = 39,   // H5-9   GPIO39（MTCK/JTAG 组）
    .encSw   = 40,   // H5-8   GPIO40（MTDO/JTAG 组）
};
