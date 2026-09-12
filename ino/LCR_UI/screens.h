// ============================================================================
// screens.h —— 界面框架：Screen 基类 / 屏幕栈 / 用户入口
// ----------------------------------------------------------------------------
// * Screen：onEnter 全量重绘；onEvent 处理输入；onTick 推进非阻塞状态机。
// * 界面层不直接触碰 ADC/激励/校准，只经 ILcrService/SweepEngine。
// * 128x160 ST7735S portrait 是产品 UI 坐标系。
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

class MainMenuScreen : public Screen {
public:
    void onEnter() override;
    void onEvent(InputEvent e) override;
    void onTick() override;
private:
    void drawItem(int i, bool selected);
    int m_sel = 0;
};

// 模式 1：未知单元件识别。5 个频点用于判型；结果页显示一个有明确 fAct 的
// 代表测点及串/并联等效参数。UNKNOWN 仍保留中位频率单点详情。
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
    bool m_cancelReq = false;
    ComponentEstimate m_est{};
    uint32_t m_errUntilMs = 0;
};

// 模式 2：用户输入一个频率，只做一次 MeasureAndCalcZ。运行期完全异步；
// 测量结束后仍等待 StopTone completion 才解除 measurement lock。
class SinglePointScreen : public Screen {
public:
    void onEnter() override;
    void onEvent(InputEvent e) override;
    void onTick() override;
private:
    enum class Phase { Config, Measuring, Stopping, Result };
    bool startMeasure();
    bool submitStop();
    void pumpEvents();
    void drawConfig();
    void drawRun(const char* text);
    void drawResult();

    DigitEditor m_freq;
    Phase m_phase = Phase::Config;
    uint32_t m_pendingId = 0;
    bool m_cancelReq = false;
    double m_requestedHz = 1000.0;
    AppZPoint m_z{};
    AppCalcResult m_calc{};
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
extern SinglePointScreen screenSinglePoint;
extern OnePortScreen screenOnePort;
extern TwoPortScreen screenTwoPort;
extern SigGenScreen screenSigGen;
extern SweepEngine sweep;
