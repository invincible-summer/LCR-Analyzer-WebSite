// ============================================================================
// dataset.h —— 封存数据集 + canonical CSV + CRC32（host 可编译）
// ----------------------------------------------------------------------------
// 语义（plan.md §5.4/§9）：
//   * 全部 chunk 测量结束、StopTone（lcr_api_set_freq(0)）完成、measurement
//     lock 解除之后 dataset 才能 seal；seal 后数据不可变；
//   * 单口 CSV 的 f/re/im 直接使用 DNT API 的 f_act/z_re/z_im，
//     禁止改用 requestedHz；失败点不伪造为 0、不进入拟合 CSV（诊断保留）；
//   * 双口 CSV 的 re_h/im_h 只由 API magnitude/phase 纯数学换算得到；
//   * v2 元数据诚实化：不再杜撰 drive_vrms=1.05 / calibration_id=
//     factory-none（那是旧外部 DAC/自定义校准架构的遗留），改为
//     measurement_backend=DO_NOT_TOUCH_lcr_api 与 calibration_state=
//     （lcr_api_cal_status 的真实摘要；双口为 raw_w_path）。
//
// CSV v2（与 frontend csv.ts / twoPortCsv.ts 兼容，v1 历史文件仍可读）：
//   # lcr-dataset=one-port-z
//   # schema=lcr-z-csv-v2
//   # protocol=1
//   # firmware=<版本>
//   # measurement_backend=DO_NOT_TOUCH_lcr_api
//   # calibration_state=cal:3/10,open:ok,short:--
//   f,re,im
//   <f_act>,<z_re>,<z_im>
// ============================================================================

#pragma once

#include "fw_version.h"
#include "measurement_types.h"

#include <stddef.h>
#include <stdint.h>

// 标准循环冗余校验：CRC-32/ISO-HDLC（poly 0xEDB88320 反射、init/xorout
// 0xFFFFFFFF）——与 zlib / 前端 crc32.ts 位位一致（host+vitest 双侧锁定）。
uint32_t crc32Update(uint32_t state, const uint8_t* data, size_t len);
uint32_t crc32Final(uint32_t state);
uint32_t crc32Of(const uint8_t* data, size_t len);

// ---------------------------------------------------------------------------
// 有效点记录（只存进 CSV 的行；失败点在 DatasetDiag 中完整保留）
// ---------------------------------------------------------------------------
struct ZPointRec {
    double f;     // DNT f_act（拟合频率唯一真源）
    double re;    // DNT z_re（欧姆）
    double im;    // DNT z_im（欧姆）
};

struct HPointRec {
    double f;     // DNT f_act
    double reH;   // = hMag*cos(phaseRad)（纯数学换算）
    double imH;   // = hMag*sin(phaseRad)（纯数学换算）
};

// 失败点诊断（不进拟合 CSV，但完整保留；apiStatus 为 DNT 错误码）
struct SweepFailureRec {
    double requestedHz;
    int apiStatus;
};

// ---------------------------------------------------------------------------
// 失败点诊断（不进拟合 CSV，但完整保留）
// ---------------------------------------------------------------------------
struct DatasetDiag {
    uint16_t plannedPoints;      // 频率表点数
    uint16_t validPoints;        // 进入 CSV 的点数
    uint16_t failedPoints;       // 失败点数
    SweepFailureRec failures[SWEEP_MAX_POINTS];
};

// canonical CSV 缓冲尺寸：257 行 × ≤40 字符 + 头部 + 余量
static constexpr size_t ONEPORT_CSV_MAX = 12 * 1024;
static constexpr size_t TWOPORT_CSV_MAX = 12 * 1024;
static constexpr size_t METADATA_JSON_MAX = 512;
static constexpr size_t CAL_STATE_MAX = 48;
static constexpr size_t BACKEND_NAME_MAX = 40;

// 数据集（静态存储；seal 之后不可变）
struct OnePortDataset {
    MeasurementKind kind;                 // OnePortImpedance
    ZPointRec points[SWEEP_MAX_POINTS];
    uint16_t nPoints;                     // 有效点数
    DatasetDiag diag;
    // seal 产物
    char csv[ONEPORT_CSV_MAX];            // canonical CSV（含头部，NUL 结尾）
    uint32_t csvLen;
    uint32_t crc32;                       // 对 csv 前 csvLen 字节的 CRC32
    uint32_t sessionId;
    char calibrationState[CAL_STATE_MAX]; // lcr_api_cal_status 真实摘要
    char measurementBackend[BACKEND_NAME_MAX];
    uint64_t sealedUnixMs;                // 封存时刻（uptime ms）
    bool sealed;
};

struct TwoPortDataset {
    MeasurementKind kind;                 // TwoPortTransfer
    HPointRec points[SWEEP_MAX_POINTS];
    uint16_t nPoints;
    DatasetDiag diag;
    char csv[TWOPORT_CSV_MAX];
    uint32_t csvLen;
    uint32_t crc32;
    uint32_t sessionId;
    char calibrationState[CAL_STATE_MAX]; // 双口 W 链为 raw/no calib -> raw_w_path
    char measurementBackend[BACKEND_NAME_MAX];
    uint64_t sealedUnixMs;
    bool sealed;
};

// 校准状态摘要 -> 头部字符串："cal:3/10,open:ok,short:--"
size_t formatCalStateString(uint8_t rangesValid, bool openValid, bool shortValid,
                            char* out, size_t cap);

// 数值格式：%.6g 保证 ≥6 位有效数字且行宽可控
size_t formatOnePortCsv(const ZPointRec* pts, uint16_t n,
                        const char* calState, char* out, size_t cap);
size_t formatTwoPortCsv(const HPointRec* pts, uint16_t n,
                        const char* calState, char* out, size_t cap);

// metadata JSON v2（UTF-8，字段固定）：
// {"protocol":1,"firmware":"...","session_id":...,"dataset_kind":"ONE_PORT_Z",
//  "schema":"lcr-z-csv-v2","point_count":N,"byte_count":N,"crc32":"A1B2C3D4",
//  "measurement_backend":"DO_NOT_TOUCH_lcr_api","calibration_state":"..."}
size_t formatMetadataJson(MeasurementKind kind, uint32_t sessionId,
                          uint16_t pointCount, uint32_t byteCount,
                          uint32_t crc32Value, const char* calState,
                          char* out, size_t cap);
