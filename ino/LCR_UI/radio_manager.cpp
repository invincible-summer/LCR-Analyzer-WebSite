// ============================================================================
// radio_manager.cpp —— BLE GATT v1 服务 + 分片发送（Arduino-ESP32 3.x BLE）
// ----------------------------------------------------------------------------
// 实现 notes：
//   * 使用核心自带 BLE wrapper（NimBLE 后端）。GATT 协议与 RadioManager
//     接口固定；若 wrapper 吞吐/内存不稳，切 ESP-IDF/NimBLE 时只改本文件
//     （plan.md §6.3）。
//   * 通知负载 = min(negotiated ATT payload, 128) − 帧头；MTU 未协商时用
//     保守值 20（ATT）→ 12 字节 CSV/帧。onMtuChanged 后自动升级。
//   * v1 无复杂重传：seq gap / 断线 / CRC 错误由浏览器发 RESTART_TRANSFER，
//     整份 sealed dataset 从 seq 0 重发（数据不可变，重发必然一致）。
//   * BLE 回调与 loop 并发：回调只置命令字节/状态标志，重活全在 poll()。
// ============================================================================

#include "radio_manager.h"

#include "ble_protocol.h"
#include "radio_lock.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEService.h>

#include <string.h>

const char* const kLcrBleServiceUuid  = "6e6f0001-5f31-4c43-a001-6c63722d7631";
const char* const kLcrBleControlUuid  = "6e6f0002-5f31-4c43-a001-6c63722d7631";
const char* const kLcrBleStatusUuid   = "6e6f0003-5f31-4c43-a001-6c63722d7631";
const char* const kLcrBleMetadataUuid = "6e6f0004-5f31-4c43-a001-6c63722d7631";
const char* const kLcrBleDataUuid     = "6e6f0005-5f31-4c43-a001-6c63722d7631";

RadioManager radio;

// ---------------------------------------------------------------------------
// GATT 对象（单服务）
// ---------------------------------------------------------------------------
static BLEServer* s_server = nullptr;
static BLECharacteristic* s_chControl = nullptr;
static BLECharacteristic* s_chStatus = nullptr;
static BLECharacteristic* s_chMetadata = nullptr;
static BLECharacteristic* s_chData = nullptr;
static bool s_streamActive = false;
static volatile uint8_t s_pendingCmd = 0;
static volatile bool s_clientConnected = false;

class LcrServerCallbacks : public BLEServerCallbacks {
public:
    void onConnect(BLEServer*) override
    {
        s_clientConnected = true;
        radio.noteClientConnected();          // state: Advertising -> Connected
    }
    void onDisconnect(BLEServer*) override
    {
        s_clientConnected = false;
        radio.noteClientDisconnected();       // 停流 + 重新 advertising
    }
#if defined(CONFIG_NIMBLE_ENABLED)
    void onMtuChanged(BLEServer*, ble_gap_conn_desc*, uint16_t mtu) override
    {
        if (mtu > 23) radio.noteAttPayload((uint16_t)(mtu - 3));
    }
#elif defined(CONFIG_BLUEDROID_ENABLED)
    void onMtuChanged(BLEServer*, esp_ble_gatts_cb_param_t* param) override
    {
        if (param && param->mtu.mtu > 23)
            radio.noteAttPayload((uint16_t)(param->mtu.mtu - 3));
    }
#endif
};

class LcrControlCallbacks : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic* ch) override
    {
        const String v = ch->getValue();
        if (v.length() > 0) s_pendingCmd = (uint8_t)v[0];
    }
};

static LcrServerCallbacks s_srvCbs;
static LcrControlCallbacks s_ctlCbs;

// ---------------------------------------------------------------------------
const char* radioStateText(RadioState s)
{
    switch (s) {
    case RadioState::Off:         return "OFF";
    case RadioState::StartingBle: return "STARTING";
    case RadioState::Advertising: return "ADVERTISING";
    case RadioState::Connected:   return "CONNECTED";
    case RadioState::Sending:     return "SENDING";
    case RadioState::StoppingBle: return "STOPPING";
    case RadioState::Error:       return "ERROR";
    }
    return "?";
}

bool RadioManager::transferComplete() const
{
    return m_bytesTotal > 0 && m_bytesSent == m_bytesTotal && m_csvLen > 0;
}

