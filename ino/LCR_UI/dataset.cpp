// ============================================================================
// dataset.cpp —— CSV/metadata 格式化 + CRC32 实现（host 可编译，单测覆盖）
// ============================================================================

#include "dataset.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
// 数值格式：% .6g —— 6 位有效数字、自动去除多余小数；保证 f/re/im 域内
// 行宽 ≤ 40 字符（含逗号分隔），且与网站 parseZCsv 的 Number() 兼容。
// 写入一行（不含换行）；返回追加后的长度，容量不足返回 0。
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

size_t formatOnePortCsv(const OnePortPoint* pts, uint16_t n,
                        const char* calibId, double driveVrms,
                        char* out, size_t cap)
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
    snprintf(head, sizeof(head), "# calibration_id=%s", calibId ? calibId : "");
    if ((pos = appendLine(out, cap, pos, head)) == 0) return 0;
    snprintf(head, sizeof(head), "# drive_vrms=%.6g", driveVrms);
    if ((pos = appendLine(out, cap, pos, head)) == 0) return 0;
    if ((pos = appendLine(out, cap, pos, "f,re,im")) == 0) return 0;

    for (uint16_t i = 0; i < n; ++i) {
        // 只有 Ok 的点进入拟合 CSV（失败点不伪造为 0）
        if (pts[i].quality.status != MeasurementStatus::Ok) continue;
        if (!isfinite(pts[i].actualHz) || !(pts[i].actualHz > 0.0) ||
            !isfinite(pts[i].reOhm) || !isfinite(pts[i].imOhm)) continue;
        const size_t p = appendRow3(out, cap, pos, pts[i].actualHz,
                                    pts[i].reOhm, pts[i].imOhm);
        if (p == 0) return 0;
        pos = p;
        if (pos + 1 >= cap) return 0;
        out[pos++] = '\n';
    }
    out[pos] = '\0';
    return pos;
}

size_t formatTwoPortCsv(const TwoPortPoint* pts, uint16_t n,
                        const char* calibId, double driveVrms,
                        char* out, size_t cap)
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
    snprintf(head, sizeof(head), "# calibration_id=%s", calibId ? calibId : "");
    if ((pos = appendLine(out, cap, pos, head)) == 0) return 0;
    snprintf(head, sizeof(head), "# drive_vrms=%.6g", driveVrms);
    if ((pos = appendLine(out, cap, pos, head)) == 0) return 0;
    if ((pos = appendLine(out, cap, pos, "f,re_h,im_h")) == 0) return 0;

    for (uint16_t i = 0; i < n; ++i) {
        if (pts[i].quality.status != MeasurementStatus::Ok) continue;
        if (!isfinite(pts[i].actualHz) || !(pts[i].actualHz > 0.0) ||
            !isfinite(pts[i].reH) || !isfinite(pts[i].imH)) continue;
        const size_t p = appendRow3(out, cap, pos, pts[i].actualHz,
                                    pts[i].reH, pts[i].imH);
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
                          uint32_t crc32Value, const char* calibId,
                          char* out, size_t cap)
{
    if (!out || cap == 0) return 0;
    const int w = snprintf(out, cap,
        "{\"protocol\":%d,\"firmware\":\"%s\",\"session_id\":%lu,"
        "\"dataset_kind\":\"%s\",\"schema\":\"%s\",\"point_count\":%u,"
        "\"byte_count\":%lu,\"crc32\":\"%08lX\",\"calibration_id\":\"%s\"}",
        LCR_BLE_PROTOCOL_VERSION, LCR_FW_VERSION, (unsigned long)sessionId,
        kind == MeasurementKind::OnePortImpedance ? "ONE_PORT_Z" : "TWO_PORT_H",
        kind == MeasurementKind::OnePortImpedance ? "lcr-z-csv-v1" : "lcr-h-csv-v1",
        (unsigned)pointCount, (unsigned long)byteCount,
        (unsigned long)crc32Value, calibId ? calibId : "");
    return (w > 0 && (size_t)w < cap) ? (size_t)w : 0;
}
