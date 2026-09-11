// ============================================================================
// radio_manager.h —— 射频生命周期管理器（全局唯一 BLE 入口）
// ----------------------------------------------------------------------------
// 强 invariant（plan.md §6.1，radio_lock 承载、debug 断言 + host 单测）：
//   MeasurementEngine.state ∈ {PREPARE..RESULT_READY} => RadioState == Off
//   RadioState ∈ {StartingBle..Sending} => Engine IDLE && ADC released
//                                           && Excitation stopped
//
// 生命周期：只在拿到 sealed dataset 后才 startBleForSealedDataset()；
// 测量开始前必须 stopBle()（disconnect -> deinit -> radio-off confirmed）。
// Wi-Fi 首版不使用：固件不初始化 Wi-Fi，采集期间整个 RF 子系统静默。
// ============================================================================

#pragma once

#include "ble_protocol.h"
#include "dataset.h"

#include <stdint.h>

enum class RadioState : uint8_t {
    Off,
    StartingBle,
    Advertising,
    Connected,
    Sending,
    StoppingBle,
    Error,
};

const char* radioStateText(RadioState s);

// BleTransfer 的 GATT/通知细节在本实现文件内聚合（radio_manager.cpp）
class RadioManager {
public:
    // dataset 必须已 seal；测量活动期调用会被拒绝（返回 false）
    bool startBleForSealedDataset(const OnePortDataset& d);
    bool startBleForSealedDataset(const TwoPortDataset& d);

    void poll();                       // BLE 事件泵：控制命令/分片发送/状态
    void stopBle();                    // disconnect -> deinit -> Off
    RadioState state() const { return m_state; }

    // 传输统计（Status 特征同步显示）
    uint32_t bytesSent() const { return m_bytesSent; }
    uint32_t bytesTotal() const { return m_bytesTotal; }
    bool transferComplete() const;

private:
    bool bleInitCommon(const char* metadataJson, uint32_t byteCount,
                       uint32_t crc32, uint32_t sessionId,
                       const uint8_t* csv, uint32_t csvLen,
                       MeasurementKind kind);
    void handleCommand(uint8_t cmd);
    void sendSomeFrames();
    void notifyStatus();

    // BLE 回调侧状态迁移（定义在 radio_manager.cpp）
public:
    void noteClientConnected();
    void noteClientDisconnected();
    void noteAttPayload(uint16_t attPayload);

private:

    RadioState m_state = RadioState::Off;
    const uint8_t* m_csv = nullptr;
    uint32_t m_csvLen = 0;
    uint32_t m_bytesSent = 0;
    uint32_t m_bytesTotal = 0;
    uint16_t m_nextSeq = 0;
    uint32_t m_sessionId = 0;
    MeasurementKind m_kind = MeasurementKind::OnePortImpedance;
    uint16_t m_attPayload = kLcrBleConservativeAttPayload - kLcrBleFrameHeaderLen;
    volatile bool m_cmdQueue[8] = {false, false, false, false, false, false, false, false};
    uint8_t m_errorCode = 0;
};

extern RadioManager radio;
