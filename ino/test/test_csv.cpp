// ============================================================================
// test_csv.cpp —— canonical CSV / CRC32 / metadata / BLE 帧（plan.md §5.2/§6.2）
// ============================================================================
#include "check.h"
#include "ble_protocol.h"
#include "dataset.h"
#include "measurement_types.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

int main()
{
    // ---- 1. CRC-32 标准向量（zlib 兼容；与前端 crc32.ts 位位一致）-------------
    CHECK(crc32Of((const uint8_t*)"123456789", 9) == 0xCBF43926u);
    CHECK(crc32Of((const uint8_t*)"", 0) == 0x00000000u);
    CHECK(crc32Of((const uint8_t*)"LCR", 3) != 0);

    // ---- 2. 单端口 CSV v1 格式 ------------------------------------------------
    {
        OnePortPoint pts[3];
        const double f[3] = {100.0, 316.2, 1000.0};
        for (int i = 0; i < 3; ++i) {
            pts[i].requestedHz = f[i];
            pts[i].actualHz = f[i] + 0.01;      // f 必须用 actualHz
            pts[i].reOhm = 12.34;
            pts[i].imOhm = -45.67;
            pts[i].quality.status = MeasurementStatus::Ok;
        }
        static char csv[2048];
        const size_t n = formatOnePortCsv(pts, 3, "factory-none", 1.05,
                                          csv, sizeof(csv));
        CHECK(n > 0);
        CHECK(strstr(csv, "# lcr-dataset=one-port-z") == csv);   // 首行
        CHECK(strstr(csv, "# schema=lcr-z-csv-v1") != nullptr);
        CHECK(strstr(csv, "# protocol=1") != nullptr);
        CHECK(strstr(csv, "# firmware=") != nullptr);
        CHECK(strstr(csv, "# calibration_id=factory-none") != nullptr);
        CHECK(strstr(csv, "# drive_vrms=1.05") != nullptr);
        CHECK(strstr(csv, "\nf,re,im\n") != nullptr);
        // f 用 actualHz 而非 requestedHz（§9.6）
        CHECK(strstr(csv, "\n100.01,12.34,-45.67\n") != nullptr);
        CHECK(strstr(csv, "\n100," ) == nullptr);
        // 每行 3 列、可解析为 double、f>0、re/im 有限（§9.6）
        int rows = 0;
        for (char* line = strtok(csv + strlen("# lcr-dataset=one-port-z\n"), "\n");
             line; line = strtok(nullptr, "\n")) {
            if (line[0] == '#' || strncmp(line, "f,re,im", 7) == 0) continue;
            double a, b, c;
            CHECK(sscanf(line, "%lf,%lf,%lf", &a, &b, &c) == 3);
            CHECK(a > 0 && isfinite(a) && isfinite(b) && isfinite(c));
            ++rows;
        }
        CHECK(rows == 3);
    }

    // ---- 3. 失败点/非有限点不进 CSV，也不伪造为 0 ------------------------------
    {
        OnePortPoint pts[4];
        for (int i = 0; i < 4; ++i) {
            pts[i].actualHz = 100.0 * (i + 1);
            pts[i].reOhm = 50.0;
            pts[i].imOhm = 0.0;
            pts[i].quality.status = MeasurementStatus::Ok;
        }
        pts[1].quality.status = MeasurementStatus::AdcOverrun;
        pts[2].reOhm = NAN;
        static char csv[2048];
        const size_t n = formatOnePortCsv(pts, 4, "cal-x", 1.0, csv, sizeof(csv));
        CHECK(n > 0);
        CHECK(strstr(csv, "\n100,") != nullptr);
        CHECK(strstr(csv, ",0,0\n") == nullptr);        // 不伪造 0
        int rows = 0;
        for (const char* s = csv; (s = strchr(s, '\n')); ) { ++rows; ++s; }
        CHECK(rows == 2 + 7);                            // 2 个有效数据行 + 7 头行
    }

    // ---- 4. 双端口 CSV v1：f,re_h,im_h ----------------------------------------
    {
        TwoPortPoint pts[2];
        pts[0].actualHz = 100.0;  pts[0].reH = 0.923;  pts[0].imH = -0.146;
        pts[1].actualHz = 1000.0; pts[1].reH = 0.5;    pts[1].imH = -0.4;
        for (int i = 0; i < 2; ++i) pts[i].quality.status = MeasurementStatus::Ok;
        static char csv[1024];
        const size_t n = formatTwoPortCsv(pts, 2, "factory-none", 1.05,
                                          csv, sizeof(csv));
        CHECK(n > 0);
        CHECK(strstr(csv, "# lcr-dataset=two-port-h") == csv);
        CHECK(strstr(csv, "# schema=lcr-h-csv-v1") != nullptr);
        CHECK(strstr(csv, "\nf,re_h,im_h\n") != nullptr);
        CHECK(strstr(csv, "\n100,0.923,-0.146\n") != nullptr);
    }

    // ---- 5. 容量不足返回 0（不溢出）--------------------------------------------
    {
        OnePortPoint pts[1];
        pts[0].actualHz = 100.0; pts[0].reOhm = 1; pts[0].imOhm = 2;
        pts[0].quality.status = MeasurementStatus::Ok;
        char tiny[16];
        CHECK(formatOnePortCsv(pts, 1, "c", 1.0, tiny, sizeof(tiny)) == 0);
    }

    // ---- 6. metadata JSON 字段固定（§6.2）---------------------------------------
    {
        char js[METADATA_JSON_MAX];
        const size_t n = formatMetadataJson(MeasurementKind::OnePortImpedance,
                                            12345678u, 121, 4812u, 0xA1B2C3D4u,
                                            "cal-abc", js, sizeof(js));
        CHECK(n > 0);
        CHECK(strstr(js, "\"protocol\":1") != nullptr);
        CHECK(strstr(js, "\"firmware\":\"") != nullptr);
        CHECK(strstr(js, "\"session_id\":12345678") != nullptr);
        CHECK(strstr(js, "\"dataset_kind\":\"ONE_PORT_Z\"") != nullptr);
        CHECK(strstr(js, "\"schema\":\"lcr-z-csv-v1\"") != nullptr);
        CHECK(strstr(js, "\"point_count\":121") != nullptr);
        CHECK(strstr(js, "\"byte_count\":4812") != nullptr);
        CHECK(strstr(js, "\"crc32\":\"A1B2C3D4\"") != nullptr);
        CHECK(strstr(js, "\"calibration_id\":\"cal-abc\"") != nullptr);
        const size_t n2 = formatMetadataJson(MeasurementKind::TwoPortTransfer, 1u,
                                             2, 3, 4, "x", js, sizeof(js));
        CHECK(n2 > 0);
        CHECK(strstr(js, "\"dataset_kind\":\"TWO_PORT_H\"") != nullptr);
        CHECK(strstr(js, "\"schema\":\"lcr-h-csv-v1\"") != nullptr);
    }

    // ---- 7. BLE 帧编解码 round-trip（§6.2 布局）--------------------------------
    {
        uint8_t frame[64];
        const uint8_t payload[5] = {'a', 'b', 'c', 'd', 'e'};
        const size_t n = lcrBleEncodeFrame(frame, sizeof(frame), 1,
                                           kLcrBleKindOnePort, 0x1234,
                                           payload, 5);
        CHECK(n == 13);
        CHECK(frame[0] == 'L' && frame[1] == 'C');
        CHECK(frame[2] == 1);
        CHECK(frame[3] == kLcrBleKindOnePort);
        uint8_t proto, kind; uint16_t seq, plen;
        const uint8_t* pl = nullptr;
        CHECK(lcrBleDecodeFrame(frame, n, proto, kind, seq, plen, pl));
        CHECK(proto == 1 && kind == kLcrBleKindOnePort);
        CHECK(seq == 0x1234);                      // little-endian
        CHECK(plen == 5);
        CHECK(memcmp(pl, payload, 5) == 0);
        // 损坏检测：magic 错 / 长度不足
        frame[0] = 'X';
        CHECK(!lcrBleDecodeFrame(frame, n, proto, kind, seq, plen, pl));
        frame[0] = 'L';
        CHECK(!lcrBleDecodeFrame(frame, 5, proto, kind, seq, plen, pl));
        // 容量不足编码失败
        CHECK(lcrBleEncodeFrame(frame, 10, 1, 0, 0, payload, 5) == 0);
    }

    // ---- 8. Status 负载（15 字节布局）------------------------------------------
    {
        uint8_t st[kLcrBleStatusLen];
        CHECK(lcrBleEncodeStatus(st, sizeof(st), 4, 7, 0xDEADBEEFu, 100u, 4812u)
              == kLcrBleStatusLen);
        CHECK(st[0] == 1);                          // protocol
        CHECK(st[1] == 4);                          // state
        CHECK(st[2] == 7);                          // error
        CHECK(st[3] == 0xEF && st[4] == 0xBE && st[5] == 0xAD && st[6] == 0xDE);
        CHECK(st[7] == 100 && st[8] == 0 && st[9] == 0 && st[10] == 0);
        CHECK(st[11] == 0xCC && st[12] == 0x12);    // 4812 = 0x12CC
    }

    // ---- 9. 频率表（§5.2 合法性）------------------------------------------------
    {
        SweepConfig cfg{};
        cfg.fStartHz = 100; cfg.fStopHz = 10000;
        cfg.pointsPerDecade = 10; cfg.maxPoints = 257; cfg.logSpacing = true;
        double f[300];
        const size_t n = buildFrequencyPlan(cfg, f, 300);
        CHECK(n == 21);                              // 10/dec × 2 dec + 1
        CHECK_NEAR(f[0], 100.0, 1e-9);
        CHECK_NEAR(f[n - 1], 10000.0, 1e-9);
        for (size_t i = 1; i < n; ++i) CHECK(f[i] > f[i - 1]);
        // maxPoints 截断
        cfg.maxPoints = 10;
        const size_t n2 = buildFrequencyPlan(cfg, f, 300);
        CHECK(n2 == 10);
        // 非法配置
        cfg.fStopHz = 50;
        CHECK(buildFrequencyPlan(cfg, f, 300) == 0);
    }

    return testSummary("test_csv");
}
