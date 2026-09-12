// ============================================================================
// test_misc.cpp —— BLE/frame + radio lock + decimal digit carry/borrow
// ============================================================================
#include "check.h"

#include "ble_protocol.h"
#include "digit_editor_math.h"
#include "measurement_types.h"
#include "radio_lock.h"

#include <string.h>

int main()
{
    radioLockReset();

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
        CHECK(seq == 0x0034);
        CHECK(plen == 4);
        CHECK(memcmp(pl, payload, 4) == 0);
        lcrBleEncodeFrame(buf, sizeof(buf), 1, 0, 1000, payload, 4);
        CHECK(buf[4] == 0xE8 && buf[5] == 0x03);
        CHECK(!lcrBleDecodeFrame(buf, 5, proto, kind, seq, plen, pl));
        buf[0] = 'X';
        CHECK(!lcrBleDecodeFrame(buf, 12, proto, kind, seq, plen, pl));
    }

    {
        uint8_t st[kLcrBleStatusLen];
        CHECK(lcrBleEncodeStatus(st, sizeof(st), 4, 0, 0x11223344, 100, 512)
              == kLcrBleStatusLen);
        CHECK(st[1] == 4);
        CHECK(st[3] == 0x44 && st[4] == 0x33 && st[5] == 0x22 && st[6] == 0x11);
        uint32_t sent = st[7] | (st[8] << 8) | (st[9] << 16) | ((uint32_t)st[10] << 24);
        CHECK(sent == 100);
    }

    {
        CHECK(radioLockInvariantOk());
        radioLockNotifyMeasurementActive(true);
        CHECK(radioLockInvariantOk());
        radioLockNotifyRadioActive(true);
        CHECK(!radioLockInvariantOk());
        radioLockNotifyMeasurementActive(false);
        CHECK(radioLockInvariantOk());
        radioLockNotifyRadioActive(false);
        CHECK(radioLockInvariantOk());
        radioLockReset();
        CHECK(radioLockInvariantOk());
    }

    // 验收示例：编辑十位。300 的十位 0 再减 1，结果必须是 290：
    // 当前位显示 9，同时百位借 1。反向 290 十位 +1 必须进位回 300。
    CHECK(digitEditorStep(300, 0, 999, 3, 1, -1) == 290);
    CHECK(digitEditorStep(290, 0, 999, 3, 1, +1) == 300);
    CHECK(digitEditorStep(199, 0, 999, 3, 2, +1) == 200);
    CHECK(digitEditorStep(200, 0, 999, 3, 2, -1) == 199);
    CHECK(digitEditorStep(0, 0, 999, 3, 2, -1) == 0);      // lower clamp
    CHECK(digitEditorStep(999, 0, 999, 3, 2, +1) == 999);  // upper clamp

    CHECK(strlen(sweepStateText(SweepState::TransferReady)) > 0);
    CHECK(strcmp(sweepStateText(SweepState::Insufficient), "DATA INSUFFICIENT") == 0);
    CHECK(strcmp(measurementKindText(MeasurementKind::OnePortImpedance),
                 "ONE_PORT_Z") == 0);
    return testSummary("test_misc");
}