// ---------------------------------------------------------------------------
bool RadioManager::startBleForSealedDataset(const OnePortDataset& d)
{
    if (!d.sealed || d.csvLen == 0) return false;
    char meta[METADATA_JSON_MAX];
    const size_t ml = formatMetadataJson(MeasurementKind::OnePortImpedance,
                                         d.sessionId, d.nPoints, d.csvLen,
                                         d.crc32, d.calibrationState, meta, sizeof(meta));
    if (ml == 0) return false;
    return bleInitCommon(meta, d.csvLen, d.crc32, d.sessionId,
                         (const uint8_t*)d.csv, d.csvLen,
                         MeasurementKind::OnePortImpedance);
}

bool RadioManager::startBleForSealedDataset(const TwoPortDataset& d)
{
    if (!d.sealed || d.csvLen == 0) return false;
    char meta[METADATA_JSON_MAX];
    const size_t ml = formatMetadataJson(MeasurementKind::TwoPortTransfer,
                                         d.sessionId, d.nPoints, d.csvLen,
                                         d.crc32, d.calibrationState, meta, sizeof(meta));
    if (ml == 0) return false;
    return bleInitCommon(meta, d.csvLen, d.crc32, d.sessionId,
                         (const uint8_t*)d.csv, d.csvLen,
                         MeasurementKind::TwoPortTransfer);
}

