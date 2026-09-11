// ============================================================================
// test_misc.cpp —— BLE 帧/seq/status + radio_lock 互斥 + 状态文本
// （plan.md §17.1 第 17 项 + Gate G 的 host 侧运行时验证）
// ============================================================================
#include "check.h"

#include "ble_protocol.h"
#include "measurement_types.h"
#include "radio_lock.h"

#include <string.h>

int main()
{
    radioLockReset();

    // ---- 17. BLE 帧 seq（LE）+ 编解码往返 --------------------------------
    {
        uint8_t buf[64];
        const uint8_t payload[4] = {1, 2, 3, 4};
        const size_t n = lcrBleEncodeFrame(buf, sizeof(buf),
                                           LCR_BLE_PROTOCOL_VERSION,
                                           kLcrBleKindTwoPort, 0x0034,
                                           payload, 4);
        CHECK(n == 12);
        CHECK(buf[0] == 'L' && buf[1] == 'C');
        uint8_t proto, kind; uint16_t seq, plen;
        const uint8_t* pl = nullptr;
        CHECK(lcrBleDecodeFrame(buf, n, proto, kind, seq, plen, pl));
        CHECK(proto == 1);
        CHECK(kind == kLcrBleKindTwoPort);
        CHECK(seq == 0x0034);                       // seq LE 往返
        CHECK(plen == 4);
        CHECK(memcmp(pl, payload, 4) == 0);
        // seq 高字节在 buf[5]（>255 的 seq）
        lcrBleEncodeFrame(buf, sizeof(buf), 1, 0, 1000, payload, 4);
        CHECK(buf[4] == 0xE8 && buf[5] == 0x03);
        // 非法帧
        CHECK(!lcrBleDecodeFrame(buf, 5, proto, kind, seq, plen, pl));
        buf[0] = 'X';
        CHECK(!lcrBleDecodeFrame(buf, 12, proto, kind, seq, plen, pl));
    }

    // ---- Status 负载（15B：state/err/session/sent/total）-------------------
    {
        uint8_t st[kLcrBleStatusLen];
        CHECK(lcrBleEncodeStatus(st, sizeof(st), 4, 0, 0x11223344, 100, 512)
              == kLcrBleStatusLen);
        CHECK(st[1] == 4);
        CHECK(st[3] == 0x44 && st[4] == 0x33 && st[5] == 0x22 && st[6] == 0x11);
        uint32_t sent = st[7] | (st[8] << 8) | (st[9] << 16) | ((uint32_t)st[10] << 24);
        CHECK(sent == 100);
    }

    // ---- radio_lock：测量与射频互斥（Gate G 运行时半边）--------------------
    {
        CHECK(radioLockInvariantOk());
        radioLockNotifyMeasurementActive(true);
        CHECK(radioLockInvariantOk());              // 测量中射频仍 Off：成立
        radioLockNotifyRadioActive(true);
        CHECK(!radioLockInvariantOk());             // 测量+射频同时活动：违反
        radioLockNotifyMeasurementActive(false);    // seal 完成
        CHECK(radioLockInvariantOk());              // 射频可开
        radioLockNotifyRadioActive(false);
        CHECK(radioLockInvariantOk());
        radioLockReset();
        CHECK(radioLockInvariantOk());
    }

    // ---- 状态文本非空 -------------------------------------------------------
    CHECK(strlen(sweepStateText(SweepState::TransferReady)) > 0);
    CHECK(strcmp(sweepStateText(SweepState::Insufficient), "DATA INSUFFICIENT") == 0);
    CHECK(strcmp(measurementKindText(MeasurementKind::OnePortImpedance),
                 "ONE_PORT_Z") == 0);
    return testSummary("test_misc");
}
