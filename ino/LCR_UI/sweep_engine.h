// ============================================================================
// sweep_engine.h —— 扫频编排状态机（host 可编译；驱动 ILcrService 逐块推进）
// ----------------------------------------------------------------------------
// 职责（只做编排，不做物理测量 —— plan.md §7.2/§8）：
//   * 构造 requested 对数频率网格（进度显示用；实际频率输出全部由
//     Worker 内的 lcr_api_sweep_z / lcr_api_sweep_w 执行）；
//   * 把网格切成 2/3 点小块提交（小块之间 UI 可响应、取消有界）；
//   * 处理 event：有效点入 dataset 工作区，失败点入诊断（不伪造 0）；
//   * 取消：当前块完成后不再提交新块 -> 提交 StopTone -> 事件确认后
//     解除 measurement lock -> Cancelled（不封存）；
//   * 收尾：StopTone 完成 + 20ms 静默 -> seal（v2 CSV + CRC32 + metadata）
//     -> TransferReady（此时才允许开 BLE）；
//   * 单口有效点 <4 / 双口 <2 -> Insufficient（不进入 BLE / 网站拟合）。
//
// 它不做：产生波形、ADC、fit、校准、计算阻抗 —— 这些全在 DNT API 内。
// ============================================================================

#pragma once

#include "dataset.h"
#include "lcr_api.h"
#include "measurement_types.h"
#include "radio_lock.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// 2/3 点 chunk 规划（纯函数，host 可测）：
//   普通 chunk 2 点；总点数为奇数时最后一块 3 点。
//   例：N=8 -> [0,1][2,3][4,5][6,7]；N=9 -> [0,1][2,3][4,5][6,7,8]。
// 连续 3 个几何点满足 f_mid = sqrt(f_start*f_end)，因此 DNT 的 3 点
// log sweep 会得到与全局网格完全相同的请求序列。
// ---------------------------------------------------------------------------
struct SweepChunk {
    double fStartHz;   // 全局网格上的首点请求频率
    double fStopHz;    // 末点请求频率
    uint8_t nPts;      // 2 或 3
};

// 返回 chunk 个数；参数非法返回 0。
size_t planSweepChunks(const double* grid, size_t nPoints,
                       SweepChunk* out, size_t cap);

class SweepEngine {
public:
    explicit SweepEngine(ILcrService& svc);

    SweepStatus start(const SweepConfig& cfg);
    void poll(uint32_t nowMs);
    void cancel();                        // 当前块完成后停止（有界取消）

    SweepState state() const { return m_state; }
    size_t completedPoints() const { return m_processed; }   // valid+failed
    size_t totalPoints() const { return m_nPoints; }
    double currentFreqHz() const;         // 当前/下一待测频点（进度显示）
    uint16_t errorCount() const { return m_diag.failedPoints; }

    // seal 之后有效；dataset 不可变，直到下一次 start()
    const OnePortDataset* sealedOnePortDataset() const;
    const TwoPortDataset* sealedTwoPortDataset() const;

private:
    bool submitJob(LcrJobKind kind, double a, double b, uint8_t nPts);
    void handleEvent(const LcrEvent& ev, uint32_t nowMs);
    void beginStop();
    void finishSeal(uint32_t nowMs);
    size_t chunkCount() const;
    void chunkAt(size_t k, SweepChunk& out) const;

    ILcrService& m_svc;
    SweepConfig m_cfg{};
    double m_freqs[SWEEP_MAX_POINTS];
    size_t m_nPoints = 0;
    size_t m_nextChunk = 0;
    uint32_t m_pendingId = 0;             // 等待事件的 job（0=无）
    bool m_cancelReq = false;
    bool m_stopDone = false;
    uint32_t m_quietDeadlineMs = 0;
    uint32_t m_sessionCounter = 0;
    size_t m_processed = 0;
    SweepState m_state = SweepState::Idle;
    AppCalSummary m_cal{};
    DatasetDiag m_diag{};
    OnePortDataset m_data1;               // 单口：工作区 + seal 产物
    TwoPortDataset m_data2;               // 双口：工作区 + seal 产物
};