bool RadioManager::bleInitCommon(const char* metadataJson, uint32_t byteCount,
                                 uint32_t crc32, uint32_t sessionId,
                                 const uint8_t* csv, uint32_t csvLen,
                                 MeasurementKind kind)
{
    (void)crc32;
    // 强 invariant：测量进行中绝不开射频（正常路径 + debug 断言双保险）
    if (!radioLockInvariantOk()) return false;

    if (m_state != RadioState::Off) stopBle();
    m_state = RadioState::StartingBle;
    radioLockNotifyRadioActive(true);

    BLEDevice::init("LCR-Analyzer");
    s_server = BLEDevice::createServer();
    s_server->setCallbacks(&s_srvCbs);

    BLEService* svc = s_server->createService(kLcrBleServiceUuid);
    s_chControl = svc->createCharacteristic(
        kLcrBleControlUuid, BLECharacteristic::PROPERTY_WRITE);
    s_chControl->setCallbacks(&s_ctlCbs);

    s_chStatus = svc->createCharacteristic(
        kLcrBleStatusUuid,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    // NimBLE 后端在开启 notify 属性时自动添加 CCCD（2902），无需手动添加

    s_chMetadata = svc->createCharacteristic(kLcrBleMetadataUuid,
                                             BLECharacteristic::PROPERTY_READ);
    s_chMetadata->setValue((uint8_t*)metadataJson, (uint16_t)strlen(metadataJson));

    s_chData = svc->createCharacteristic(kLcrBleDataUuid,
                                         BLECharacteristic::PROPERTY_NOTIFY);

    svc->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(kLcrBleServiceUuid);
    adv->setScanResponse(true);
    adv->start();

    m_csv = csv;
    m_csvLen = csvLen;
    m_bytesTotal = byteCount;
    m_bytesSent = 0;
    m_nextSeq = 0;
    m_sessionId = sessionId;
    m_kind = kind;
    m_errorCode = 0;
    s_streamActive = false;
    m_state = RadioState::Advertising;

    uint8_t st[kLcrBleStatusLen];
    lcrBleEncodeStatus(st, sizeof(st), 1 /*Advertising*/, 0, sessionId, 0, byteCount);
    s_chStatus->setValue(st, kLcrBleStatusLen);
    return true;
}

// ---------------------------------------------------------------------------
void RadioManager::stopBle()
{
    if (m_state == RadioState::Off) return;
    m_state = RadioState::StoppingBle;
    s_streamActive = false;
    if (s_server) {
        BLEAdvertising* adv = BLEDevice::getAdvertising();
        if (adv) adv->stop();
    }
    // deinit 释放协议栈与连接（radio-off 的确认点 = 本调用返回）
    BLEDevice::deinit(true);
    s_server = nullptr;
    s_chControl = s_chStatus = s_chMetadata = s_chData = nullptr;
    s_clientConnected = false;
    m_state = RadioState::Off;
    radioLockNotifyRadioActive(false);
}

// ---------------------------------------------------------------------------
void RadioManager::handleCommand(uint8_t cmd)
{
    switch ((BleCommand)cmd) {
    case BleCommand::StartTransfer:
    case BleCommand::RestartTransfer:
        if (!s_clientConnected) { m_errorCode = 3; break; }
        m_bytesSent = 0;
        m_nextSeq = 0;
        m_errorCode = 0;
        s_streamActive = true;
        m_state = RadioState::Sending;
        break;
    case BleCommand::AbortTransfer:
        s_streamActive = false;
        if (m_state == RadioState::Sending) m_state = RadioState::Connected;
        break;
    case BleCommand::GetStatus:
        break;                      // 状态在 sendSomeFrames/命令后统一 notify
    default:
        m_errorCode = 2;            // 未知命令
        break;
    }
    notifyStatus();
}

// poll() 单次上界：最多 4 帧 / 每次调用（12–128 字节 CSV / 帧）
static constexpr int kFramesPerPoll = 4;

void RadioManager::sendSomeFrames()
{
    if (!s_streamActive || !s_clientConnected || !s_chData) return;
    uint8_t frame[8 + 128];
    for (int i = 0; i < kFramesPerPoll; ++i) {
        if (m_bytesSent >= m_bytesTotal) {
            s_streamActive = false;                 // 整份 sealed dataset 发完
            m_state = RadioState::Connected;
            notifyStatus();
            return;
        }
        uint16_t chunk = m_attPayload;
        if ((uint32_t)chunk > m_bytesTotal - m_bytesSent) chunk = (uint16_t)(m_bytesTotal - m_bytesSent);
        const size_t n = lcrBleEncodeFrame(frame, sizeof(frame),
                                           LCR_BLE_PROTOCOL_VERSION,
                                           lcrBleKindByte(m_kind), m_nextSeq,
                                           m_csv + m_bytesSent, chunk);
        if (n == 0) { m_errorCode = 4; s_streamActive = false; return; }
        s_chData->setValue(frame, (uint16_t)n);
        s_chData->notify();
        m_bytesSent += chunk;
        ++m_nextSeq;
    }
}

void RadioManager::notifyStatus()
{
    if (!s_chStatus) return;
    uint8_t stateByte = 0;
    switch (m_state) {
    case RadioState::Off:         stateByte = 0; break;
    case RadioState::StartingBle: stateByte = 1; break;
    case RadioState::Advertising: stateByte = 2; break;
    case RadioState::Connected:   stateByte = 3; break;
    case RadioState::Sending:     stateByte = 4; break;
    case RadioState::StoppingBle: stateByte = 5; break;
    case RadioState::Error:       stateByte = 6; break;
    }
    uint8_t st[kLcrBleStatusLen];
    lcrBleEncodeStatus(st, sizeof(st), stateByte, m_errorCode, m_sessionId,
                       m_bytesSent, m_bytesTotal);
    s_chStatus->setValue(st, kLcrBleStatusLen);
    s_chStatus->notify();
}

void RadioManager::poll()
{
    if (m_state == RadioState::Off) return;
    if (s_pendingCmd) {
        const uint8_t cmd = s_pendingCmd;
        s_pendingCmd = 0;
        handleCommand(cmd);
    }
    if (m_state == RadioState::Sending || s_streamActive) sendSomeFrames();
}

// BLE 回调侧的状态迁移（只做标志/状态位，重活在 poll）
void RadioManager::noteClientConnected()
{
    if (m_state == RadioState::Advertising) {
        m_state = RadioState::Connected;
        BLEAdvertising* adv = BLEDevice::getAdvertising();
        if (adv) adv->stop();          // 一对一连接：连上即停广播
        notifyStatus();
    }
}

void RadioManager::noteClientDisconnected()
{
    s_streamActive = false;            // 中断的流：等待重连 + RESTART_TRANSFER
    m_bytesSent = 0;
    m_nextSeq = 0;
    if (m_state == RadioState::Connected || m_state == RadioState::Sending) {
        m_state = RadioState::Advertising;
        BLEAdvertising* adv = BLEDevice::getAdvertising();
        if (adv) adv->start();         // 重新可连接
    }
}

void RadioManager::noteAttPayload(uint16_t attPayload)
{
    // 帧头 8 字节；单帧 CSV 负载上限 128（RAM/吞吐平衡）
    if (attPayload < 12) return;
    uint16_t p = attPayload - kLcrBleFrameHeaderLen;
    if (p > 128) p = 128;
    m_attPayload = p;
}
