// ============================================================================
// ble_protocol.h —— GATT v1 协议常量与帧编解码（host 可编译，无 BLE 依赖）
// ----------------------------------------------------------------------------
// 固定 UUID（不随文件/分支改变）：
//   Service:  6e6f0001-5f31-4c43-a001-6c63722d7631
//   Control:  6e6f0002-...  WRITE WITH RESPONSE
//   Status:   6e6f0003-...  READ + NOTIFY
//   Metadata: 6e6f0004-...  READ
//   Data:     6e6f0005-...  NOTIFY
//
// Data 帧布局（plan.md §6.2）：
//   offset  size  field
//   0       2     magic = ASCII 'L','C'
//   2       1     protocol = 1
//   3       1     dataset kind (0 = ONE_PORT_Z, 1 = TWO_PORT_H)
//   4       2     seq, uint16 little-endian
//   6       2     payload_len, uint16 little-endian
//   8       N     CSV bytes
// ============================================================================

#pragma once

#include "fw_version.h"
#include "measurement_types.h"

#include <stddef.h>
#include <stdint.h>

// ---- 固定 UUID（字符串形式给 BLE 栈）---------------------------------------
extern const char* const kLcrBleServiceUuid;
extern const char* const kLcrBleControlUuid;
extern const char* const kLcrBleStatusUuid;
extern const char* const kLcrBleMetadataUuid;
extern const char* const kLcrBleDataUuid;

// ---- Control v1 定长命令字节 ------------------------------------------------
enum class BleCommand : uint8_t {
    StartTransfer   = 0x01,
    RestartTransfer = 0x02,
    AbortTransfer   = 0x03,
    GetStatus       = 0x04,
};

// ---- 帧常量 ------------------------------------------------------------------
static constexpr uint8_t kLcrBleFrameMagic0 = 'L';
static constexpr uint8_t kLcrBleFrameMagic1 = 'C';
static constexpr uint8_t kLcrBleFrameHeaderLen = 8;
// ATT 保守通知负载：未协商 MTU 时按 MTU=23 → ATT payload 20 → CSV 12 字节
static constexpr uint16_t kLcrBleConservativeAttPayload = 20;

// kind 字节
static constexpr uint8_t kLcrBleKindOnePort = 0;
static constexpr uint8_t kLcrBleKindTwoPort = 1;

inline uint8_t lcrBleKindByte(MeasurementKind k)
{
    return k == MeasurementKind::OnePortImpedance ? kLcrBleKindOnePort
                                                  : kLcrBleKindTwoPort;
}

// 编码一帧；返回总长（buf 至少 header+payloadLen），容量不足返回 0
inline size_t lcrBleEncodeFrame(uint8_t* buf, size_t cap, uint8_t protocol,
                                uint8_t kind, uint16_t seq,
                                const uint8_t* payload, uint16_t payloadLen)
{
    if (!buf || (payloadLen > 0 && !payload)) return 0;
    if (cap < (size_t)kLcrBleFrameHeaderLen + payloadLen) return 0;
    buf[0] = kLcrBleFrameMagic0;
    buf[1] = kLcrBleFrameMagic1;
    buf[2] = protocol;
    buf[3] = kind;
    buf[4] = (uint8_t)(seq & 0xFF);
    buf[5] = (uint8_t)(seq >> 8);
    buf[6] = (uint8_t)(payloadLen & 0xFF);
    buf[7] = (uint8_t)(payloadLen >> 8);
    for (uint16_t i = 0; i < payloadLen; ++i) buf[kLcrBleFrameHeaderLen + i] = payload[i];
    return (size_t)kLcrBleFrameHeaderLen + payloadLen;
}

// 解码帧头（含合法性检查）；payload 指向 buf+8，返回 true 表示帧头合法
inline bool lcrBleDecodeFrame(const uint8_t* buf, size_t len, uint8_t& protocol,
                              uint8_t& kind, uint16_t& seq, uint16_t& payloadLen,
                              const uint8_t*& payload)
{
    if (!buf || len < kLcrBleFrameHeaderLen) return false;
    if (buf[0] != kLcrBleFrameMagic0 || buf[1] != kLcrBleFrameMagic1) return false;
    protocol = buf[2];
    kind = buf[3];
    seq = (uint16_t)(buf[4] | ((uint16_t)buf[5] << 8));
    payloadLen = (uint16_t)(buf[6] | ((uint16_t)buf[7] << 8));
    if ((size_t)kLcrBleFrameHeaderLen + payloadLen > len) return false;
    payload = buf + kLcrBleFrameHeaderLen;
    return true;
}

// Status 特征负载（READ + NOTIFY）：
//   [0] protocol      [1] state      [2] error code
//   [3..6]  session id u32 LE        [7..10] bytes sent u32 LE
//   [11..14] bytes total u32 LE
static constexpr uint8_t kLcrBleStatusLen = 15;

inline size_t lcrBleEncodeStatus(uint8_t* buf, size_t cap, uint8_t state,
                                 uint8_t errorCode, uint32_t sessionId,
                                 uint32_t bytesSent, uint32_t bytesTotal)
{
    if (!buf || cap < kLcrBleStatusLen) return 0;
    buf[0] = LCR_BLE_PROTOCOL_VERSION;
    buf[1] = state;
    buf[2] = errorCode;
    for (int i = 0; i < 4; ++i) {
        buf[3 + i]  = (uint8_t)((sessionId >> (8 * i)) & 0xFF);
        buf[7 + i]  = (uint8_t)((bytesSent >> (8 * i)) & 0xFF);
        buf[11 + i] = (uint8_t)((bytesTotal >> (8 * i)) & 0xFF);
    }
    return kLcrBleStatusLen;
}
