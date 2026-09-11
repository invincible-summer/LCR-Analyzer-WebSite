// ============================================================================
// dataset.h —— 封存数据集 + canonical CSV + CRC32（host 可编译）
// ----------------------------------------------------------------------------
// 语义（plan.md §5.2/§6.2）：
//   * sweep 全部频点结束、停激励/ADC、静默保护之后 dataset 才能 seal；
//   * seal 后生成 canonical CSV + metadata + 全文件 CRC32；
//   * seal 后数据不可变：BLE 传输失败绝不回头修改测量值；
//   * 质量失败的点不伪造为 0，不进入拟合 CSV（错误保留在 diagnostics 中）。
//
// CSV v1（与网站 frontend/src/lib/csv.ts 的 parseZCsv 完全兼容）：
//   # lcr-dataset=one-port-z
//   # schema=lcr-z-csv-v1
//   # protocol=1
//   # firmware=<git/version>
//   # calibration_id=<id>
//   # drive_vrms=<value>
//   f,re,im
//   100.0,12.34,-45.67
// f 使用 actualHz；re/im 单位 ohm；首版固定 3 列。
//
// 双端口 CSV v1：
//   # lcr-dataset=two-port-h
//   # schema=lcr-h-csv-v1
//   ...（同上头部）
//   f,re_h,im_h
// 真源是复数 H；Bode gain/phase 由网站从复 H 推导。
// ============================================================================

#pragma once

#include "fw_version.h"
#include "measurement_types.h"

#include <stdint.h>
#include <stddef.h>

// 标准循环冗余校验：CRC-32/ISO-HDLC（poly 0xEDB88320 反射、init/xorout
// 0xFFFFFFFF）——与 zlib / 前端 crc32.ts 位位一致（host+vitest 双侧锁定）。
uint32_t crc32Update(uint32_t state, const uint8_t* data, size_t len);
uint32_t crc32Final(uint32_t state);
uint32_t crc32Of(const uint8_t* data, size_t len);

// ---------------------------------------------------------------------------
// 失败点诊断（不进拟合 CSV，但完整保留）
// ---------------------------------------------------------------------------
struct SweepFailure {
    double requestedHz;
    MeasurementStatus status;
};

// canonical CSV 缓冲尺寸：257 行 × ≤40 字符 + 头部 + 余量
static constexpr size_t ONEPORT_CSV_MAX = 12 * 1024;
static constexpr size_t TWOPORT_CSV_MAX = 12 * 1024;
static constexpr size_t METADATA_JSON_MAX = 512;

struct DatasetDiag {
    uint16_t plannedPoints;      // 频率表点数
    uint16_t validPoints;        // 进入 CSV 的点数
    uint16_t failedPoints;       // 失败点数（= planned - valid - skipped?）
    SweepFailure failures[SWEEP_MAX_POINTS];
};

// ---------------------------------------------------------------------------
// 数据集（静态存储；RAM 预算见 README）
// ---------------------------------------------------------------------------
struct OnePortDataset {
    MeasurementKind kind;                 // OnePortImpedance
    OnePortPoint points[SWEEP_MAX_POINTS];
    uint16_t nPoints;                     // 有效点数
    DatasetDiag diag;
    // seal 产物
    char csv[ONEPORT_CSV_MAX];            // canonical CSV（含头部，NUL 结尾）
    uint32_t csvLen;
    uint32_t crc32;
    uint32_t sessionId;
    char calibrationId[32];
    double driveVrms;                     // ExcitationState.actualVrms
    uint64_t sealedUnixMs;                // 封存时刻（uptime ms）
    bool sealed;
};

struct TwoPortDataset {
    MeasurementKind kind;                 // TwoPortTransfer
    TwoPortPoint points[SWEEP_MAX_POINTS];
    uint16_t nPoints;
    DatasetDiag diag;
    char csv[TWOPORT_CSV_MAX];
    uint32_t csvLen;
    uint32_t crc32;
    uint32_t sessionId;
    char calibrationId[32];
    double driveVrms;
    uint64_t sealedUnixMs;
    bool sealed;
};

// 数值格式：%.6g 保证 ≥6 位有效数字且行宽可控（≤13 字符/字段）
size_t formatOnePortCsv(const OnePortPoint* pts, uint16_t n,
                         const char* calibId, double driveVrms,
                         char* out, size_t cap);
size_t formatTwoPortCsv(const TwoPortPoint* pts, uint16_t n,
                         const char* calibId, double driveVrms,
                         char* out, size_t cap);

// 生成 metadata JSON（UTF-8，字段固定，plan.md §6.2）：
// {"protocol":1,"firmware":"...","session_id":...,"dataset_kind":"ONE_PORT_Z",
//  "schema":"lcr-z-csv-v1","point_count":N,"byte_count":N,"crc32":"A1B2C3D4",
//  "calibration_id":"..."}
size_t formatMetadataJson(MeasurementKind kind, uint32_t sessionId,
                          uint16_t pointCount, uint32_t byteCount,
                          uint32_t crc32Value, const char* calibId,
                          char* out, size_t cap);
