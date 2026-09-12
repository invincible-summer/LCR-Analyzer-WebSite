// ============================================================================
// screens.h —— 界面框架：Screen 基类 / 屏幕栈 / 四个用户入口
// ----------------------------------------------------------------------------
// * Screen：onEnter 全量重绘；onEvent 处理输入；onTick 推进非阻塞状态机。
// * 界面层不直接知道 ADC/激励/校准细节，只经 ILcrService/SweepEngine 工作。
// * 128x160 ST7735S portrait 是产品 UI 坐标系；每个 screen 用 tft.width()/
//   tft.height() 做边界，不再按 160x128 横屏硬编码。
// ============================================================================

#pragma once

#include "component_meter.h"
#include "display.h"
#include "input.h"
#include "lcr_api.h"
#include "plot.h"
#include "sweep_engine.h"

class Screen {
public:
    virtual ~Screen() = default;
    virtual void onEnter() = 0;
    virtual void onEvent(InputEvent e) {}
    virtual void onTick() {}
};

class ScreenManager {
public:
    void begin(Screen* root) {
        m_top = 0;
        m_stack[0] = root;
        root->onEnter();
    }
    void push(Screen* s) {
        if (m_top >= MAX_DEPTH - 1) return;
        m_stack[++m_top] = s;
        s->onEnter();
    }
    void pop() {
        if (m_top <= 0) return;
        --m_top;
        m_stack[m_top]->onEnter();
    }
    void handle(InputEvent e) { m_stack[m_top]->onEvent(e); }
    void tick() { m_stack[m_top]->onTick(); }
    Screen* current() const { return m_stack[m_top]; }

private:
    static constexpr int MAX_DEPTH = 4;
    Screen* m_stack[MAX_DEPTH] = {nullptr};
    int m_top = -1;
};

// 主菜单：Component / One-Port / Two-Port / Signal Generator 全部正常可见。
class MainMenuScreen : public Screen {
public:
    void onEnter() override;
    void onEvent(InputEvent e) override;
    void onTick() override;

private:
    void drawItem(int i, bool selected);
    int m_sel = 0;
};

class ComponentScreen : public Screen {
public:
    void onEnter() override;
    void onEvent(InputEvent e) override;
    void onTick() override;

private:
    enum class Phase { Config, Run, Result };
    bool startMeasure();
    bool submitNext();
    void pumpEvents();
    void finishRun();
    void drawConfig();
    void drawRun();
    void updateRunProgress(bool stopping);
    void drawResult();

    DigitEditor m_f0;
    DigitEditor m_f1;
    int m_field = 0;
    Phase m_phase = Phase::Config;
    double m_plan[5];
    uint8_t m_nPlan = 0;
    uint8_t m_nextIdx = 0;
    uint32_t m_pendingId = 0;
    AppZPoint m_z[5];
    AppCalcResult m_calc[5];
    uint8_t m_nPts = 0;
    bool m_cancelReq = false;
    ComponentEstimate m_est{};
    uint32_t m_errUntilMs = 0;
};

class OnePortScreen : public Screen {
public:
    void onEnter() override;
    void onEvent(InputEvent e) override;
    void onTick() override;

private:
    enum class Phase { Config, Run, Ready, Ble };
    bool startSweep();
    void drawConfig();
    void drawRun();
    void updateRun(bool stopping);
    void drawReady();
    void drawBle();

    DigitEditor m_f0, m_f1, m_ppd;
    int m_field = 0;
    Phase m_phase = Phase::Config;
    uint32_t m_errUntilMs = 0;
};

class TwoPortScreen : public Screen {
public:
    void onEnter() override;
    void onEvent(InputEvent e) override;
    void onTick() override;

private:
    enum class Phase { Config, Run, Ready, Ble };
    bool startSweep();
    void drawConfig();
    void drawRun();
    void updateRun(bool stopping);
    void drawReady();
    void drawBle();
    void drawPreview();

    DigitEditor m_f0, m_f1, m_ppd;
    int m_field = 0;
    BodePlot m_plot;
    Phase m_phase = Phase::Config;
    uint32_t m_errUntilMs = 0;
};

// Signal Generator 仍使用异步 SetTone/StopTone completion 状态机；只把入口从
// 隐藏手势改为主菜单第 4 项，硬件安全/非阻塞语义不变。
class SigGenScreen : public Screen {
public:
    void onEnter() override;
    void onEvent(InputEvent e) override;
    void onTick() override;

private:
    void startOutput();
    void stopOutput();
    void drawStatic();
    void drawFreq();
    void drawStatus();
    void pumpEvents();

    DigitEditor m_freq;
    bool m_running = false;
    bool m_pending = false;
    bool m_exitRequested = false;
    uint32_t m_pendingId = 0;
    LcrJobKind m_pendingKind = LcrJobKind::ServiceInit;
    double m_actualHz = 0;
    uint32_t m_errUntilMs = 0;
};

extern ScreenManager screens;
extern MainMenuScreen screenMenu;
extern ComponentScreen screenComponent;
extern OnePortScreen screenOnePort;
extern TwoPortScreen screenTwoPort;
extern SigGenScreen screenSigGen;

extern SweepEngine sweep;
