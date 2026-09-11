// ============================================================================
// dataset.cpp —— CSV/metadata 格式化 + CRC32 实现（host 可编译，单测覆盖）
// ============================================================================

#include "dataset.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// 数字宏 -> 字符串（schema 版本号写进 JSON："lcr-z-csv-v" "2"）
#define LCR_STR_(x) #x
#define LCR_STR2(x) LCR_STR_(x)

// ---------------------------------------------------------------------------
// CRC-32/ISO-HDLC（zlib 兼容）：反射多项式 0xEDB88320
// uint32_t crc = crc32Final(crc32Update(0xFFFFFFFF, data, len));
// 等价于 zlib crc32(0, data, len)。前端 crc32.ts 与本实现位位一致。
// ---------------------------------------------------------------------------
uint32_t crc32Update(uint32_t state, const uint8_t* data, size_t len)
{
    static uint32_t table[256];
    static bool tableInit = false;
    if (!tableInit) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        tableInit = true;
    }
    uint32_t c = state;
    for (size_t i = 0; i < len; ++i)
        c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c;
}

uint32_t crc32Final(uint32_t state) { return state ^ 0xFFFFFFFFu; }

uint32_t crc32Of(const uint8_t* data, size_t len)
{
    return crc32Final(crc32Update(0xFFFFFFFFu, data, len));
}

// ---------------------------------------------------------------------------
static size_t appendRow3(char* out, size_t cap, size_t pos,
                         double a, double b, double c)
{
    char line[96];
    const int w = snprintf(line, sizeof(line), "%.6g,%.6g,%.6g", a, b, c);
    if (w < 0 || pos + (size_t)w >= cap) return 0;
    memcpy(out + pos, line, (size_t)w);
    return pos + (size_t)w;
}

static size_t appendLine(char* out, size_t cap, size_t pos, const char* s)
{
    const size_t l = strlen(s);
    if (pos + l + 1 >= cap) return 0;
    memcpy(out + pos, s, l);
    pos += l;
    out[pos++] = '\n';
    return pos;
}

// 校准状态摘要（诚实描述，不杜撰 ID）：
// rangesValid 由 DNT lcr_api_cal_status 汇总；双口固定 raw_w_path（W 链
// 在 DNT 源码中明确为 raw chain / no calib，不得暗示已校准）。
size_t formatCalStateString(uint8_t rangesValid, bool openValid, bool shortValid,
                            char* out, size_t cap)
{
    if (!out || cap == 0) return 0;
    const int w = snprintf(out, cap, "cal:%u/10,open:%s,short:%s",
                           (unsigned)rangesValid,
                           openValid ? "ok" : "--",
                           shortValid ? "ok" : "--");
    return (w > 0 && (size_t)w < cap) ? (size_t)w : 0;
}

// ---------------------------------------------------------------------------
// v2 公共头部：lcr-dataset / schema / protocol / firmware /
// measurement_backend / calibration_state。数据列保持 3 列不变，
// 拟合核心（Try1/2/3）不受影响。
// ---------------------------------------------------------------------------
size_t formatOnePortCsv(const ZPointRec* pts, uint16_t n,
                        const char* calState, char* out, size_t cap)
{
    if (!pts || !out || cap == 0) return 0;
    char head[160];
    size_t pos = 0;

    if ((pos = appendLine(out, cap, pos, "# lcr-dataset=one-port-z")) == 0) return 0;
    snprintf(head, sizeof(head), "# schema=lcr-z-csv-v%d", LCR_Z_CSV_SCHEMA_VERSION);
    if ((pos = appendLine(out, cap, pos, head)) == 0) return 0;
    snprintf(head, sizeof(head), "# protocol=%d", LCR_BLE_PROTOCOL_VERSION);
    if ((pos = appendLine(out, cap, pos, head)) == 0) return 0;
    snprintf(head, sizeof(head), "# firmware=%s", LCR_FW_VERSION);
    if ((pos = appendLine(out, cap, pos, head)) == 0) return 0;
    if ((pos = appendLine(out, cap, pos,
                          "# measurement_backend=DO_NOT_TOUCH_lcr_api")) == 0) return 0;
    snprintf(head, sizeof(head), "# calibration_state=%s",
             calState ? calState : "");
    if ((pos = appendLine(out, cap, pos, head)) == 0) return 0;
    if ((pos = appendLine(out, cap, pos, "f,re,im")) == 0) return 0;

    for (uint16_t i = 0; i < n; ++i) {
        // 只有有限数值的有效点进入拟合 CSV（失败点不伪造为 0）
        if (!isfinite(pts[i].f) || !(pts[i].f > 0.0) ||
            !isfinite(pts[i].re) || !isfinite(pts[i].im)) continue;
        const size_t p = appendRow3(out, cap, pos, pts[i].f, pts[i].re, pts[i].im);
        if (p == 0) return 0;
        pos = p;
        if (pos + 1 >= cap) return 0;
        out[pos++] = '\n';
    }
    out[pos] = '\0';
    return pos;
}

