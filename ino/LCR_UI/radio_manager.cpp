// ============================================================================
// radio_manager.cpp —— BLE GATT v1 服务 + 稳健分片发送（Arduino-ESP32 3.3.11）
// ----------------------------------------------------------------------------
// 关键约束：
//   * BLE callback 只写 atomic mailbox，不做 advertising/notify/deinit 等重活；
//   * stopBle 使用 BLEDevice::deinit(false)，禁止 deinit(true)。Arduino-ESP32
//     3.3.11 的 BLEDevice API 明确说明 release_memory=true 会阻止再次初始化；
//   * 每次新连接/session 都把 ATT 数据负载复位到 MTU=23 的保守值，只有收到
//     本连接 onMtuChanged 后才扩大，避免第二次连接沿用上次 MTU；
//   * 单个 notification 永远服从 negotiated ATT_MTU-3；CSV 分片上限 128 B，
//     大数据集自动拆成多帧，seq 以 uint16 modulo 2^16 连续递增；
//   * 主 loop 是唯一 poll() owner；单次 pump 最多发 1 帧，并按 15 ms pacing，
//     防止连续 notify 淹没 NimBLE host/controller queue；
//   * Data characteristic 的底层 notify error 通过 atomic mailbox 回到主 loop，
//     立即停止本轮 stream，允许浏览器发 RESTART_TRANSFER 自动重传；
//   * 初始化失败路径必须回到真正的 Off 状态，禁止留下“Error 但 BLE 已 deinit”
//     的伪状态，否则下一次 start/stop 会访问已经失效的 advertising 对象。
// ============================================================================

#include "radio_manager.h"

#include "ble_protocol.h"
#include "radio_lock.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEService.h>

#include <atomic>
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

// BLE callback 与 Arduino loop 运行在不同 FreeRTOS task 上。volatile 不能构成
// C++ 跨线程同步，因此 mailbox 必须用 atomic；callback store，poll exchange。
static std::atomic<uint8_t> s_pendingCmd{0};
static std::atomic<bool> s_connectEvent{false};
static std::atomic<bool> s_disconnectEvent{false};
static std::atomic<uint16_t> s_mtuAttPayloadEvent{0};
static std::atomic<uint8_t> s_notifyErrorEvent{0};

static void clearGattPointers()
{
    s_server = nullptr;
    s_chControl = nullptr;
    s_chStatus = nullptr;
    s_chMetadata = nullptr;
    s_chData = nullptr;
}

class LcrServerCallbacks : public BLEServerCallbacks {
public:
    void onConnect(BLEServer*) override
    {
        s_connectEvent.store(true, std::memory_order_release);
    }
    void onDisconnect(BLEServer*) override
    {
        s_disconnectEvent.store(true, std::memory_order_release);
    }
#if defined(CONFIG_NIMBLE_ENABLED)
    void onMtuChanged(BLEServer*, ble_gap_conn_desc*, uint16_t mtu) override
    {
        if (mtu > 23)
            s_mtuAttPayloadEvent.store((uint16_t)(mtu - 3), std::memory_order_release);
    }
#elif defined(CONFIG_BLUEDROID_ENABLED)
    void onMtuChanged(BLEServer*, esp_ble_gatts_cb_param_t* param) override
    {
        if (param && param->mtu.mtu > 23)
            s_mtuAttPayloadEvent.store((uint16_t)(param->mtu.mtu - 3),
                                       std::memory_order_release);
    }
#endif
};

class LcrControlCallbacks : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic* ch) override
    {
        const String v = ch->getValue();
        if (v.length() > 0)
            s_pendingCmd.store((uint8_t)v[0], std::memory_order_release);
    }
};

class LcrDataCallbacks : public BLECharacteristicCallbacks {
public:
    void onStatus(BLECharacteristic*, Status s, uint32_t code) override
    {
        (void)code;
        if (s == BLECharacteristicCallbacks::SUCCESS_NOTIFY) return;
        // error code 5 = local notify submission/transport failure. 不在 callback
        // 中直接修改 RadioManager 状态，避免 BLE task 与 Arduino loop 重入。
        s_notifyErrorEvent.store(5, std::memory_order_release);
    }
};

static LcrServerCallbacks s_srvCbs;
static LcrControlCallbacks s_ctlCbs;
static LcrDataCallbacks s_dataCbs;

static constexpr uint16_t kConservativeDataPayload =
    kLcrBleConservativeAttPayload - kLcrBleFrameHeaderLen;
