// ============================================================================
// test_csv.cpp —— CSV v2 / CRC32 / metadata v2（plan.md §17.1 第 7/16/18 项）
//   7  Z 失败点不进 CSV（在 test_sweep；此处复核格式器行为）
//   16 CRC32 标准向量 + byte_count 对应 exact CSV bytes
//   18 Metadata v2 字段
// ============================================================================
#include "check.h"

#include "dataset.h"
#include "measurement_types.h"

#include <math.h>
#include <string.h>

int main()
{
    // ---- 16. CRC-32/ISO-HDLC 标准向量（与前端 crc32.ts 同向量）------------
    {
        const char* s = "123456789";
        CHECK(crc32Of((const uint8_t*)s, 9) == 0xCBF43926u);
        CHECK(crc32Of((const uint8_t*)"", 0) == 0x00000000u);
        // 分块更新 == 整段
        const char* s2 = "LCR-Analyzer golden csv fixture payload";
        const size_t n = strlen(s2);
        const size_t half = n / 2;
        const uint32_t a = crc32Final(crc32Update(0xFFFFFFFFu, (const uint8_t*)s2, n));
        uint32_t state = crc32Update(0xFFFFFFFFu, (const uint8_t*)s2, half);
        state = crc32Update(state, (const uint8_t*)s2 + half, n - half);
        CHECK(crc32Final(state) == a);
    }

    // ---- v2 单口 CSV 头部 + 数据行 + byte_count/CRC 对应 exact bytes ------
    {
        ZPointRec recs[3] = {{100.0, 1000.0, 1.5},
                             {316.0, 1002.0, -1.5},
                             {1000.0, 1004.0, -4.5}};
        static char csv[ONEPORT_CSV_MAX];
        const size_t len = formatOnePortCsv(recs, 3, "cal:3/10,open:ok,short:--",
                                            csv, sizeof(csv));
        CHECK(len > 0);
        CHECK(len == strlen(csv));                    // NUL 结尾且长度一致
        CHECK(strncmp(csv, "# lcr-dataset=one-port-z\n", 24) == 0);
        CHECK(strstr(csv, "# schema=lcr-z-csv-v2\n") != nullptr);
        CHECK(strstr(csv, "# protocol=1\n") != nullptr);
        CHECK(strstr(csv, "# firmware=4.1.0\n") != nullptr);
        CHECK(strstr(csv, "# measurement_backend=DO_NOT_TOUCH_lcr_api\n") != nullptr);
        CHECK(strstr(csv, "# calibration_state=cal:3/10,open:ok,short:--\n") != nullptr);
        CHECK(strstr(csv, "f,re,im\n100,1000,1.5\n") != nullptr);
        // v1 杜撰字段不存在
        CHECK(strstr(csv, "drive_vrms") == nullptr);
        CHECK(strstr(csv, "calibration_id") == nullptr);
        // CRC 覆盖 exact bytes（截断最后一字节即改变 CRC）
        const uint32_t crc = crc32Of((const uint8_t*)csv, len);
        CHECK(crc != crc32Of((const uint8_t*)csv, len - 1));
    }

    // ---- v2 双口 CSV -------------------------------------------------------
    {
        HPointRec recs[2] = {{100.0, 0.99, -0.01}, {1000.0, 0.7071, -0.7071}};
        static char csv[TWOPORT_CSV_MAX];
        const size_t len = formatTwoPortCsv(recs, 2, "raw_w_path", csv, sizeof(csv));
        CHECK(len > 0);
        CHECK(strncmp(csv, "# lcr-dataset=two-port-h\n", 25) == 0);
        CHECK(strstr(csv, "# schema=lcr-h-csv-v2\n") != nullptr);
        CHECK(strstr(csv, "# calibration_state=raw_w_path\n") != nullptr);
        CHECK(strstr(csv, "f,re_h,im_h\n") != nullptr);
        CHECK(strstr(csv, "100,0.99,-0.01") != nullptr);
    }

    // ---- 18. Metadata v2 ---------------------------------------------------
    {
        char json[METADATA_JSON_MAX];
        const size_t n = formatMetadataJson(MeasurementKind::OnePortImpedance,
                                            0x12345678, 8, 512, 0xDEADBEEF,
                                            "cal:3/10,open:ok,short:--",
                                            json, sizeof(json));
        CHECK(n > 0 && n < sizeof(json));
        CHECK(strstr(json, "\"protocol\":1") != nullptr);
        CHECK(strstr(json, "\"firmware\":\"4.1.0\"") != nullptr);
        CHECK(strstr(json, "\"dataset_kind\":\"ONE_PORT_Z\"") != nullptr);
        CHECK(strstr(json, "\"schema\":\"lcr-z-csv-v2\"") != nullptr);
        CHECK(strstr(json, "\"point_count\":8") != nullptr);
        CHECK(strstr(json, "\"byte_count\":512") != nullptr);
        CHECK(strstr(json, "\"crc32\":\"DEADBEEF\"") != nullptr);
        CHECK(strstr(json, "\"measurement_backend\":\"DO_NOT_TOUCH_lcr_api\"") != nullptr);
        CHECK(strstr(json, "\"calibration_state\":\"cal:3/10,open:ok,short:--\"") != nullptr);
        CHECK(strstr(json, "calibration_id") == nullptr);   // 杜撰 ID 已删
        // 双口 schema 字符串
        const size_t n2 = formatMetadataJson(MeasurementKind::TwoPortTransfer,
                                             1, 2, 64, 0x11223344, "raw_w_path",
                                             json, sizeof(json));
        CHECK(n2 > 0);
        CHECK(strstr(json, "\"dataset_kind\":\"TWO_PORT_H\"") != nullptr);
        CHECK(strstr(json, "\"schema\":\"lcr-h-csv-v2\"") != nullptr);
    }

    // ---- 校准状态摘要字符串 -----------------------------------------------
    {
        char buf[CAL_STATE_MAX];
        CHECK(formatCalStateString(3, true, false, buf, sizeof(buf)) > 0);
        CHECK(strcmp(buf, "cal:3/10,open:ok,short:--") == 0);
        formatCalStateString(10, true, true, buf, sizeof(buf));
        CHECK(strcmp(buf, "cal:10/10,open:ok,short:ok") == 0);
    }
    return testSummary("test_csv");
}
