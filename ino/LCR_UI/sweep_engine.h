// ============================================================================
// sweep_engine.h —— 扫频引擎（host 可编译；驱动 MeasurementEngine 逐点推进）
// ----------------------------------------------------------------------------
// 流程硬约束（plan.md §5.2）：
//   配置 -> 生成频率表
//    -> BLE/RF 必须为 Off（radio_lock invariant，debug 断言）
//    -> 每个频点：settle -> async capture -> fit -> calibration -> store
//    -> 全部频点结束 -> stop excitation -> stop/deinit ADC/DMA
//    -> quiet guard -> dataset seal -> canonical CSV + CRC32
//    -> TRANSFER_READY（此时才允许初始化 BLE / advertising）
//
// 失败点语义：不伪造为 0，不进入拟合 CSV；错误保留在 DatasetDiag。
// seal 后数据不可变；BLE 传输失败绝不回头修改测量值。
// ============================================================================

#pragma once

#include "dataset.h"
#include "measurement_engine.h"
#include "measurement_types.h"

#include <stdint.h>

class SweepEngine {
public:
    explicit SweepEngine(MeasurementEngine& engine);

    SweepStatus start(const SweepConfig& cfg);
    void poll(uint64_t nowUs);
    void cancel();
    SweepState state() const { return m_state; }
    size_t completedPoints() const { return m_nextIdx; }
    size_t totalPoints() const { return m_nPoints; }
    double currentFreqHz() const;           // 正在测的频点（UI 进度显示）
    uint16_t errorCount() const { return m_diag.failedPoints; }

    // seal 之后有效；dataset 不可变，直到用户开始新测量
    const OnePortDataset* sealedOnePortDataset() const;
    const TwoPortDataset* sealedTwoPortDataset() const;

private:
    void storePointOk(const OnePortPoint& p);
    void storePointOk(const TwoPortPoint& p);
    void storePointFail(double requestedHz, MeasurementStatus st);
    void beginSeal(uint64_t nowUs);
    void finishSeal(uint64_t nowUs);

    MeasurementEngine& m_engine;
    SweepConfig m_cfg{};
    double m_freqs[SWEEP_MAX_POINTS];
    size_t m_nPoints = 0;
    size_t m_nextIdx = 0;
    bool m_pointInFlight = false;
    SweepState m_state = SweepState::Idle;
    bool m_cancelReq = false;
    uint64_t m_quietDeadlineUs = 0;
    uint32_t m_sessionCounter = 0;

    // 结果暂存（seal 时拷入 dataset）
    OnePortPoint m_pts1[SWEEP_MAX_POINTS];
    TwoPortPoint m_pts2[SWEEP_MAX_POINTS];
    size_t m_valid = 0;
    DatasetDiag m_diag{};
    OnePortDataset m_data1;
    TwoPortDataset m_data2;
};

const char* sweepStatusText(SweepStatus s);
