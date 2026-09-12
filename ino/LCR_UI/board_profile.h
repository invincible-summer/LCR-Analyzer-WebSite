// ============================================================================
// board_profile.h —— UI 外设引脚唯一出处（UI-only；测量硬件不在此文件）
// ----------------------------------------------------------------------------
// v4.1.0 重构后的边界：
//   * 测量硬件全部由 DO_NOT_TOUCH 头文件定义并在硬件验证，应用层禁止触碰；
//   * 本 profile 只描述 UI 外设：ST7735S 屏（SPI）与 4 按键 + EC11 编码器；
//   * 最终测量接线当前占用 ADC GPIO1/2、LCD_CAM 并行 DAC
//     GPIO6/7/15/16/17/18/8/9、74HC595 GPIO21/19/20；
//   * 因此 TFT 使用已释放且在 H4 连续引出的 GPIO10-14：
//     CS=10、MOSI=11、SCK=12、RST=13、DC=14；MISO 不使用。
//   * ST7735S 数据手册：4-line serial 写时钟周期 >=66ns，产品固定 10MHz。
//
// 接线真源优先级：DO_NOT_TOUCH 测量头文件 + board_profile.cpp；二者冲突时
// 必须停止构建并人工确认，禁止用 CI 白名单掩盖引脚争用。
// ============================================================================

#pragma once

#include <stdint.h>

// 未使用的引脚统一用该哨兵值（如 ST7735S 4 线屏只写不读时的 MISO）
#define PIN_UNUSED (-1)

struct BoardProfile {
    // ---- Display（ST7735S 4-wire SPI，经 H4 排针外接）--------------------
    int tftCs;
    int tftDc;
    int tftRst;
    int spiSck;
    int spiMosi;
    int spiMiso;          // 屏幕只写 -> PIN_UNUSED
    uint32_t tftSpiHz;    // ST7735S 写周期 >=66ns -> 上限 ~15.15MHz；产品 10MHz
    uint16_t tftWidth;    // 控制器物理 RAM（portrait 基准）
    uint16_t tftHeight;
    int16_t tftXOffset;   // 可见区偏移：属实板属性，color-bar 实测后只改这里
    int16_t tftYOffset;
    uint8_t tftRotation;  // 1 = 横屏 160x128（本工程所有界面按横屏设计）
    bool tftInvert;       // 颜色反转（面板极性，属实板属性）

    // ---- Human input（4 按键 + EC11 编码器，经 H5 外接，另一端接 GND）----
    int keyUp, keyDown, keyBack, keyOk;
    int encA, encB, encSw;
};

extern const BoardProfile kBoard;
