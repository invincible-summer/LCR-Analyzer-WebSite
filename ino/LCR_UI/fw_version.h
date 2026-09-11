// ============================================================================
// fw_version.h —— 固件 / 协议 / 数据模式版本（单一出处）
// ----------------------------------------------------------------------------
// 任何改变下列语义的提交必须 bump 对应版本号，而不是悄悄改字段：
//   * Z = V/I 定义、H = Vout/Vin 定义、phase 正方向、CSV 单位
//   * BLE frame layout、CRC 覆盖范围、数据集完成/封存语义
// 版本号契约详见 protocol/BLE_PROTOCOL_V1.md 与 protocol/CSV_SCHEMA_V1.md。
// ============================================================================

#pragma once

#define LCR_FW_VERSION "4.1.0"
#define LCR_BLE_PROTOCOL_VERSION 1
#define LCR_Z_CSV_SCHEMA_VERSION 1
#define LCR_H_CSV_SCHEMA_VERSION 1
