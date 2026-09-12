// ============================================================================
// radio_manager.cpp —— BLE GATT v1 服务 + 稳健分片发送（Arduino-ESP32 3.3.11）
// ----------------------------------------------------------------------------
// 关键约束：
//   * BLE callback 只写 mailbox 标志，不做 advertising/notify/deinit 等重活；
//   * stopBle 使用 BLEDevice::deinit(false)，禁止 deinit(true)。Arduino-ESP32
//     3.3.11 的 BLEDevice API 明确说明 release_memory=true 会阻止再次初始化；
//   * 每次新连接/session 都把 ATT 数据负载复位到 MTU=23 的保守值，只有收到
//     本连接 onMtuChanged 后才扩大，避免第二次连接沿用上次 MTU；
//   * 主 loop 是唯一 poll() owner；单次 pump 最多发 1 帧，并按 8 ms pacing，
//     防止连续 notify 淹没 NimBLE host/controller queue。
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
// GATT 对象（每次 BLE init 创建；deinit(false) 后全部作废并置空）
// ---------------------------------------------------------------------------
static BLEServer* s_server = nullptr;
static BLECharacteristic* s_chControl = nullptr;
static BLECharacteristic* s_chStatus = nullptr;
static BLECharacteristic* s_chMetadata = nullptr;
static BLECharacteristic* s_chData = nullptr;
static bool s_streamActive = false;
static bool s_clientConnected = false;

// BLE callback -> main-loop mailbox。单连接/单控制命令协议，因此 latest-value
// mailbox 足够；所有 GATT 状态迁移都延后到 RadioManager::poll()。
static volatile uint8_t s_pendingCmd = 0;
static volatile bool s_connectEvent = false;
static volatile bool s_disconnectEvent = false;
static volatile uint16_t s_mtuAttPayloadEvent = 0;

