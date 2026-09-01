// ============================================================================
// input.h —— 人机输入：4 个功能按键 + EC11 旋转编码器
// ----------------------------------------------------------------------------
// 事件模型：所有输入统一抽象为 InputEvent，放入一个小型环形队列；
// 主循环每圈取走事件分发给当前界面，保证界面代码不直接触碰 GPIO。
//
//   Left / Right / Ok / Back  —— 4 个物理按键（支持长按连发，用于游标快速移动）
//   EncInc / EncDec           —— 编码器顺时针 / 逆时针旋转一格(定位档)
//   编码器按钮可选等效 Ok（hw_config.h: ENC_SW_AS_OK）
// ============================================================================

#pragma once

#include <stdint.h>

enum class InputEvent : uint8_t {
    None = 0,
    Left, Right, Ok, Back,   // 按键事件
    EncInc, EncDec,          // 编码器旋转事件（一格一个事件）
};

class Input {
public:
    void begin();              // 配置 GPIO / 上拉 / 中断（在 setup 中调用一次）
    void poll();               // 按键扫描 + 事件入队（主循环每圈调用）
    InputEvent getEvent();     // 取出一条事件（FIFO），无事件返回 None

    /// 编码器累计计数（四倍频原始计数，诊断用；业务请用 EncInc/EncDec 事件）
    int32_t encoderRawCount() const;

private:
    struct Button {
        uint8_t pin;
        bool    stable;            // 消抖后的稳定电平（true=按下）
        bool    lastRaw;           // 上次采样原始电平
        uint32_t lastChangeMs;     // 原始电平最近一次跳变时刻（消抖计时基准）
        uint32_t pressedMs;        // 本轮按下开始时刻（连发计时）
        bool     repeatSent;       // 是否已进入连发阶段
    };

    void pushEvent(InputEvent e);             // 事件入队（满则丢弃最旧）
    void scanButton(Button& b, InputEvent ev);// 单键扫描：消抖 + 沿检测 + 连发

    static constexpr int QUEUE_SIZE = 16;     // 事件队列容量
    InputEvent m_queue[QUEUE_SIZE];
    uint8_t    m_qHead = 0, m_qCount = 0;

    Button m_btns[4];
};

/// 全局唯一输入实例（display 之外所有界面共用）
extern Input input;
