// ============================================================================
// screens.h —— 界面框架：Screen 基类 / 屏幕栈 / 四个功能界面声明
// ----------------------------------------------------------------------------
// * Screen：一个功能界面。onEnter 全量重绘；onEvent 处理输入；
//   onTick 在主循环空闲时被周期调用（后台任务、状态刷新）。
// * ScreenManager：屏幕栈。push 进子界面、pop 返回上层（BACK 键的默认语义），
//   所有屏幕为静态实例，不使用动态内存。
//
// 界面总览（任务书三大功能）：
//   MainMenuScreen  主菜单：1 信号发生器 / 2 单频点复阻抗 / 3 幅相频特性
//   SigGenScreen    信号发生器：数字位编辑 + 波形/占空比 + 输出控制
//   MeasureScreen   单频点测量：频率 + 单/双端口复选框 + 结果显示
//   SweepScreen     扫频测量：起止频率/点数/端口 + Bode 曲线 + 游标
// ============================================================================

#pragma once

#include "analysis.h"
#include "display.h"
#include "hw_config.h"
#include "plot.h"

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
// 主菜单：三个功能入口，编码器/左右键选择，OK 进入
// ----------------------------------------------------------------------------
class MainMenuScreen : public Screen {
public:
    void onEnter() override;
    void onEvent(InputEvent e) override;
    void onTick() override;

private:
    void drawItem(int i, bool selected);
    int m_sel = 0;                            // 当前选中项 0..2
};

// ----------------------------------------------------------------------------
// 信号发生器
// ----------------------------------------------------------------------------
class SigGenScreen : public Screen {
public:
    void onEnter() override;
    void onEvent(InputEvent e) override;

private:
    enum Field { F_FREQ = 0, F_WAVE = 1, F_DUTY = 2 };   // 焦点字段
    void startOutput();
    void stopOutput();
    void drawStatic();                        // 静态框架（标签等）
    void drawFreq(); void drawWave(); void drawDuty();
    void drawStatus(); void drawPreview();

    DigitEditor m_freq;                       // 频率 5 位 (10~10000 Hz)
    DigitEditor m_duty;                       // 占空比 3 位 (0~100 %)
    int m_field = F_FREQ;
    int m_wave = Sine;                        // WaveType: Sine/Square/Triangle
    bool m_running = false;                   // 输出中 / 暂停
    double m_actualHz = 0;                    // 实际输出频率（队友函数返回值）
    uint32_t m_errUntilMs = 0;                // 错误提示显示截止时间
};

// ----------------------------------------------------------------------------
// 单频点复阻抗 / 双端口测量
// ----------------------------------------------------------------------------
class MeasureScreen : public Screen {
public:
    void onEnter() override;
    void onEvent(InputEvent e) override;

private:
    enum Phase { PhConfig, PhRun, PhResult };
    enum Field { F_FREQ = 0, F_MODE = 1 };               // 配置页焦点
    void runMeasurement();                    // 采样→幅相→复阻抗/增益（阻塞一次）
    void drawConfig(); void drawResult(); void drawError(const char* msg);
    void drawHeader();                        // 测量频率 / 实际频率

    DigitEditor m_freq;                       // 频率 5 位 (10~10000 Hz)
    Checkbox m_mode;                          // 1-PORT / 2-PORT
    int m_field = F_FREQ;
    Phase m_phase = PhConfig;
    // 最近一次测量结果（绘制用）
    AdcSampleResult m_smp{};
    RatioPhaseResult m_rp{};
    ImpedanceCalcResult m_z{};
    GainPhaseResult m_gp{};
};

// ----------------------------------------------------------------------------
// 幅频/相频特性（扫频）
// ----------------------------------------------------------------------------
class SweepScreen : public Screen {
public:
    void onEnter() override;
    void onEvent(InputEvent e) override;
    void onTick() override;                   // 扫频逐点推进（非阻塞）

private:
    enum Phase { PhConfig, PhRun, PhDone };
    enum Field { F_F0 = 0, F_F1 = 1, F_PPD = 2, F_MODE = 3 };
    bool startSweep();                        // 校验参数、初始化数组；false=参数非法
    void stepOnePoint();                      // 测量并记录一个频点
    void finishSweep();                       // 识别/拟合 + 最终重绘 + 进游标态
    void drawConfig();
    void drawRun();                           // 进度 + 已得曲线（增量）
    void drawDone();                          // 完整坐标轴 + 曲线 + 游标
    void updateReadout();                     // 游标读数行
    int  pointCount() const { return m_nPts; }

    DigitEditor m_f0, m_f1, m_ppd;            // 起始/终止频率、每十倍频点数
    Checkbox m_mode;
    int m_field = F_F0;
    Phase m_phase = PhConfig;

    // 扫频数据（实际频率 + 幅度/相位 + 单端口复阻抗）
    double m_plan[SWEEP_MAX_POINTS];  // 计划频率（对数间隔，Hz）
    double m_f[SWEEP_MAX_POINTS];     // 各点实际频率（队友接口返回）
    double m_mag[SWEEP_MAX_POINTS];   // 1-port: |Z|(Ω)  2-port: 增益(dB)；失败点为 NAN
    double m_ph[SWEEP_MAX_POINTS];    // 相位（度）；失败点为 NAN
    double m_zRe[SWEEP_MAX_POINTS];   // 1-port 复阻抗实部
    double m_zIm[SWEEP_MAX_POINTS];   // 1-port 复阻抗虚部
    int m_nPts = 0;                   // 已完成点数
    int m_nextIdx = 0;                // 下一个待测点下标
    int m_cursor = 0;                 // 游标所在频点索引

    // 绘图与量程（扫频过程中量程只扩不缩，扩大即整体重绘）
    BodePlot m_plot;
    double m_rngMin = 0, m_rngMax = 1;
    bool m_rngLog = false, m_rngValid = false;
    void updateRangesAndDraw(bool finalDraw);

    FilterGuess m_filter{};
    EqCircuitGuess m_eqc{};
    bool m_abort = false;             // 扫频中按 BACK 置位
    uint32_t m_msgUntilMs = 0;        // 临时提示（CSV 已发送等）显示截止时间
    uint32_t m_errUntilMs = 0;        // 配置页错误提示截止时间
};

// ----------------------------------------------------------------------------
// 全局实例（screen_*.cpp 中定义，LCR_UI.ino 装配）
// ----------------------------------------------------------------------------
extern ScreenManager screens;
extern MainMenuScreen screenMenu;
extern SigGenScreen screenSigGen;
extern MeasureScreen screenMeasure;
extern SweepScreen screenSweep;