// 185 足够容纳 8-byte header + 128-byte CSV frame；若 peer 不协商，仍自动
// 回退到 MTU23。选择 185 而非 517 是为了减少通知数同时控制 host 内存压力。
static constexpr uint16_t kPreferredMtu = 185;
static constexpr uint32_t kTxIntervalMs = 15;

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
    s_pendingCmd.store(0, std::memory_order_release);
    s_connectEvent.store(false, std::memory_order_release);
    s_disconnectEvent.store(false, std::memory_order_release);
    s_mtuAttPayloadEvent.store(0, std::memory_order_release);
    s_notifyErrorEvent.store(0, std::memory_order_release);
    s_clientConnected = false;
    s_streamActive = false;
    clearGattPointers();

    m_state = RadioState::StartingBle;
    radioLockNotifyRadioActive(true);

    if (!BLEDevice::init("LCR-Analyzer")) {
        m_errorCode = 1;
        m_state = RadioState::Off;
        radioLockNotifyRadioActive(false);
        return false;
    }

    // 只设置本机 preferred MTU，不假定 peer 一定接受。真正发送尺寸仍只由
    // 当前连接 onMtuChanged 结果决定；失败时继续使用默认 MTU23。
    const esp_err_t mtuRc = BLEDevice::setMTU(kPreferredMtu);
    if (mtuRc != ESP_OK)
        Serial.printf("# BLE setMTU(%u) failed rc=%d; using negotiated/default MTU\n",
                      (unsigned)kPreferredMtu, (int)mtuRc);

    s_server = BLEDevice::createServer();
    if (!s_server) {
        BLEDevice::deinit(false);
        clearGattPointers();
        m_errorCode = 1;
        m_state = RadioState::Off;
        radioLockNotifyRadioActive(false);
        return false;
    }
    s_server->setCallbacks(&s_srvCbs);

    BLEService* svc = s_server->createService(kLcrBleServiceUuid);
    if (!svc) {
        BLEDevice::deinit(false);
        clearGattPointers();
        m_errorCode = 1;
        m_state = RadioState::Off;
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
        clearGattPointers();
        m_errorCode = 1;
        m_state = RadioState::Off;
        radioLockNotifyRadioActive(false);
        return false;
    }
    s_chControl->setCallbacks(&s_ctlCbs);
    s_chData->setCallbacks(&s_dataCbs);
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

    // 只有 stack 真正 initialized 时才允许解引用其 advertising singleton。
    // 这也保护未来的初始化失败重构，不让 stop path 去访问半初始化/已释放对象。
    if (BLEDevice::getInitialized()) {
        BLEAdvertising* adv = BLEDevice::getAdvertising();
        if (adv) adv->stop();
        BLEDevice::deinit(false);
    }

    clearGattPointers();
    s_clientConnected = false;
    s_pendingCmd.store(0, std::memory_order_release);
    s_connectEvent.store(false, std::memory_order_release);
    s_disconnectEvent.store(false, std::memory_order_release);
    s_mtuAttPayloadEvent.store(0, std::memory_order_release);
    s_notifyErrorEvent.store(0, std::memory_order_release);
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
        s_notifyErrorEvent.store(0, std::memory_order_release);
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

    uint8_t frame[kLcrBleFrameHeaderLen + kLcrBleMaxCsvPayload];
    uint16_t chunk = m_attPayload;
    if (chunk > kLcrBleMaxCsvPayload) chunk = kLcrBleMaxCsvPayload;
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
    ++m_nextSeq;  // uint16_t 自然 modulo 2^16；前端必须使用相同 wrap 语义。

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

    // Disconnect wins over connect if both were posted before this tick. atomic
    // exchange also closes the callback/main-task data race that volatile leaves.
    if (s_disconnectEvent.exchange(false, std::memory_order_acq_rel)) {
        s_connectEvent.store(false, std::memory_order_release);
        noteClientDisconnected();
    } else if (s_connectEvent.exchange(false, std::memory_order_acq_rel)) {
        noteClientConnected();
    }

    const uint16_t att = s_mtuAttPayloadEvent.exchange(0, std::memory_order_acq_rel);
    if (att) noteAttPayload(att);

    const uint8_t notifyErr = s_notifyErrorEvent.exchange(0, std::memory_order_acq_rel);
    if (notifyErr && m_state == RadioState::Sending) {
        m_errorCode = notifyErr;
        s_streamActive = false;
        m_state = RadioState::Error;
        notifyStatus();
        return;
    }

    const uint8_t cmd = s_pendingCmd.exchange(0, std::memory_order_acq_rel);
    if (cmd) handleCommand(cmd);

    if (m_state == RadioState::Sending && s_streamActive) sendSomeFrames();
}

void RadioManager::noteClientConnected()
{
    s_clientConnected = true;
    // A fresh connection begins at the BLE default MTU until this connection's
    // onMtuChanged proves otherwise. Never inherit the previous client's MTU.
    m_attPayload = kConservativeDataPayload;
    s_notifyErrorEvent.store(0, std::memory_order_release);
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
    s_mtuAttPayloadEvent.store(0, std::memory_order_release);
    s_notifyErrorEvent.store(0, std::memory_order_release);

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
    // cap CSV data for deterministic stack/RAM pressure. 大数据集靠多帧完成。
    if (attPayload <= kLcrBleFrameHeaderLen) return;
    uint16_t p = (uint16_t)(attPayload - kLcrBleFrameHeaderLen);
    if (p > kLcrBleMaxCsvPayload) p = kLcrBleMaxCsvPayload;
    if (p < kConservativeDataPayload) return;
    m_attPayload = p;
    Serial.printf("# BLE ATT payload=%u, csv/frame=%u\n",
                  (unsigned)attPayload, (unsigned)m_attPayload);
}
