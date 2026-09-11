// ============================================================================
// board_profile.cpp —— BoardProfile 引脚矩阵（唯一允许出现裸 GPIO 的业务文件）
// ----------------------------------------------------------------------------
// 真源：docs/HARDWARE_MAPPING.md（原理图 + 开发板手册 + 数据手册交叉核对）。
// 表中「排针位」指课程自制 ESP32-S3 开发板 H4（左排）/H5（右排）的脚号。
//
// !! 实板冻结条件（plan.md 阶段 A）!!
//   本矩阵已按原理图/手册逐条核对（见 HARDWARE_MAPPING.md §3 交叉核对表），
//   连续性/上电 smoke test 尚待实板执行；若实测与下表不符，只改本文件。
// ============================================================================

#include "board_profile.h"

// ---------------------------------------------------------------------------
// 未占用 / 禁用引脚清单（原因注释是本文件的强制内容）：
//   GPIO0    —— strap（BOOT 键 SW1，10kΩ 上拉；H5-14），禁用作 IO
//   GPIO3    —— strap（JTAG 信号源选择；H4-13），内部弱下拉，不用
//   GPIO19/20—— USB D-/D+ 网络；本板 Micro USB 经 CH340X 隔离，非直连，
//                但网络仍属 USB 专用（H5-20/19），不用
//   GPIO26-32—— 模块内 Octal Flash/PSRAM 专用，未引出，严禁使用
//   GPIO33-37—— N16R8 八线 PSRAM 高 4 位占用（手册 §3.3.2；H5-13/12/11
//                虽引出但 N16R8 上不可用），不用
//   GPIO43/44—— UART0 → 隔离 → CH340X → Micro USB（烧录/日志），不用
//   GPIO45/46—— strap（VDD_SPI / 启动模式 + ROM 日志；H5-15/H4-14），不用
//   GPIO1/2  —— 预留给 I2C（手册建议的常规用途），本固件不用
// ---------------------------------------------------------------------------

const BoardProfile kBoard = {
    // ---- Display: ST7735S 128x160, 4-wire SPI, 横屏 160x128 ----------------
    .tftCs   = 10,   // H4-16  GPIO10（FSPI CS0 备用功能，这里作普通 CS）
    .tftDc   = 14,   // H4-20  GPIO14
    .tftRst  = 13,   // H4-19  GPIO13
    .spiSck  = 12,   // H4-18  GPIO12（FSPICLK 备用功能）
    .spiMosi = 11,   // H4-17  GPIO11（FSPID 备用功能）
    .spiMiso = PIN_UNUSED,   // 屏幕只写
    .spiMiso2 = PIN_UNUSED,
    // ST7735S datasheet: 写时钟周期最小 66ns（≈15.15MHz 上限）→ 首版 10MHz；
    // 提高前必须逻辑分析仪 + 实屏压力测试（plan.md §2.4）。
    .tftSpiHz = 10000000,
    .tftWidth  = 128,
    .tftHeight = 160,
    // 可见区偏移与面板极性属于实板属性：默认按常见 1.8" 128x160 BLACKTAB
    // （0,0 / 不反转）起步，实屏 color-bar / 边界矩形测试后只改这里。
    .tftXOffset = 0,
    .tftYOffset = 0,
    .tftRotation = 1,          // 横屏 160x128
    .tftInvert = false,

    // ---- Human input: 4 按键 + EC11（全部经 H5 外接，按下接地，内部上拉）---
    .keyUp   = 47,   // H5-17  GPIO47
    .keyDown = 48,   // H5-16  GPIO48
    .keyBack = 41,   // H5-7   GPIO41（默认 MTMS/JTAG 组，作普通输入）
    .keyOk   = 42,   // H5-6   GPIO42（默认 MTDI/JTAG 组，作普通输入）
    .encA    = 38,   // H5-10  GPIO38
    .encB    = 39,   // H5-9   GPIO39（默认 MTCK/JTAG 组；JTAG 默认引脚在
                     //                  Arduino 固件中未启用，作普通输入）
    .encSw   = 40,   // H5-8   GPIO40（默认 MTDO/JTAG 组）

    // ---- Analog paths: 4 路全部 ADC1，逐条核对 channel（S3 数据手册）-------
    // GPIO4 = ADC1_CH3, GPIO5 = ADC1_CH4, GPIO6 = ADC1_CH5, GPIO7 = ADC1_CH6
    .adcVoltageGpio     = { 4, AdcUnit::Adc1, 3 },   // H4-4   DUT 电压
    .adcCurrentGpio     = { 5, AdcUnit::Adc1, 4 },   // H4-5   采样电阻电压
    .adcPort2InputGpio  = { 6, AdcUnit::Adc1, 5 },   // H4-6   双端口 Vin
    .adcPort2OutputGpio = { 7, AdcUnit::Adc1, 6 },   // H4-7   双端口 Vout

    // ---- Excitation: I2S → 外部 PCM5102A DAC（BCK/LRCK/DATA 三线）----------
    .excitationBck  = 17,   // H4-10  GPIO17（U1TXD 备用功能，不用 UART1）
    .excitationLrck = 18,   // H4-11  GPIO18（U1RXD 备用功能）
    .excitationData = 21,   // H5-18  GPIO21（纯数字脚）
    .excitationMclk = PIN_UNUSED,   // PCM5102 SCK 接地（内部 PLL 模式）

    // ---- Front-end control ---------------------------------------------------
    .rangeSelectPins  = { 15, 16 },   // H4-8 GPIO15, H4-9 GPIO16（ADC2 脚，
                                      // 本仪器不用 ADC2，作数字量程控制）
    .frontEndEnablePin = 8,           // H4-12 GPIO8；高=前端上电

    // ---- Electrical constants（外接前端的硬件事实）----------------------------
    .nominalCurrentSenseOhm = 100.0,    // v1 参考：100 Ω 精密采样电阻
    .nominalTransimpedanceGain = 1.0,   // v1 参考：无跨阻级
};

// ---------------------------------------------------------------------------
// ADC GPIO → (unit, channel)。表内容来自 ESP32-S3 数据手册 ADC 章节：
// ADC1: GPIO1..GPIO10 = CH0..CH9；ADC2: GPIO11..GPIO20 = CH0..CH9。
// ADC2 故意不接受（本仪器不使用，原因见头文件注释）。
// ---------------------------------------------------------------------------
bool adcLookup(int gpio, AdcUnit& unit, uint8_t& channel)
{
    if (gpio >= 1 && gpio <= 10) {          // ADC1
        unit = AdcUnit::Adc1;
        channel = (uint8_t)(gpio - 1);
        return true;
    }
    return false;                            // GPIO11..20(ADC2) 与数字脚：拒绝
}
