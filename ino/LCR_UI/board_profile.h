// ============================================================================
// board_profile.h —— 自制 ESP32-S3 板硬件配置的唯一入口（Single Source of Truth）
// ----------------------------------------------------------------------------
// 依据（真源链，详见 docs/HARDWARE_MAPPING.md）：
//   1. ino/databook/自制开发板资料/.../开发板原理图.pdf 与 开发板手册.pdf
//      （排针 H4/H5 引脚表、板载资源、受限引脚说明）；
//   2. ESP32-S3 / ESP32-S3-WROOM-1-N16R8 数据手册（strap、USB、Flash/PSRAM、
//      ADC unit/channel、IO 能力）；
//   3. ST7735S 数据手册（串行写周期 ≥66ns → SPI ≤15.15MHz，首版取 10MHz）。
//
// 规则（plan.md §2.1）：
//   * 所有 GPIO 只在本文件对应的 board_profile.cpp 中集中定义；业务文件
//     不得出现裸 GPIO 数字（CI 静态检查）。
//   * 原理图中同时连接启动 strap / USB / PSRAM / 板载器件的引脚，必须在
//     实现旁注释原因与限制。
//   * ADC 引脚必须核对到 ESP32-S3 的 ADC unit/channel。
//
// 本板（课程自制 ESP32-S3 开发板）没有板载 TFT / 按键 / 编码器 / LCR 模拟
// 前端：仪器外设全部通过 H4/H5 排针外接。本 profile 声明的就是这套外接
// 布线（对应实板接线表见 HARDWARE_MAPPING.md §4），更换接线只改这里。
// ============================================================================

#pragma once

#include <stdint.h>

// 未使用的引脚统一用该哨兵值（如 ST7735S 4 线屏只写不读时的 MISO）
#define PIN_UNUSED (-1)

// ---------------------------------------------------------------------------
// ADC 资源声明（ESP32-S3：ADC1 = GPIO1..10 → CH0..CH9）
// 本仪器全部模拟通道固定使用 ADC1：ADC2 的 DMA continuous 模式受硬件
// errata/稳定性限制（plan.md §2.2 明确禁止依赖），且 Wi-Fi 开启时不可用。
// ---------------------------------------------------------------------------
enum class AdcUnit : uint8_t { Adc1 };   // Adc2 有意不提供

struct AdcGpio {
    int8_t gpio;          // GPIO 编号（-1 = 未使用）
    AdcUnit unit;         // ADC 单元
    uint8_t channel;      // 该单元内的通道号
};

struct BoardProfile {
    // ---- Display（ST7735S 4-wire SPI，经 H4 外接）------------------------
    int tftCs;
    int tftDc;
    int tftRst;
    int spiSck;
    int spiMosi;
    int spiMiso;          // 屏幕只写 → PIN_UNUSED
    int spiMiso2;         // 预留（未使用）
    uint32_t tftSpiHz;    // ST7735S 写周期 ≥66ns → 上限 ~15.15MHz；首版 10MHz
    uint16_t tftWidth;    // 控制器物理 RAM（portrait 基准）
    uint16_t tftHeight;
    int16_t tftXOffset;   // 可见区偏移：属实板属性，color-bar 实测后只改这里
    int16_t tftYOffset;
    uint8_t tftRotation;  // 1 = 横屏 160x128（本工程所有界面按横屏设计）
    bool tftInvert;       // 颜色反转（面板极性，属实板属性）

    // ---- Human input（4 按键 + EC11 编码器，经 H5 外接，另一端接 GND）----
    int keyUp, keyDown, keyBack, keyOk;
    int encA, encB, encSw;

    // ---- Analog paths（外接 LCR 前端的 4 路测量信号，全部 ADC1）----------
    AdcGpio adcVoltageGpio;       // 单端口：DUT 电压 V
    AdcGpio adcCurrentGpio;       // 单端口：电流感测（R_sense 上的电压）
    AdcGpio adcPort2InputGpio;    // 双端口：网络输入电压 Vin
    AdcGpio adcPort2OutputGpio;   // 双端口：网络输出电压 Vout

    // ---- Excitation / front-end control（经 H4 外接）----------------------
    // 激励源：ESP32-S3 无片上 DAC；采用 I2S 标准模式 → 外部 PCM5102A DAC
    // （幅度固定 = "calibrated nominal drive"，语义见 plan.md §2.3）。
    int excitationBck;
    int excitationLrck;
    int excitationData;
    int excitationMclk;           // PCM5102 可工作于 BCK 主时钟模式 → 未用

    int rangeSelectPins[2];       // 前端量程选择（0 = 继电器/MUX 断开）
    int frontEndEnablePin;        // 前端使能（低有效与否由外接硬件决定）

    // ---- Electrical constants（硬件事实，不是拟合参数）---------------------
    // v1 参考前端：DUT 电流经 100 Ω 精密采样电阻，ADC 直接测其两端电压；
    // 跨阻放大器不使用（增益 1.0）。数值必须与实际搭建的前端一致，
    // 残余误差由 CalibrationProfile 的复数校准层吸收。
    double nominalCurrentSenseOhm;
    double nominalTransimpedanceGain;
};

extern const BoardProfile kBoard;

// ADC GPIO → (unit, channel) 反查；非法 gpio 返回 false。业务代码用它
// 校验，而不是自己推 ADC 通道（plan.md §2.1 约束 4）。
bool adcLookup(int gpio, AdcUnit& unit, uint8_t& channel);
