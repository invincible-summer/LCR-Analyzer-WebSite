// ============================================================================
// board_profile.h —— UI/控制外设引脚唯一出处（测量硬件不在此文件）
// ----------------------------------------------------------------------------
// 测量硬件全部由 DO_NOT_TOUCH 头文件定义；本 profile 只描述 TFT、按键、
// EC11 与 GPIO4 诊断开关。ST7735S 4-line serial 产品时钟固定 10 MHz。
// ============================================================================

#pragma once

#include <stdint.h>

#define PIN_UNUSED (-1)

struct BoardProfile {
    int tftCs;
    int tftDc;
    int tftRst;
    int spiSck;
    int spiMosi;
    int spiMiso;
    uint32_t tftSpiHz;
    uint16_t tftWidth;
    uint16_t tftHeight;
    int16_t tftXOffset;
    int16_t tftYOffset;
    uint8_t tftRotation;
    bool tftInvert;

    int keyUp, keyDown, keyBack, keyOk;
    int encA, encB, encSw;

    // 与 DO_NOT_TOUCH_EXAMPLE 完全相同的诊断开关语义：INPUT_PULLUP，
    // HIGH（悬空/3.3V）= DNT 诊断打印开启，LOW（接 GND）= 关闭。
    int diagEnable;
};

extern const BoardProfile kBoard;
