// ============================================================================
// input.cpp —— 按键消抖/连发 与 EC11 全正交状态机解码
// ----------------------------------------------------------------------------
// * 按键：每圈扫描原始电平，25ms 稳定后确认状态；下降沿产生一次事件；
//   按住不放超过 450ms 后每 110ms 连发一次。
// * 编码器：A/B 两相双边沿中断，按完整 2-bit Gray 状态转移表解码；
//   相邻抖动会形成 +1/-1 抵消，非法双比特跳变会清掉半格累计，只有完整
//   4 个有效 quarter-step 才发布一格事件。因此不再把 B 相触点抖动误判成
//   方向变化，也不靠固定 A 相边沿数“猜”一格。
// * GPIO 一律来自 BoardProfile（本文件不出现裸 GPIO 数字）。
// ============================================================================

#include "input.h"

#include "board_profile.h"
#include "quadrature.h"

#include <Arduino.h>

Input input;

// 消抖/连发时序（输入域常量，与引脚无关）
static constexpr uint32_t BTN_DEBOUNCE_MS = 25;
static constexpr uint32_t BTN_REPEAT_FIRST_MS = 450;
static constexpr uint32_t BTN_REPEAT_MS = 110;
static constexpr int8_t ENC_QUARTERS_PER_DETENT = 4;

// 编码器状态（ISR 与 poll 共享）。ISR 只维护非常小的整数状态；poll() 在
// 关中断窗口内读取累计 detent 数，UI 事件发布仍在主循环完成。
static volatile uint8_t s_encPrevAb = 0;
static volatile int8_t s_encQuarter = 0;
static volatile int32_t s_encDetents = 0;
static int32_t s_encLastIssued = 0;

static inline uint8_t IRAM_ATTR readEncoderAb()
{
    return (uint8_t)((digitalRead(kBoard.encA) ? 2u : 0u) |
                     (digitalRead(kBoard.encB) ? 1u : 0u));
}

// ---------------------------------------------------------------------------
// EC11 全状态解码：
//   00 -> 01 -> 11 -> 10 -> 00 为正方向；反向为负方向。
// 接点抖动通常在相邻状态间往返，quarter-step 自然抵消；若一次采到两位同时
// 变化（例如 00 -> 11），该路径不满足正交编码约束，清空半格累计，避免将
// 丢边沿/毛刺拼接成一个虚假的 detent。
// ---------------------------------------------------------------------------
static void IRAM_ATTR encoderIsr()
{
    const uint8_t current = readEncoderAb();
    const uint8_t previous = s_encPrevAb;
    if (current == previous) return;

    const int8_t delta = quadratureTransition(previous, current);
    s_encPrevAb = current;

    if (delta == 0) {
        s_encQuarter = 0;                    // 非法双比特跳变：丢弃半格
        return;
    }

    int8_t q = (int8_t)(s_encQuarter + delta);
    if (q >= ENC_QUARTERS_PER_DETENT) {
        ++s_encDetents;
        q = 0;
    } else if (q <= -ENC_QUARTERS_PER_DETENT) {
        --s_encDetents;
        q = 0;
    }
    s_encQuarter = q;
}

// ---------------------------------------------------------------------------
void Input::begin()
{
    // ---- 按键：内部上拉，按下接地（低电平有效） ---------------------------
    const uint8_t pins[4] = {(uint8_t)kBoard.keyUp, (uint8_t)kBoard.keyDown,
                             (uint8_t)kBoard.keyBack, (uint8_t)kBoard.keyOk};
    for (int i = 0; i < 4; ++i) {
        m_btns[i] = Button{pins[i], false, true, 0, 0, false};
        pinMode(pins[i], INPUT_PULLUP);
    }
    // 编码器按钮并入 Ok 键扫描（不接也无妨，悬空为高）
    if (kBoard.encSw >= 0) {
        pinMode(kBoard.encSw, INPUT_PULLUP);
        m_btns[3].pin = (uint8_t)kBoard.encSw;
    }

    // ---- 编码器：两相都启用内部上拉与 CHANGE 中断 -------------------------
    pinMode(kBoard.encA, INPUT_PULLUP);
    pinMode(kBoard.encB, INPUT_PULLUP);
    s_encPrevAb = readEncoderAb();
    s_encQuarter = 0;
    s_encDetents = 0;
    s_encLastIssued = 0;
    attachInterrupt(digitalPinToInterrupt(kBoard.encA), encoderIsr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(kBoard.encB), encoderIsr, CHANGE);
}

// ---------------------------------------------------------------------------
void Input::pushEvent(InputEvent e)
{
    if (m_qCount >= QUEUE_SIZE) {           // 队列满：丢最旧，保留最新输入
        m_qHead = (m_qHead + 1) % QUEUE_SIZE;
        --m_qCount;
    }
    m_queue[(m_qHead + m_qCount) % QUEUE_SIZE] = e;
    ++m_qCount;
}

InputEvent Input::getEvent()
{
    if (m_qCount == 0) return InputEvent::None;
    const InputEvent e = m_queue[m_qHead];
    m_qHead = (m_qHead + 1) % QUEUE_SIZE;
    --m_qCount;
    return e;
}

int32_t Input::encoderRawCount() const
{
    noInterrupts();
    const int32_t c = s_encDetents;
    interrupts();
    return c;
}

// ---------------------------------------------------------------------------
void Input::scanButton(Button& b, InputEvent ev)
{
    const bool raw = (digitalRead(b.pin) == LOW);          // 按下为低
    const uint32_t now = millis();

    if (raw != b.lastRaw) {          // 原始电平跳变：重置消抖计时
        b.lastRaw = raw;
        b.lastChangeMs = now;
    } else if ((now - b.lastChangeMs) >= BTN_DEBOUNCE_MS && raw != b.stable) {
        b.stable = raw;              // 电平稳定且与确认状态不同 -> 状态切换
        if (raw) {                   // 按下沿：立即发事件
            pushEvent(ev);
            b.pressedMs = now;
            b.repeatSent = false;
        }
    } else if (b.stable && (now - b.pressedMs) >=
               (b.repeatSent ? BTN_REPEAT_MS : BTN_REPEAT_FIRST_MS)) {
        pushEvent(ev);               // 按住连发
        b.pressedMs = now;           // 连发间隔计时重启
        b.repeatSent = true;
    }
}

// ---------------------------------------------------------------------------
void Input::poll()
{
    static const InputEvent map[4] = {
        InputEvent::Up, InputEvent::Down, InputEvent::Back, InputEvent::Ok};
    for (int i = 0; i < 4; ++i)
        scanButton(m_btns[i], map[i]);

    // ---- ISR 已折算成 detent；这里只把累计差转换为 UI 事件 ----------------
    noInterrupts();
    const int32_t cnt = s_encDetents;
    interrupts();
    const int32_t delta = cnt - s_encLastIssued;
    if (delta != 0) {
        s_encLastIssued = cnt;
        const InputEvent ev = (delta > 0) ? InputEvent::EncInc : InputEvent::EncDec;
        for (int32_t i = 0; i < (delta > 0 ? delta : -delta); ++i)
            pushEvent(ev);
    }
}
