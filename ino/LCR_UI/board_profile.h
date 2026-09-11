// ============================================================================
// board_profile.h —— UI 外设引脚唯一出处（UI-only；测量硬件不在此文件）
// ----------------------------------------------------------------------------
// v4.1.0 重构后的边界（plan.md §10.6）：
//   * 测量硬件（ADC GPIO1/2、LCD_CAM 并行 DAC GPIO8/9/10/11/12/13/14/18、
//     74HC595 GPIO15/16/17）全部由 DO_NOT_TOUCH 头文件定义并在硬件验证，
//     应用层禁止触碰 —— 因此本 profile 不再包含任何 ADC / 激励 / TIA /
//     量程 / 采样率 / 采样电阻字段；
//   * 本文件只描述 UI 外设：ST7735S 屏（SPI）与 4 按键 + EC11 编码器；
//   * 真源依据：开发板原理图/手册（ino/databook/自制开发板资料）、
//     ESP32-S3 数据手册（strap/USB/JTAG/Flash/PSRAM 限制）、
//     ST7735S 数据手册（串行写时钟周期 >=66ns -> SPI 上限 ~15.15MHz）。
//
// !! 实板冻结条件（plan.md 阶段 D）!!
//   TFT 新映射（SCK=4 MOSI=5 CS=6 DC=7 RST=21）采用 plan.md §10.4 的
//   候选接法，尚未做实物 continuity/上电 smoke test；实测通过前，接线的
//   最终权威是本文件 + docs/HARDWARE_MAPPING.md（若实测不符只改这两处）。
// ============================================================================

#pragma once

#include <stdint.h>

// 未使用的引脚统一用该哨兵值（如 ST7735S 4 线屏只写不读时的 MISO）
#define PIN_UNUSED (-1)

struct BoardProfile {
    // ---- Display（ST7735S 4-wire SPI，经 H4/H5 排针外接）-----------------
    // 已离开 GPIO10-14（LCD_CAM 并行 DAC 总线冲突，plan.md §10.4）
    int tftCs;
    int tftDc;
    int tftRst;
    int spiSck;
    int spiMosi;
    int spiMiso;          // 屏幕只写 -> PIN_UNUSED
    uint32_t tftSpiHz;    // ST7735S 写周期 >=66ns -> 上限 ~15.15MHz；首版 10MHz
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
