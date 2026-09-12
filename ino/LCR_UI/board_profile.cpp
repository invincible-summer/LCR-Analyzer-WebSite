// ============================================================================
// board_profile.cpp —— UI/控制外设引脚矩阵
// ----------------------------------------------------------------------------
// 最终 DNT 测量接线：ADC GPIO1/2；LCD_CAM DAC GPIO6/7/15/16/17/18/8/9；
// 74HC595 GPIO21/19/20。GPIO4 在开发板 H4-4 上为空闲 GPIO（ADC1_CH3/
// TOUCH4 复用），不是 strapping pin，故用于只读诊断开关。
// ============================================================================

#include "board_profile.h"

const BoardProfile kBoard = {
    .tftCs   = 10,
    .tftDc   = 14,
    .tftRst  = 13,
    .spiSck  = 12,
    .spiMosi = 11,
    .spiMiso = PIN_UNUSED,
    .tftSpiHz = 10000000,
    .tftWidth  = 128,
    .tftHeight = 160,
    .tftXOffset = 0,
    .tftYOffset = 0,
    .tftRotation = 0,
    .tftInvert = false,

    .keyUp   = 47,
    .keyDown = 48,
    .keyBack = 41,
    .keyOk   = 42,
    .encA    = 38,
    .encB    = 39,
    .encSw   = 40,

    .diagEnable = 4,   // H4-4 GPIO4；HIGH=diagnostics ON, LOW=OFF
};
