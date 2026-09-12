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

uint32_t crc32Update(uint32_t state, const uint8_t* data, size_t len);
uint32_t crc32Final(uint32_t state);
uint32_t crc32Of(const uint8_t* data, size_t len);

struct ZPointRec {
    double f;
    double re;
    double im;
};

struct HPointRec {
    double f;
    double reH;
    double imH;
};

struct SweepFailureRec {
    double requestedHz;
    int apiStatus;
};

struct DatasetDiag {
    uint16_t plannedPoints;
    uint16_t validPoints;
    uint16_t failedPoints;
    SweepFailureRec failures[SWEEP_MAX_POINTS];
};

static constexpr size_t ONEPORT_CSV_MAX = 12 * 1024;
static constexpr size_t TWOPORT_CSV_MAX = 12 * 1024;
static constexpr size_t METADATA_JSON_MAX = 512;
static constexpr size_t CAL_STATE_MAX = 48;
static constexpr size_t BACKEND_NAME_MAX = 40;

struct OnePortDataset {
    MeasurementKind kind;
    ZPointRec points[SWEEP_MAX_POINTS];
    uint16_t nPoints;
    DatasetDiag diag;
    char csv[ONEPORT_CSV_MAX];
    uint32_t csvLen;
    uint32_t crc32;
    uint32_t sessionId;
    char calibrationState[CAL_STATE_MAX];
    char measurementBackend[BACKEND_NAME_MAX];
    // Arduino millis()/SweepEngine nowMs based monotonic uptime, NOT Unix epoch.
    // Kept internal (not serialized into metadata); uint32_t matches millis wrap.
    uint32_t sealedUptimeMs;
    bool sealed;
};

struct TwoPortDataset {
    MeasurementKind kind;
    HPointRec points[SWEEP_MAX_POINTS];
    uint16_t nPoints;
    DatasetDiag diag;
    char csv[TWOPORT_CSV_MAX];
    uint32_t csvLen;
    uint32_t crc32;
    uint32_t sessionId;
    char calibrationState[CAL_STATE_MAX];
    char measurementBackend[BACKEND_NAME_MAX];
    uint32_t sealedUptimeMs;
    bool sealed;
};

size_t formatCalStateString(uint8_t rangesValid, bool openValid, bool shortValid,
                            char* out, size_t cap);

size_t formatOnePortCsv(const ZPointRec* pts, uint16_t n,
                        const char* calState, char* out, size_t cap);
size_t formatTwoPortCsv(const HPointRec* pts, uint16_t n,
                        const char* calState, char* out, size_t cap);

size_t formatMetadataJson(MeasurementKind kind, uint32_t sessionId,
                          uint16_t pointCount, uint32_t byteCount,
                          uint32_t crc32Value, const char* calState,
                          char* out, size_t cap);