class LcrServerCallbacks : public BLEServerCallbacks {
public:
    void onConnect(BLEServer*) override
    {
        s_connectEvent = true;
    }
    void onDisconnect(BLEServer*) override
    {
        s_disconnectEvent = true;
    }
#if defined(CONFIG_NIMBLE_ENABLED)
    void onMtuChanged(BLEServer*, ble_gap_conn_desc*, uint16_t mtu) override
    {
        if (mtu > 23) s_mtuAttPayloadEvent = (uint16_t)(mtu - 3);
    }
#elif defined(CONFIG_BLUEDROID_ENABLED)
    void onMtuChanged(BLEServer*, esp_ble_gatts_cb_param_t* param) override
    {
        if (param && param->mtu.mtu > 23)
            s_mtuAttPayloadEvent = (uint16_t)(param->mtu.mtu - 3);
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

static constexpr uint16_t kConservativeDataPayload =
    kLcrBleConservativeAttPayload - kLcrBleFrameHeaderLen;
static constexpr uint32_t kTxIntervalMs = 8;

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
    if (!radioLockInvariantOk()) return false;

    if (m_state != RadioState::Off) stopBle();
    if (m_state != RadioState::Off) return false;

    // 新 session 必须清理上次连接留下的协商状态/mailbox。
    m_attPayload = kConservativeDataPayload;
    m_nextTxMs = 0;
    s_pendingCmd = 0;
    s_connectEvent = false;
    s_disconnectEvent = false;
    s_mtuAttPayloadEvent = 0;
    s_clientConnected = false;
    s_streamActive = false;

    m_state = RadioState::StartingBle;
    radioLockNotifyRadioActive(true);

    if (!BLEDevice::init("LCR-Analyzer")) {
        m_state = RadioState::Error;
        radioLockNotifyRadioActive(false);
        return false;
    }
    s_server = BLEDevice::createServer();
    if (!s_server) {
        BLEDevice::deinit(false);
        m_state = RadioState::Error;
        radioLockNotifyRadioActive(false);
        return false;
    }
    s_server->setCallbacks(&s_srvCbs);

    BLEService* svc = s_server->createService(kLcrBleServiceUuid);
    if (!svc) {
        BLEDevice::deinit(false);
        s_server = nullptr;
        m_state = RadioState::Error;
        radioLockNotifyRadioActive(false);
        return false;
    }

    s_chControl = svc->createCharacteristic(
        kLcrBleControlUuid, BLECharacteristic::PROPERTY_WRITE);
    s_chStatus = svc->createCharacteristic(
        kLcrBleStatusUuid,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    s_chMetadata = svc->createCharacteristic(kLcrBleMetadataUuid,
                                             BLECharacteristic::PROPERTY_READ);
    s_chData = svc->createCharacteristic(kLcrBleDataUuid,
                                         BLECharacteristic::PROPERTY_NOTIFY);
    if (!s_chControl || !s_chStatus || !s_chMetadata || !s_chData) {
        BLEDevice::deinit(false);
        s_server = nullptr;
        s_chControl = s_chStatus = s_chMetadata = s_chData = nullptr;
        m_state = RadioState::Error;
        radioLockNotifyRadioActive(false);
        return false;
    }
    s_chControl->setCallbacks(&s_ctlCbs);
    s_chMetadata->setValue((uint8_t*)metadataJson, (uint16_t)strlen(metadataJson));
    svc->start();

    m_csv = csv;
    m_csvLen = csvLen;
    m_bytesTotal = byteCount;
    m_bytesSent = 0;
    m_nextSeq = 0;
    m_sessionId = sessionId;
    m_kind = kind;
    m_errorCode = 0;

    uint8_t st[kLcrBleStatusLen];
    lcrBleEncodeStatus(st, sizeof(st), 1 /*Starting*/, 0, sessionId, 0, byteCount);
    s_chStatus->setValue(st, kLcrBleStatusLen);

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    if (!adv) {
        stopBle();
        return false;
    }
    adv->addServiceUUID(kLcrBleServiceUuid);
    adv->setScanResponse(true);
    adv->start();
    m_state = RadioState::Advertising;

    Serial.printf("# BLE session start id=%lu bytes=%lu heap=%lu psram=%lu\n",
                  (unsigned long)m_sessionId, (unsigned long)m_bytesTotal,
                  (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getFreePsram());
    return true;
}

// ---------------------------------------------------------------------------
void RadioManager::stopBle()
{
    if (m_state == RadioState::Off) return;
    m_state = RadioState::StoppingBle;
    s_streamActive = false;

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    if (adv) adv->stop();

    // release_memory=false is intentional and required. Arduino-ESP32 documents
    // release_memory=true as preventing reinitialization; using true caused the
    // deterministic second-upload StoreProhibited lifecycle failure.
    BLEDevice::deinit(false);

    s_server = nullptr;
    s_chControl = s_chStatus = s_chMetadata = s_chData = nullptr;
    s_clientConnected = false;
    s_pendingCmd = 0;
    s_connectEvent = false;
    s_disconnectEvent = false;
    s_mtuAttPayloadEvent = 0;
    m_attPayload = kConservativeDataPayload;
    m_nextTxMs = 0;
    m_state = RadioState::Off;
    radioLockNotifyRadioActive(false);

    Serial.printf("# BLE stopped heap=%lu psram=%lu\n",
                  (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getFreePsram());
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
        m_nextTxMs = millis();
        s_streamActive = true;
        m_state = RadioState::Sending;
        break;
    case BleCommand::AbortTransfer:
        s_streamActive = false;
        if (m_state == RadioState::Sending) m_state = RadioState::Connected;
        break;
    case BleCommand::GetStatus:
        break;
    default:
        m_errorCode = 2;
        break;
    }
    notifyStatus();
}

void RadioManager::sendSomeFrames()
{
    if (!s_streamActive || !s_clientConnected || !s_chData) return;

    const uint32_t now = millis();
    if ((int32_t)(now - m_nextTxMs) < 0) return;
    m_nextTxMs = now + kTxIntervalMs;

    if (m_bytesSent >= m_bytesTotal) {
        s_streamActive = false;
        m_state = RadioState::Connected;
        notifyStatus();
        return;
    }

    uint8_t frame[kLcrBleFrameHeaderLen + 128];
    uint16_t chunk = m_attPayload;
    if (chunk > 128) chunk = 128;
    if ((uint32_t)chunk > m_bytesTotal - m_bytesSent)
        chunk = (uint16_t)(m_bytesTotal - m_bytesSent);

    const size_t n = lcrBleEncodeFrame(frame, sizeof(frame),
                                       LCR_BLE_PROTOCOL_VERSION,
                                       lcrBleKindByte(m_kind), m_nextSeq,
                                       m_csv + m_bytesSent, chunk);
    if (n == 0) {
        m_errorCode = 4;
        s_streamActive = false;
        m_state = RadioState::Error;
        notifyStatus();
        return;
    }
    s_chData->setValue(frame, n);
    s_chData->notify();
    m_bytesSent += chunk;
    ++m_nextSeq;

    if (m_bytesSent >= m_bytesTotal) {
        s_streamActive = false;
        m_state = RadioState::Connected;
        notifyStatus();
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
    if (s_clientConnected) s_chStatus->notify();
}

void RadioManager::poll()
{
    if (m_state == RadioState::Off) return;

    // Process callback mailbox first. Disconnect wins over connect if both were
    // observed before this tick, because no data may be sent to a stale link.
    if (s_disconnectEvent) {
        s_disconnectEvent = false;
        s_connectEvent = false;
        noteClientDisconnected();
    } else if (s_connectEvent) {
        s_connectEvent = false;
        noteClientConnected();
    }

    const uint16_t att = s_mtuAttPayloadEvent;
    if (att) {
        s_mtuAttPayloadEvent = 0;
        noteAttPayload(att);
    }

    if (s_pendingCmd) {
        const uint8_t cmd = s_pendingCmd;
        s_pendingCmd = 0;
        handleCommand(cmd);
    }
    if (m_state == RadioState::Sending && s_streamActive) sendSomeFrames();
}

void RadioManager::noteClientConnected()
{
    s_clientConnected = true;
    // A fresh connection begins at the BLE default MTU until this connection's
    // onMtuChanged proves otherwise. Never inherit the previous client's MTU.
    m_attPayload = kConservativeDataPayload;
    if (m_state == RadioState::Advertising) {
        m_state = RadioState::Connected;
        BLEAdvertising* adv = BLEDevice::getAdvertising();
        if (adv) adv->stop();
        notifyStatus();
    }
}

void RadioManager::noteClientDisconnected()
{
    s_clientConnected = false;
    s_streamActive = false;
    m_bytesSent = 0;
    m_nextSeq = 0;
    m_attPayload = kConservativeDataPayload;
    s_mtuAttPayloadEvent = 0;

    if (m_state == RadioState::Connected || m_state == RadioState::Sending ||
        m_state == RadioState::Error) {
        m_state = RadioState::Advertising;
        BLEAdvertising* adv = BLEDevice::getAdvertising();
        if (adv) adv->start();
    }
}

void RadioManager::noteAttPayload(uint16_t attPayload)
{
    // attPayload = negotiated ATT_MTU - 3. Remove our own 8-byte frame header;
    // cap CSV data to 128 bytes for deterministic stack/RAM pressure.
    if (attPayload <= kLcrBleFrameHeaderLen) return;
    uint16_t p = (uint16_t)(attPayload - kLcrBleFrameHeaderLen);
    if (p > 128) p = 128;
    if (p < kConservativeDataPayload) return;
    m_attPayload = p;
}
