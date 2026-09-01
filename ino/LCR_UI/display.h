// ============================================================================
// display.h —— TFT 显示基础层：初始化 / 主题配色 / 通用绘制 / 输入控件
// ----------------------------------------------------------------------------
// 分层约定：
//   * 屏幕业务代码（screen_*.cpp）通过本文件提供的原语画界面，
//     不直接操作颜色数值与字体细节，保证全工程观感一致；
//   * 控件（DigitEditor / Checkbox）自带事件处理与绘制，
//     界面代码只负责「焦点分配 + 调用 onEvent/draw」。
//
// 使用的 TFT_eSPI 内置字体：
//   font1(8px) 坐标刻度 / font2(16px) 标签正文 / font4(26px) 数值
//   font7(48px 七段码, 仅 0-9 与 :-. ) 大号频率显示
// 注意：内置字体不含中文，界面文案使用英文；中文见代码注释。
// ============================================================================

#pragma once

#include <TFT_eSPI.h>

#include "input.h"

extern TFT_eSPI tft;   ///< 全局唯一的 TFT 实例（display.cpp 中定义）

// RGB888 -> RGB565 编译期换算
#define UI_RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/// 主题配色（科学仪器暗色风，双曲线用 蓝/橙 —— 与网站前端色板同源，色盲友好）
namespace ui {
inline constexpr uint16_t C_BG     = UI_RGB565(12, 16, 24);    // 背景
inline constexpr uint16_t C_PANEL  = UI_RGB565(28, 36, 50);    // 面板/顶栏
inline constexpr uint16_t C_FG     = UI_RGB565(232, 236, 240); // 主文字
inline constexpr uint16_t C_DIM    = UI_RGB565(120, 130, 145); // 次要文字
inline constexpr uint16_t C_ACCENT = UI_RGB565(0, 170, 255);   // 焦点/光标
inline constexpr uint16_t C_OK     = UI_RGB565(0, 200, 110);   // 运行/正常
inline constexpr uint16_t C_ERR    = UI_RGB565(255, 80, 80);   // 停止/错误
inline constexpr uint16_t C_GRID   = UI_RGB565(46, 56, 72);    // 曲线图网格
inline constexpr uint16_t C_AXIS   = UI_RGB565(140, 150, 165); // 坐标轴/刻度
inline constexpr uint16_t C_CH1    = UI_RGB565(86, 148, 214);  // 曲线1：幅度/|Z|（蓝）
inline constexpr uint16_t C_CH2    = UI_RGB565(255, 150, 40);  // 曲线2：相位（橙）

/// 显示初始化（setup 中调用一次）：init / 旋转 / 背光 / 清屏
void begin();

/// 顶部状态栏：标题 + 蓝牙连接状态（btOn=true 时 BT 标记点亮）
void topBar(const char* title, bool btOn);

/// 底部操作提示行（如 "OK:START  BACK:RETURN"）
void bottomHint(const char* hint);

/// 进度条（frac ∈ [0,1]）
void progressBar(int x, int y, int w, int h, double frac, uint16_t color);

/// 标签-数值两列行：左标签(font2 灰) + 右数值(font4 指定色)，y 为行顶
void row(int x, int y, int w, const char* label, const char* value, uint16_t color);

/// 工程计数法格式化（自动 G/M/k/''/m/u/n 前缀），返回 buf
const char* fmtEng(double v, const char* unit, char* buf, int len, int prec = 3);
/// 频率格式化：10.0Hz / 1.234kHz / 10.00kHz
const char* fmtFreq(double hz, char* buf, int len);
/// 相位/角度格式化："-45.2°"（° 为 ASCII 'o'? —— 用 "deg" 更清晰）
const char* fmtDeg(double deg, char* buf, int len);
}  // namespace ui

// ============================================================================
// 数字位编辑器：左右键移动数位光标，编码器 0-9 循环调当前位
// ============================================================================
class DigitEditor {
public:
    /// 配置取值范围与位数（vmin/vmax 需能被 ndigits 位十进制表示覆盖）
    void setup(int32_t vmin, int32_t vmax, int ndigits, int32_t v);
    /// 处理事件：Left/Right 移动位数光标——到达最左/最右边界时返回 false，
    /// 把事件交还界面层（用于跨字段转移焦点）；EncInc/EncDec 将当前位 0-9
    /// 循环加减，修改后自动钳位到 [vmin, vmax] 并重排各位数字。
    bool onEvent(InputEvent e);
    /// 设置当前编辑位（0 = 最高位），供界面从不同方向进入时定位光标
    void setCursor(int pos) { m_pos = (int8_t)pos; }
    int32_t value() const { return m_value; }
    /// 绘制在 (x,y)，fontH=26 用 font4、fontH=48 用 font7（七段码）
    void draw(int x, int y, int fontH, bool focused) const;
    /// 控件总宽度（像素），用于布局计算
    int width(int fontH) const;

private:
    void syncFromValue();   // 数值 -> 各位数字
    int32_t clampValue(int32_t v) const { return v < m_vmin ? m_vmin : (v > m_vmax ? m_vmax : v); }

    int32_t m_value = 0, m_vmin = 0, m_vmax = 1;
    int8_t  m_ndigits = 1, m_pos = 0;
    uint8_t m_digits[7] = {0};   // 各位数字（高到低）
};

// ============================================================================
// 复选框：获得焦点时编码器旋转切换选中状态（任务书要求的交互）
// ============================================================================
class Checkbox {
public:
    void setup(const char* labelOff, const char* labelOn, bool v);
    /// 处理事件：EncInc/EncDec 切换选中，返回是否消费
    bool onEvent(InputEvent e);
    bool value() const { return m_value; }
    void toggle() { m_value = !m_value; }
    void draw(int x, int y, bool focused) const;

private:
    const char* m_labelOff = "";
    const char* m_labelOn  = "";
    bool m_value = false;
};
