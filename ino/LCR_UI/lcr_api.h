// ============================================================================
// lcr_api.h —— 应用层唯一测量接口（UI / 菜单 / 扫频编排 / Dataset 看到的全部）
// ----------------------------------------------------------------------------
// 物理测量核心位于 DO_NOT_TOUCH_lcr_api.h。应用层不直接 include DNT；唯一
// include 点是 lcr_api.cpp。初始化与全部测量必须在同一 FreeRTOS Worker
// task 内，满足 DNT ADC ISR 的 task-affinity 约束。
//
// GPIO4 诊断开关同样不能绕过这一边界：setup 只读取 GPIO4，并把布尔状态
// 传给 lcrServiceBegin(diagnosticsEnabled)。Worker 在 lcr_api_init() 之前调用
// lcr_api_set_diagnostics(state)，因此开关既控制初始化日志也控制后续测量日志。
// ============================================================================

#pragma once

#include <stddef.h>
#include <stdint.h>

enum class AppLcrStatus : int {
    Ok = 0,
    Busy = 1,
    NotReady = 2,
    QueueFull = 3,
    Cancelled = 4,
    BackendError = 5,
};

struct AppZPoint {
    double fReq;
    double fAct;
    double reOhm;
    double imOhm;
    double magOhm;
    double phaseDeg;
    double D;
    double Q;
    char apiType;      // R/C/L; E=failed
    int apiStatus;
};

struct AppWPoint {
    double fReq;
    double fAct;
    double hMag;
    double hDb;
    double phaseDeg;
    double reH;
    double imH;
    int apiStatus;
};

struct AppCalcResult {
    char type;
    double rs, cs, ls;
    double rp, cp, lp;
    double D, Q;
    int apiStatus;
};

struct AppCalSummary {
    uint8_t rangesValid;
    bool openValid;
    bool shortValid;
};

enum class LcrJobKind : uint8_t {
    ServiceInit = 0,
    SweepZChunk,
    SweepWChunk,
    MeasureAndCalcZ,
    SetTone,
    StopTone,
    Reset,
    SelfCheck,
    ReadCalibrationStatus,
};

struct LcrJob {
    uint32_t id;
    LcrJobKind kind;
    double fStartHz;
    double fStopHz;
    uint8_t pointCount;
    double frequencyHz;
};

struct LcrEvent {
    uint32_t id;
    LcrJobKind kind;
    int backendStatus;
    uint8_t pointCount;
    AppZPoint z[3];
    AppWPoint w[3];
    AppCalcResult calc;
    AppCalSummary cal;
    double actualHz;
};

class ILcrService {
public:
    virtual ~ILcrService() = default;
    virtual bool submit(LcrJob& job) = 0;
    virtual bool takeEvent(LcrEvent& ev) = 0;
    virtual bool busy() const = 0;
    virtual void requestCancel() = 0;
};

// 创建 Worker。diagnosticsEnabled 必须在创建时给出；Worker 启动后不再改变
// 该 boot latch。默认 false 保持 host/旧调用者的静默语义。
bool lcrServiceBegin(bool diagnosticsEnabled = false);
bool lcrServiceReady();
int lcrServiceInitError();
ILcrService& lcrService();
bool lcrServiceSubmit(LcrJob& job);
bool lcrServiceTakeEvent(LcrEvent& ev);
bool lcrServiceBusy();
void lcrServiceRequestCancel();