size_t formatTwoPortCsv(const HPointRec* pts, uint16_t n,
                        const char* calState, char* out, size_t cap)
{
    if (!pts || !out || cap == 0) return 0;
    char head[160];
    size_t pos = 0;

    if ((pos = appendLine(out, cap, pos, "# lcr-dataset=two-port-h")) == 0) return 0;
    snprintf(head, sizeof(head), "# schema=lcr-h-csv-v%d", LCR_H_CSV_SCHEMA_VERSION);
    if ((pos = appendLine(out, cap, pos, head)) == 0) return 0;
    snprintf(head, sizeof(head), "# protocol=%d", LCR_BLE_PROTOCOL_VERSION);
    if ((pos = appendLine(out, cap, pos, head)) == 0) return 0;
    snprintf(head, sizeof(head), "# firmware=%s", LCR_FW_VERSION);
    if ((pos = appendLine(out, cap, pos, head)) == 0) return 0;
    if ((pos = appendLine(out, cap, pos,
                          "# measurement_backend=DO_NOT_TOUCH_lcr_api")) == 0) return 0;
    snprintf(head, sizeof(head), "# calibration_state=%s",
             calState ? calState : "");
    if ((pos = appendLine(out, cap, pos, head)) == 0) return 0;
    if ((pos = appendLine(out, cap, pos, "f,re_h,im_h")) == 0) return 0;

    for (uint16_t i = 0; i < n; ++i) {
        if (!isfinite(pts[i].f) || !(pts[i].f > 0.0) ||
            !isfinite(pts[i].reH) || !isfinite(pts[i].imH)) continue;
        const size_t p = appendRow3(out, cap, pos, pts[i].f, pts[i].reH, pts[i].imH);
        if (p == 0) return 0;
        pos = p;
        if (pos + 1 >= cap) return 0;
        out[pos++] = '\n';
    }
    out[pos] = '\0';
    return pos;
}

size_t formatMetadataJson(MeasurementKind kind, uint32_t sessionId,
                          uint16_t pointCount, uint32_t byteCount,
                          uint32_t crc32Value, const char* calState,
                          char* out, size_t cap)
{
    if (!out || cap == 0) return 0;
    const int w = snprintf(out, cap,
        "{\"protocol\":%d,\"firmware\":\"%s\",\"session_id\":%lu,"
        "\"dataset_kind\":\"%s\",\"schema\":\"%s\",\"point_count\":%u,"
        "\"byte_count\":%lu,\"crc32\":\"%08lX\","
        "\"measurement_backend\":\"DO_NOT_TOUCH_lcr_api\","
        "\"calibration_state\":\"%s\"}",
        LCR_BLE_PROTOCOL_VERSION, LCR_FW_VERSION, (unsigned long)sessionId,
        kind == MeasurementKind::OnePortImpedance ? "ONE_PORT_Z" : "TWO_PORT_H",
        kind == MeasurementKind::OnePortImpedance
            ? "lcr-z-csv-v" LCR_STR2(LCR_Z_CSV_SCHEMA_VERSION)
            : "lcr-h-csv-v" LCR_STR2(LCR_H_CSV_SCHEMA_VERSION),
        (unsigned)pointCount, (unsigned long)byteCount,
        (unsigned long)crc32Value, calState ? calState : "");
    return (w > 0 && (size_t)w < cap) ? (size_t)w : 0;
}
