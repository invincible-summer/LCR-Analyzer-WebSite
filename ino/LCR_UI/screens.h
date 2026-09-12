// ============================================================================
// screens.h —— 界面框架：Screen 基类 / 屏幕栈 / 三个产品界面 + 隐藏诊断页
// ----------------------------------------------------------------------------
// * Screen：一个功能界面。onEnter 全量重绘；onEvent 处理输入；
//   onTick 在主循环空闲时被周期调用（推进编排状态机、消费测量事件、
//   刷新进度）。
// * v4.1.0 起界面层不再知道任何物理测量细节（ADC/sine fit/校准都已删除）；
//   它只与 ILcrService（lcr_api.h）和 SweepEngine（编排）交互。
//
// 顶层三个用户模式（plan.md §0/§2.4）：
//   1. ComponentScreen  单元件 R/C/L 自动识别与测量（不启动 BLE）
//   2. OnePortScreen    单端口扫频 -> seal -> BLE 上传网站拟合
//   3. TwoPortScreen    双端口扫频（H=Vout/Vin）-> BLE 上传网站画曲线
//   （SigGenScreen 诊断页，不占顶层入口）
// ============================================================================

#pragma once

#include "component_meter.h"
#include "display.h"
#include "input.h"
#include "lcr_api.h"
#include "plot.h"
#include "sweep_engine.h"

// ----------------------------------------------------------------------------
// 界面基类
// ----------------------------------------------------------------------------
class Screen {
public:
    virtual ~Screen() = default;
    virtual void onEnter() = 0;              // 进入界面（含从下层返回）：全量重绘
    virtual void onEvent(InputEvent e) {}    // 输入事件（按键 / 编码器）
    virtual void onTick() {}                 // 主循环空闲周期回调
};

// ----------------------------------------------------------------------------
// 屏幕栈管理器
// ----------------------------------------------------------------------------
class ScreenManager {
public:
    void begin(Screen* root) {              // 初始化并进入根界面（主菜单）
        m_top = 0;
        m_stack[0] = root;
        root->onEnter();
    }
    void push(Screen* s) {                  // 进入子界面（栈满则忽略）
        if (m_top >= MAX_DEPTH - 1) return;
        m_stack[++m_top] = s;
        s->onEnter();
    }
    void pop() {                            // 返回上层界面并重绘
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

// ----------------------------------------------------------------------------
// 主菜单：三个产品模式入口；Up 连按 3 次进入隐藏诊断页（信号发生器）
// ----------------------------------------------------------------------------
class MainMenuScreen : public Screen {
public:
    void onEnter() override;
    void onEvent(InputEvent e) override;
    void onTick() override;

private:
    void drawItem(int i, bool selected);
    int m_sel = 0;                            // 当前选中项 0..2
    uint8_t m_diagArmed = 0;                  // 隐藏页解锁计数（Up 连按）
    uint32_t m_diagFirstMs = 0;
};

// ----------------------------------------------------------------------------
// 模式 1：单元件 R/C/L 自动识别与测量（不启动 BLE）
// 5 个几何频点，每点一个 MeasureAndCalcZ job；结果交给
// summarizeComponent 做 apiType 一致性判型 + 中位数聚合。
// ----------------------------------------------------------------------------
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

    DigitEditor m_f0;                         // 频段起点（默认 100 Hz）
    DigitEditor m_f1;                         // 频段终点（默认 2 kHz）
    int m_field = 0;
    Phase m_phase = Phase::Config;
    double m_plan[5];
    uint8_t m_nPlan = 0;
    uint8_t m_nextIdx = 0;
    uint32_t m_pendingId = 0;                 // 等待中的 job（0=无）
    AppZPoint m_z[5];
    AppCalcResult m_calc[5];
    uint8_t m_nPts = 0;
    bool m_cancelReq = false;
    ComponentEstimate m_est{};
    uint32_t m_errUntilMs = 0;
};

// ----------------------------------------------------------------------------
// 模式 2：单端口扫频 -> seal -> BLE 上传（f,re,im CSV，网站 parseZCsv 拟合）
// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
// 模式 3：双端口扫频（H=Vout/Vin）-> seal -> BLE 上传（网站画 Bode/Nyquist）
// ----------------------------------------------------------------------------
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
    BodePlot m_plot;                          // 完成后的轻量 preview
    Phase m_phase = Phase::Config;
    uint32_t m_errUntilMs = 0;
};

// ----------------------------------------------------------------------------
// 隐藏诊断页：信号发生器（经 lcr_api wrapper 调 DNT set_freq；不进顶层菜单）
// ----------------------------------------------------------------------------
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

    DigitEditor m_freq;                       // 频率 5 位（10~10000 Hz）
    bool m_running = false;                   // 仅 StopTone completion 后才清 false
    bool m_pending = false;                   // 有 SetTone/StopTone completion 未回
    bool m_exitRequested = false;             // Back 后异步等 StopTone，再 pop
    uint32_t m_pendingId = 0;                 // completion 必须 id+kind 同时匹配
    LcrJobKind m_pendingKind = LcrJobKind::ServiceInit;
    double m_actualHz = 0;
    uint32_t m_errUntilMs = 0;
};

// ----------------------------------------------------------------------------
// 全局实例（screen_*.cpp 中定义，LCR_UI.ino 装配）
// ----------------------------------------------------------------------------
extern ScreenManager screens;
extern MainMenuScreen screenMenu;
extern ComponentScreen screenComponent;
extern OnePortScreen screenOnePort;
extern TwoPortScreen screenTwoPort;
extern SigGenScreen screenSigGen;

// 引擎装配（LCR_UI.ino 中定义；screen 只通过引擎/射频管理器工作）
extern SweepEngine sweep;
