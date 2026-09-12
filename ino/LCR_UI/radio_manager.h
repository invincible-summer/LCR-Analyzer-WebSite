// ============================================================================
// radio_manager.h —— 射频生命周期管理器（全局唯一 BLE 入口）
// ----------------------------------------------------------------------------
// 强 invariant：测量活动期间 RadioState 必须是 Off；sealed dataset 后才允许
// 启动 BLE。stopBle() 使用 BLEDevice::deinit(false)：关闭 host/controller，
// 但绝不释放 BT controller memory，因为 Arduino-ESP32 明确规定
// deinit(true) 会“prevents reinitialization”，与第二次上传需求相冲突。
// BLE 回调只记录轻量事件；GATT/advertising/notify 状态迁移都在 poll() 中。
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

class RadioManager {
public:
    // dataset 必须已 seal；测量活动期调用会被拒绝（返回 false）
    bool startBleForSealedDataset(const OnePortDataset& d);
    bool startBleForSealedDataset(const TwoPortDataset& d);

    void poll();                       // 唯一 BLE event/tx pump（主 loop 调一次）
    void stopBle();                    // stop adv/stream -> deinit(false) -> Off
    RadioState state() const { return m_state; }

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

public:
    // 只由 poll() 根据 callback mailbox 调用；函数本身不在 BLE callback 栈执行。
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
    // CSV data bytes per notification (ATT payload minus 8-byte LCR frame header).
    uint16_t m_attPayload = kLcrBleConservativeAttPayload - kLcrBleFrameHeaderLen;
    uint32_t m_nextTxMs = 0;           // notification pacing deadline
    uint8_t m_errorCode = 0;
};

extern RadioManager radio;
