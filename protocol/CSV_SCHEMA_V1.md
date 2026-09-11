# CSV 数据模式 v1（设备 → 网站）

> 适用固件：v4.1.0（`LCR_Z_CSV_SCHEMA_VERSION 1` / `LCR_H_CSV_SCHEMA_VERSION 1`，
> 见 `ino/LCR_UI/fw_version.h`）。真源实现：固件 `ino/LCR_UI/dataset.cpp`
> （`formatOnePortCsv` / `formatTwoPortCsv`）；前端 `frontend/src/lib/csv.ts`
> （`parseZCsv`）与 `frontend/src/lib/twoPortCsv.ts`（`parseHCsv`）。
> 任何改变字段语义的提交必须 bump 对应 schema version。

## 1. 单端口 `lcr-z-csv-v1`

设备上传的不是原始波形，而是网站拟合真正需要的复阻抗点：

```csv
# lcr-dataset=one-port-z
# schema=lcr-z-csv-v1
# protocol=1
# firmware=4.1.0
# calibration_id=factory-none
# drive_vrms=1.05
f,re,im
100.0,12.34,-45.67
...
```

规则：

- `f` **必须使用 actualHz**（激励的精确实际频率，由 I2S 有理数构造回读），
  `requestedHz` 仅用于诊断；
- `re`/`im` 单位为欧姆，`Z = V_DUT / I_DUT`（`V`、`I` 已经过 ADC 校准 +
  前端复校准层）；
- 数值格式 `% .6g`（≥6 位有效数字）；首版固定 3 列；
- **失败点不伪造为 0、不进入 CSV**：错误保留在设备 DatasetDiag 中；
- 只有 `status == Ok` 且数值有限的行会写出；
- 行序为频率升序（对数扫描的自然产物）。

网站接收端：`parseZCsv(text)` → `ZPoint[]` → `loadPoints()` → 现有 WASM
拟合。手工文件导入与 BLE 导入在解析层汇合，**不存在第二套拟合格式**。
双向兼容由 golden fixture 锁定：固件 host 测试产物
`frontend/src/lib/__tests__/fixtures/golden_oneport.csv` 喂给
`parseZCsv` 断言逐点一致（`deviceImport.test.ts`）。

### 升级为 6 列（预留）

后续若测量协方差链完成，可升级为 parser 已支持的
`f,re,im,cov_rr,cov_ri,cov_ii`（GLS 逐点白化）。届时 **schema version
必须 bump 到 v2 并在头部声明**。

## 2. 双端口 `lcr-h-csv-v1`

```csv
# lcr-dataset=two-port-h
# schema=lcr-h-csv-v1
# protocol=1
# firmware=4.1.0
# calibration_id=factory-none
# drive_vrms=1.05
f,re_h,im_h
100.0,0.923,-0.146
...
```

- 真源是**复数传递函数** `H = Vout / Vin`（统一约定：
  `gainDb = 20·log10|H|`，`phase = arg(H) = arg(Vout) − arg(Vin)`）；
- 设备只传复 H；Bode gain、phase 与 Nyquist 全部由网站从 `re_h/im_h`
  推导，不传一组可能符号不一致的派生量；
- 旧固件的 `-20·log10(out/in)` 倒数语义已删除，业务路径无引用。

## 3. 头部注释键

| 键 | 含义 |
|---|---|
| `lcr-dataset` | `one-port-z` / `two-port-h` |
| `schema` | `lcr-z-csv-v1` / `lcr-h-csv-v1` |
| `protocol` | BLE GATT 协议版本（1） |
| `firmware` | 固件版本（`LCR_FW_VERSION`） |
| `calibration_id` | 校准 profile 标识（`factory-none` = 未做前端校准） |
| `drive_vrms` | calibrated nominal drive（实际激励幅度名义值） |

所有头部行以 `#` 开始；两套 parser 均容忍 `#` 注释与非数值表头行。

## 4. 可追溯性

每个数据集带 `firmware/protocol/schema/calibration_id`（头部 + BLE
Metadata），传输带 `byte_count + CRC32`。任何拟合结果都可以追溯回一份
保存的 CSV（页面“导出 CSV”/“保存设备 CSV”下载的即通过 CRC 校验的原始
字节）。
