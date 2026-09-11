# CSV 数据模式 v2（设备 → 网站）

> 适用固件：v4.1.0（`LCR_Z_CSV_SCHEMA_VERSION 2` / `LCR_H_CSV_SCHEMA_VERSION 2`，
> 见 `ino/LCR_UI/fw_version.h`）。真源实现：固件 `ino/LCR_UI/dataset.cpp`
> （`formatOnePortCsv` / `formatTwoPortCsv`）；前端 `frontend/src/lib/csv.ts`
> （`parseZCsv`）与 `frontend/src/lib/twoPortCsv.ts`（`parseHCsv`）。
> v2 相对 v1 的变化只有**头部元数据键**：数据列仍为 3 列，数字语义不变，
> 拟合核心（Try1/2/3）与 Bode/Nyquist 推导不受影响。v1 历史文件仍可读
> （parser 同时接受 v1/v2，见 `CSV_SCHEMA_V1.md`）。

## 1. 为什么 bump 到 v2

v1 头部的 `calibration_id=factory-none` 与 `drive_vrms=1.05` 来自旧的
外部 DAC / 自定义校准架构，是不真实的杜撰元数据。v4.1.0 起测量链全部
来自已验证的 `DO_NOT_TOUCH_lcr_api`，规则变为：

> **DNT API 没有暴露的硬件量，应用层不得杜撰。**

因此 v2：

- 删除 `drive_vrms`（API 未暴露激励幅度）；
- 删除 `calibration_id`（API 没有校准 profile ID）；
- 新增 `measurement_backend=DO_NOT_TOUCH_lcr_api`（测量后端标识）；
- 新增 `calibration_state`（`lcr_api_cal_status()` 的真实摘要）。

## 2. 单端口 `lcr-z-csv-v2`

```csv
# lcr-dataset=one-port-z
# schema=lcr-z-csv-v2
# protocol=1
# firmware=4.1.0
# measurement_backend=DO_NOT_TOUCH_lcr_api
# calibration_state=cal:3/10,open:ok,short:--
f,re,im
100.0,12.34,-45.67
...
```

规则（与 v1 相同的部分不再重复，见 `CSV_SCHEMA_V1.md`）：

- `f` **必须使用 DNT 返回的 `f_act`**（实际频率），`f_req` 仅用于诊断；
- `re`/`im` 直接来自 `lcr_api_measure_z / lcr_api_sweep_z` 的 `z_re/z_im`
  （测量链校准已在 DNT 内完成，应用层**不做二次校准**）；
- 失败点（API 错误 / NaN）不伪造为 0、不进入 CSV；
- `calibration_state` 格式：`cal:<N>/10,open:<ok|-->,short:<ok|-->`
  （N = 10 个 TIA 量程中已校准的个数；由固件在会话开始时读取）。

网站接收端不变：`parseZCsv(text)` → `ZPoint[]` → `loadPoints()` → WASM
拟合。BLE 导入与文件上传在解析层汇合，没有第二套拟合格式。

## 3. 双端口 `lcr-h-csv-v2`

```csv
# lcr-dataset=two-port-h
# schema=lcr-h-csv-v2
# protocol=1
# firmware=4.1.0
# measurement_backend=DO_NOT_TOUCH_lcr_api
# calibration_state=raw_w_path
f,re_h,im_h
100.0,0.923,-0.146
...
```

- 真源是复数传递函数 `H = Vout/Vin`；`re_h/im_h` 由 DNT 的
  `h_mag/phase_deg` 纯数学换算：`re=|H|cos(φ)`, `im=|H|sin(φ)`；
- **`calibration_state=raw_w_path` 是诚实声明**：DNT 的双端口 W 链为
  raw chain / no calib（源码注释明确），不得复用单端口校准状态暗示
  双端口已校准，也不得在应用层自行补一套 H 校准（plan.md §8.1）；
- 若未来硬件测量核心提供正式的双端口校准 API，再 bump v3。

## 4. 头部注释键（v2 全集）

| 键 | 含义 |
|---|---|
| `lcr-dataset` | `one-port-z` / `two-port-h` |
| `schema` | `lcr-z-csv-v2` / `lcr-h-csv-v2` |
| `protocol` | BLE GATT 协议版本（1） |
| `firmware` | 固件版本（`LCR_FW_VERSION`） |
| `measurement_backend` | `DO_NOT_TOUCH_lcr_api`（测量后端标识） |
| `calibration_state` | 真实校准摘要（见上） |

BLE Metadata 特征同步升级（`measurement_backend` / `calibration_state`
字段，见 `protocol/BLE_PROTOCOL_V1.md` §3）；`byte_count + CRC32` 覆盖
exact CSV bytes 的可追溯性契约不变。

## 5. 兼容矩阵

| 数据 | v1 文件 | v2 文件 |
|---|---|---|
| parseZCsv（单端口） | ✅ | ✅（同 ZPoint[]） |
| parseHCsv（双端口） | ✅ | ✅（同 HPoint[]） |
| WASM 拟合 / Bode/Nyquist | 不变 | 不变 |
| BLE Metadata | 含 calibration_id | 含 calibration_state（v1 字段可选） |

golden fixtures（`ino/tools/run_tests.sh` 生成，CI 双侧锁定）：
`frontend/src/lib/__tests__/fixtures/golden_oneport.csv`（v2）与
`golden_twoport.csv`（v2）。
