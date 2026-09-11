// ============================================================================
// measurement_engine.h —— 非阻塞单点测量状态机（host 可编译，依赖注入）
// ----------------------------------------------------------------------------
// 状态机（plan.md §3.2，全部状态显式可观测）：
//   IDLE -> PREPARE -> EXCITATION_START -> SETTLING -> CAPTURE_ARM
//        -> CAPTURING -> FITTING -> APPLY_CALIBRATION -> RESULT_READY -> IDLE
//   任何状态 -> CANCELLING -> SAFE_OFF -> IDLE
//   任何硬件错误 -> ERROR_SAFE_OFF -> IDLE
//
// 约束：
//   * start() 只做参数校验/资源申请；
//   * poll() 每次执行有明确上界（绝不一次等待整个频点）；
//   * SETTLING 用周期截止点推进，不使用 delay()；
//   * CAPTURING 由 DMA/缓冲推进，loop() 始终能轮询按键；
//   * Back/Cancel 到 SAFE_OFF 的目标延迟 <100ms（下一次 poll 停激励停采集）；
//   * 每次 ERROR/CANCEL 走同一 safeOff()：停激励、停/释放 ADC、前端安全态。
//
// 通过纯虚接口与硬件解耦（Arduino 构建注入真实驱动，host 单测注入 mock）。
// ============================================================================

#pragma once

#include "calibration.h"
#include "measurement_types.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// 硬件抽象（引擎只认接口，不触碰 GPIO/外设寄存器）
// ---------------------------------------------------------------------------
class IExcitationSource {
public:
    virtual ~IExcitationSource() = default;
    virtual ExcitationStatus excitationBegin(const ExcitationConfig& cfg) = 0;
    virtual ExcitationState excitationState() const = 0;
    virtual void excitationStop() = 0;
};

// 前端使能/量程（独立于激励；safe-off 时必须落到安全态）
class IFrontEnd {
public:
    virtual ~IFrontEnd() = default;
    virtual void frontEndEnable(uint8_t rangeBits) = 0;
    virtual void frontEndDisable() = 0;
};

class ICaptureDevice {
public:
    virtual ~ICaptureDevice() = default;
    virtual CaptureStatus captureStart(const CaptureRequest& req) = 0;
    virtual void capturePoll() = 0;                 // 驱动 DMA 读取（非 ISR 重活）
    virtual bool captureDone() const = 0;
    virtual CaptureStatus captureStatus() const = 0;
    virtual TimedSamples captureChannel(CaptureChannel ch) = 0;  // done 后有效
    virtual uint32_t captureMaxPatternRateHz() const = 0;        // S3: 83333
    virtual uint16_t captureFullScaleMv() const = 0;             // 削顶判据
    virtual void captureCancel() = 0;
    virtual void captureRelease() = 0;
};

// ---------------------------------------------------------------------------
// 引擎状态
// ---------------------------------------------------------------------------
enum class MeasurementEngineState : uint8_t {
    Idle,
    Prepare,
    ExcitationStart,
    Settling,
    CaptureArm,
    Capturing,
    Fitting,
    ApplyCalibration,
    ResultReady,
    Cancelling,
    SafeOff,          // 正常/取消后的统一安全化（也用于 RESULT_READY 收尾）
    ErrorSafeOff,
};

const char* engineStateText(MeasurementEngineState s);

// ---------------------------------------------------------------------------
// 质量门限（编译期常量；实板标定后可按 §9.5 规则收紧/记录，不悄悄放宽）
// ---------------------------------------------------------------------------
struct QualityGates {
    double minAmplitudeMv;      // 信号过小判据
    double freqRelTol;          // |actual-requested|/requested 上限
    double residOverAmpMax;     // 残差 RMS / 幅度上限（超过视为拟合不可信）
    uint32_t maxSamplesPerChannel;
    uint64_t maxSettleUs;       // 建立期上限（防低频死等）
};

const QualityGates& defaultQualityGates();

// ---------------------------------------------------------------------------
// MeasurementEngine
// ---------------------------------------------------------------------------
class MeasurementEngine {
public:
    MeasurementEngine(IExcitationSource& exc, ICaptureDevice& cap,
                      IFrontEnd* frontEnd, const CalibrationProfile& cal,
                      double currentSenseOhm, double transimpedanceGain);

    MeasurementStatus start(const MeasurementRequest& request);
    void poll(uint64_t nowUs);
    MeasurementEngineState state() const { return m_state; }
    float progress() const;                 // 0..1（状态推进近似）
    bool resultReady() const { return m_state == MeasurementEngineState::ResultReady; }
    bool active() const;                    // Prepare..ResultReady
    MeasurementStatus lastStatus() const { return m_lastStatus; }

    bool takeResult(OnePortPoint& out);
    bool takeResult(TwoPortPoint& out);
    void cancel();                          // 用户动作 → 下一次 poll 进入安全化

    // safeOff 落点（供 SweepEngine 复用的统一安全语义）
    void safeOff();

private:
    void enterError(MeasurementStatus st, uint64_t nowUs);
    bool fitAndBuildResult(uint64_t nowUs);
    void requestCapture();

    IExcitationSource& m_exc;
    ICaptureDevice& m_cap;
    IFrontEnd* m_front;                     // 可为 nullptr（诊断路径）
    const CalibrationProfile& m_cal;
    double m_rSenseOhm;
    double m_tiaGain;

    MeasurementEngineState m_state = MeasurementEngineState::Idle;
    MeasurementRequest m_req{};
    MeasurementStatus m_lastStatus = MeasurementStatus::Ok;
    ExcitationState m_excState{};
    uint64_t m_settleDeadlineUs = 0;
    uint64_t m_captureStartUs = 0;
    uint32_t m_targetPerCh = 0;
    uint32_t m_patternRateHz = 0;
    uint32_t m_skewNs = 0;                  // 通道 skew（ns）
    OnePortPoint m_result1{};
    TwoPortPoint m_result2{};
    bool m_haveResult = false;
};
