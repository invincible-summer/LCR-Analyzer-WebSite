// ============================================================================
// hw_config.h —— 硬件相关配置的唯一来源（Single Source of Truth）
// ----------------------------------------------------------------------------
// 本工程所有引脚、屏幕参数、换算常量全部集中在此文件。
// 更换硬件（不同型号彩屏 / 不同接线 / 不同模拟前端）时 **只需要改这里**，
// 业务代码（界面 / 算法 / 蓝牙）不感知硬件差异。
//
// 默认配置面向：
//   * 主控   ：ESP32 DevKit（经典 ESP32，非 S2/S3/C3——注意 S3 无经典蓝牙 SPP）
//   * 彩屏   ：ILI9341 240x320 SPI 串口屏（TFT_eSPI 库驱动）
//   * 编码器 ：EC11 机械编码器（A/B/Z 三端，Z 为按钮）
//   * 按键   ：4 个独立按键（左 / 右 / 返回 / 确定），另一端接 GND
//
// !! 重要 !!
//   彩屏引脚必须与 TFT_eSPI 库目录下 User_Setup.h 中的定义完全一致，
//   需要修改的内容见 ino/README.md「TFT_eSPI 配置」一节（可直接复制粘贴）。
// ============================================================================

#pragma once

// ---------------------------------------------------------------------------
// [彩屏] TFT_eSPI 引脚 —— 必须与库的 User_Setup.h 保持一致
// 默认接法为 ESP32 DevKit 的 VSPI（SCK=18 / MISO=19 / MOSI=23 / CS=5）
// ---------------------------------------------------------------------------
static constexpr int TFT_PIN_CS   = 5;    // 片选
static constexpr int TFT_PIN_DC   = 2;    // 数据/命令（GPIO2 为启动 strapping 脚，
                                          //   接屏幕 DC 不受影响，属常见接法）
static constexpr int TFT_PIN_RST  = 4;    // 复位（接屏幕 RES）
static constexpr int TFT_PIN_BL   = -1;   // 背光控制脚；-1 = 背光常亮（LED 直接接 3.3V）。
                                          //   若需要程控背光，填对应 GPIO（如 22）

// 屏幕方向：1 = 横屏 320x240（本工程所有界面按横屏设计）
// 常见取值：0 竖屏 / 1 横屏(USB在右) / 3 横屏(USB在左)
static constexpr int TFT_ROTATION = 1;

// 屏幕分辨率（ILI9341 为 240x320；代码一律运行时取 tft.width()/height()，
// 这两个常量仅作说明用途，注意不要与 TFT_eSPI 的 TFT_WIDTH/TFT_HEIGHT 宏重名）
static constexpr int SCREEN_W = 320;   // 横屏下的逻辑宽
static constexpr int SCREEN_H = 240;   // 横屏下的逻辑高

// ---------------------------------------------------------------------------
// [按键] 4 个功能按键：左 / 右 / 返回 / 确定
// 接线：GPIO — 按键 — GND，使用内部上拉（按下为低电平）
// 选脚原则：全部选在支持内部上拉的引脚（避开 34~39 输入专用脚）
// ---------------------------------------------------------------------------
static constexpr int PIN_BTN_LEFT  = 32;   // 「左」
static constexpr int PIN_BTN_RIGHT = 33;   // 「右」
static constexpr int PIN_BTN_BACK  = 25;   // 「返回」
static constexpr int PIN_BTN_OK    = 26;   // 「确定」

// 按键消抖时间（毫秒）：电平稳定超过该时长才认为状态有效
static constexpr uint32_t BTN_DEBOUNCE_MS = 25;
// 长按连发：按住超过该时长后，每过 REPEAT_MS 自动产生一次重复按键事件
// （用于扫频曲线游标的快速移动）
static constexpr uint32_t BTN_REPEAT_FIRST_MS = 450;
static constexpr uint32_t BTN_REPEAT_MS       = 110;

// ---------------------------------------------------------------------------
// [编码器] EC11 旋转编码器
// 接线：A/B 接 GPIO（内部上拉），C 接 GND；Z(按钮) 接 GPIO（内部上拉）— 按下接地
// ---------------------------------------------------------------------------
static constexpr int PIN_ENC_A  = 27;      // 编码器 A 相
static constexpr int PIN_ENC_B  = 14;      // 编码器 B 相
static constexpr int PIN_ENC_SW = 13;      // 编码器按钮（不接也无妨，悬空为高）
static constexpr bool ENC_SW_AS_OK = true; // true = 编码器按钮等效「确定」键

// 每个定位格(clic)产生的 A 相计数。A 相双边沿中断下常见 EC11 为 2；
// 校准方法见 README「编码器校准」：旋转一格跳两格改 4，反向跳一格改 1
static constexpr int ENC_COUNTS_PER_DETENT = 2;

// ---------------------------------------------------------------------------
// [ADC / 模拟前端] 码值 -> 物理量换算（仅蓝牙上传时使用，本地 UI 显示不依赖）
// partner 的 AdcSampleResult 给出 0~4095 的原始码与各级增益，
// 转换公式见 bt_link.h 顶部注释。若实际参考电压不同，改这里即可。
// ---------------------------------------------------------------------------
static constexpr double ADC_VREF        = 3.3;    // ADC 参考电压 (V)
static constexpr double ADC_FULL_SCALE  = 4095.0; // 满量程码值
// 换算系数 K = VREF / FULL_SCALE（码值 x 对应电压 x*K 伏）
static constexpr double ADC_CODE_TO_VOLTS = ADC_VREF / ADC_FULL_SCALE;

// ---------------------------------------------------------------------------
// [蓝牙] 经典蓝牙 SPP 串口通路（用于把测量数据转发给 LCR 网站后端）
// ---------------------------------------------------------------------------
static const char BT_DEVICE_NAME[] = "LCR-UI";   // 蓝牙可见名称
// 单频点 / 扫频测量完成后是否自动通过蓝牙上传原始波形（连接存在时）
static constexpr bool BT_AUTO_UPLOAD = true;

// ---------------------------------------------------------------------------
// [测量范围] 任务书约定：所有频率均在 10 ~ 10000 Hz 之间
// ---------------------------------------------------------------------------
static constexpr int FREQ_MIN_HZ  = 10;
static constexpr int FREQ_MAX_HZ  = 10000;

// 扫频：每 10 倍频率的测量点数上下限，以及数组容量上限
static constexpr int SWEEP_PPD_MIN     = 1;
static constexpr int SWEEP_PPD_MAX     = 50;
static constexpr int SWEEP_PPD_DEFAULT = 10;
static constexpr int SWEEP_MAX_POINTS  = 257;   // 50 点/十倍频 × 3 个十倍频 + 1
