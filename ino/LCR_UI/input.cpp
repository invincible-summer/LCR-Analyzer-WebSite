// ============================================================================
// input.cpp —— 按键消抖/连发 与 编码器正交解码实现
// ----------------------------------------------------------------------------
// * 按键：每圈扫描原始电平，25ms 稳定后确认状态；下降沿产生一次事件；
//   按住不放超过 450ms 后每 110ms 连发一次（游标快速移动）。
// * 编码器：A 相双边沿中断 + 读 B 相判向（格雷码解码天然抑制抖动）；
//   累计计数，poll() 中按 ENC_COUNTS_PER_DETENT 折算成「一格一事件」。
// ============================================================================

#include "input.h"

#include "hw_config.h"

#include <Arduino.h>

Input input;

// 编码器计数（文件内静态：中断与 poll 共享，IRAM_ATTR ISR 可直接访问）
static volatile int32_t s_encCount = 0;
static volatile int32_t s_encLastIssued = 0;

// ---------------------------------------------------------------------------
// 编码器中断：A 相跳变时按 (新A, B) 的格雷码关系判向计数。
// 只挂 A 相双边沿中断：常见 EC11 一格产生 2 个计数（A 相一格一升一降），
// poll() 中按 ENC_COUNTS_PER_DETENT 折算；若手感跳格见 README「编码器校准」。
// ---------------------------------------------------------------------------
static void IRAM_ATTR encoderIsr()
{
    const int a = digitalRead(PIN_ENC_A);
    const int b = digitalRead(PIN_ENC_B);
    // A 的当前沿与 B 的电平同相 -> 一个方向；异相 -> 另一个方向
    if (a == b) s_encCount += 1;
    else        s_encCount -= 1;
}

// ---------------------------------------------------------------------------
void Input::begin()
{
    // ---- 按键：内部上拉，按下接地（低电平有效） ---------------------------
    const uint8_t pins[4] = {PIN_BTN_LEFT, PIN_BTN_RIGHT, PIN_BTN_BACK, PIN_BTN_OK};
    for (int i = 0; i < 4; ++i) {
        m_btns[i] = Button{pins[i], false, true, 0, 0, false};
        pinMode(pins[i], INPUT_PULLUP);
    }

    // ---- 编码器：内部上拉；A 相中断计数 ------------------------------------
    pinMode(PIN_ENC_A, INPUT_PULLUP);
    pinMode(PIN_ENC_B, INPUT_PULLUP);
    if (ENC_SW_AS_OK && PIN_ENC_SW >= 0) {
        pinMode(PIN_ENC_SW, INPUT_PULLUP);   // 编码器按钮并入 Ok 键扫描
        m_btns[3].pin = PIN_ENC_SW;          // 复用 Ok 的 Button 槽位
    }
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encoderIsr, CHANGE);
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
    const int32_t c = s_encCount;
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
        InputEvent::Left, InputEvent::Right, InputEvent::Back, InputEvent::Ok};
    for (int i = 0; i < 4; ++i)
        scanButton(m_btns[i], map[i]);

    // ---- 编码器计数折算成「每格一个事件」 ---------------------------------
    noInterrupts();
    const int32_t cnt = s_encCount;
    interrupts();
    const int32_t delta = cnt - s_encLastIssued;
    const int32_t nDetents = delta / ENC_COUNTS_PER_DETENT;
    if (nDetents != 0) {
        s_encLastIssued += nDetents * ENC_COUNTS_PER_DETENT;
        const InputEvent ev = (nDetents > 0) ? InputEvent::EncInc : InputEvent::EncDec;
        for (int32_t i = 0; i < (nDetents > 0 ? nDetents : -nDetents); ++i)
            pushEvent(ev);
    }
}
