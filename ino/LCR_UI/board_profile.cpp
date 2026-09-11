// ============================================================================
// board_profile.cpp —— UI 外设引脚矩阵（唯一允许出现裸 GPIO 的业务文件）
// ----------------------------------------------------------------------------
// 测量 GPIO 保留清单（应用层/CI 一律禁用，见 tools/static_check.sh Gate F）：
//   GPIO1/2        ADC 电流/电压通道（DNT lcr_adc）
//   GPIO8/9/10/11/12/13/14/18   LCD_CAM 并行正弦 DAC 总线（DNT sinwave）
//   GPIO15/16/17   74HC595 SRCLK/SER/RCLK（DNT lcr_measure）
//
// 其它不占用引脚（ESP32-S3 / N16R8 限制，HARDWARE_MAPPING.md §2）：
//   GPIO0/3/45/46 strap；GPIO19/20 USB；GPIO26-32 模组内 Flash/PSRAM；
//   GPIO33-37 八线 PSRAM；GPIO43/44 UART0（CH340X 烧录/日志）。
// ============================================================================

#include "board_profile.h"

const BoardProfile kBoard = {
    // ---- Display: ST7735S 128x160, 4-wire SPI, 横屏 160x128 ----------------
    // plan.md §10.4 候选映射（待实物 continuity 确认后冻结）：
    //   SCK=H4-4(GPIO4) MOSI=H4-5(GPIO5) CS=H4-6(GPIO6) DC=H4-7(GPIO7)
    //   RST=H5-18(GPIO21)；MISO 不使用
    .tftCs   = 6,
    .tftDc   = 7,
    .tftRst  = 21,
    .spiSck  = 4,
    .spiMosi = 5,
    .spiMiso = PIN_UNUSED,   // 屏幕只写
    // ST7735S datasheet: 写时钟周期最小 66ns（~15.15MHz 上限）-> 首版 10MHz；
    // 升频前必须逻辑分析仪 + 实屏压力测试。
    .tftSpiHz = 10000000,
    .tftWidth  = 128,
    .tftHeight = 160,
    .tftXOffset = 0,         // 常见 1.8" BLACKTAB 起步；实屏校准只改这里
    .tftYOffset = 0,
    .tftRotation = 1,        // 横屏 160x128
    .tftInvert = false,

    // ---- Human input: 4 按键 + EC11（全部经 H5 外接，按下接地 + 内部上拉）---
    // 与测量 GPIO 无冲突；GPIO39-42 属 JTAG 默认组，本固件不启用 JTAG
    //（产品固件因此不能同时依赖 pad JTAG，plan.md §10.3）。
    .keyUp   = 47,   // H5-17  GPIO47
    .keyDown = 48,   // H5-16  GPIO48
    .keyBack = 41,   // H5-7   GPIO41（MTMS/JTAG 组，作普通输入）
    .keyOk   = 42,   // H5-6   GPIO42（MTDI/JTAG 组，作普通输入）
    .encA    = 38,   // H5-10  GPIO38
    .encB    = 39,   // H5-9   GPIO39（MTCK/JTAG 组）
    .encSw   = 40,   // H5-8   GPIO40（MTDO/JTAG 组）
};
