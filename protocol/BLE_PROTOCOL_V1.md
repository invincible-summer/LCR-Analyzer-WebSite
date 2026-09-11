# BLE GATT 协议 v1

> 适用固件：v4.1.0（`LCR_BLE_PROTOCOL_VERSION 1`，见 `ino/LCR_UI/fw_version.h`）。
> 真源实现：固件 `ino/LCR_UI/{ble_protocol.h, radio_manager.cpp}`；前端
> `frontend/src/lib/ble/{protocol.ts, lcrDevice.ts}`。任何改变本协议语义的
> 提交必须 bump 版本，而不是悄悄改字段。

## 1. 架构与射频互斥

- BLE 是设备到浏览器的**唯一**无线通道（Classic `BluetoothSerial` 已删除，
  生产源码引用数为 0，CI 静态门禁锁定）。Wi-Fi 不使用。
- 固件**启动时不初始化 BLE**。只有当一次扫频完成、激励停止、ADC/DMA
  释放、数据集封存（seal）之后，用户在设备上确认才开启 BLE advertising。
- 测量窗口（`MeasurementEngine PREPARE..RESULT_READY`）内 `RadioState`
  必须为 Off：该 invariant 由 `radio_lock` 承载，debug 构建轮询断言，
  host 状态机单测覆盖（`ino/test/test_engines.cpp`）。
- 已连接 BLE 时用户开始新测量：先 `disconnect → BLE deinit → radio-off
  confirmed`，然后才允许启动采集。
- 传输失败绝不回头修改测量值：封存数据集不可变，重传必然逐字节一致。

## 2. GATT 服务

固定 128-bit UUID（不随文件/分支改变）：

```
Service:  6e6f0001-5f31-4c43-a001-6c63722d7631
Control:  6e6f0002-5f31-4c43-a001-6c63722d7631   WRITE WITH RESPONSE
Status:   6e6f0003-5f31-4c43-a001-6c63722d7631   READ + NOTIFY
Metadata: 6e6f0004-5f31-4c43-a001-6c63722d7631   READ
Data:     6e6f0005-5f31-4c43-a001-6c63722d7631   NOTIFY
```

设备广播名：`LCR-Analyzer`；广播包含 Service UUID（浏览器按此过滤）。

## 3. Metadata（READ，UTF-8 JSON）

字段固定（v2 起以 `calibration_state`/`measurement_backend` 取代 v1 杜撰的
`calibration_id`；浏览器解析器 v1/v2 均接受，见 `frontend/src/lib/ble/protocol.ts`）：

```json
{
  "protocol": 1,
  "firmware": "4.1.0",
  "session_id": 12345678,
  "dataset_kind": "ONE_PORT_Z",
  "schema": "lcr-z-csv-v2",
  "point_count": 121,
  "byte_count": 4812,
  "crc32": "A1B2C3D4",
  "measurement_backend": "DO_NOT_TOUCH_lcr_api",
  "calibration_state": "cal:3/10,open:ok,short:--"
}
```

- `calibration_state` 为固件用 `lcr_api_cal_status()` 得到的真实校准摘要
  （不杜撰唯一校准 ID）；双端口 W 链在测量核心中为 raw/no-calib，
  固定为 `raw_w_path`。
- `dataset_kind` ∈ {`ONE_PORT_Z`, `TWO_PORT_H`}。
- `crc32` 为 Data 特征将要传输的**整份 CSV 字节流**的 CRC-32/ISO-HDLC
  （zlib 兼容；8 位十六进制大写）。
- `protocol` ≠ 1 时前端必须拒绝并提示升级固件。

## 4. Control（WRITE WITH RESPONSE，定长命令字节）

| 字节 | 含义 |
|---|---|
| 0x01 | START_TRANSFER（从 seq 0 开始发送） |
| 0x02 | RESTART_TRANSFER（断线/seq gap/CRC 错误后整份重发） |
| 0x03 | ABORT_TRANSFER（停止发送，保持连接） |
| 0x04 | GET_STATUS（请求一次 Status notify） |

v1 不在 MCU 上解析 JSON；未知命令回错误码 2 的 Status。

## 5. Status（READ + NOTIFY，15 字节）

```
offset  size  field
0       1     protocol = 1
1       1     state（0 Off /1 Starting /2 Advertising /3 Connected /4 Sending
                 /5 Stopping /6 Error）
2       1     error code（0 无 /2 未知命令 /3 未连接即传输 /4 编码失败）
3       4     session id，uint32 little-endian
7       4     bytes sent，uint32 little-endian
11      4     bytes total，uint32 little-endian
```

## 6. Data（NOTIFY，分帧 CSV）

帧布局（小端）：

```
offset  size  field
0       2     magic = ASCII 'L','C'
2       1     protocol = 1
3       1     dataset kind（0 = ONE_PORT_Z，1 = TWO_PORT_H）
4       2     seq，uint16 little-endian（从 0 连续递增）
6       2     payload_len，uint16 little-endian
8       N     CSV 字节（N ≤ ATT payload − 8）
```

- 通知负载按协商 MTU 自适应（`onMtuChanged` → ATT payload = MTU − 3，
  单帧 CSV 上限 128 字节）；未协商时保守按 MTU 23 → 12 字节 CSV/帧。
- 浏览器重组时校验：seq 连续、总 `byte_count`、整份 CRC32。
- **v1 无复杂重传**：seq gap / 断线 / CRC 错误 → 前端重连并写
  `RESTART_TRANSFER`，设备从 seq 0 重发整份封存数据集。
- 传输完成后设备保持数据集不变，直到用户开始新测量、超时清理或重启。

## 7. 前端接收流程（Web Bluetooth）

1. 用户点击触发 `navigator.bluetooth.requestDevice({filters:[{services:[UUID]}]})`；
2. GATT connect → 读 Metadata（版本/schema 校验）；
3. 订阅 Status/Data notifications → 写 `START_TRANSFER`；
4. 按 seq 重组字节流 → `byte_count` + CRC32 校验；
5. `TextDecoder('utf-8')` 得到 CSV 文本；
6. 单端口：喂给现有 `parseZCsv()` → `loadPoints()` → WASM 拟合
   （与文件上传完全相同的数据路径，无第二套拟合格式）；
   双端口：`parseHCsv()` → 复 H 曲线（Bode/phase/Nyquist 由复 H 推导）；
7. “保存收到的 CSV”下载的是通过 CRC 校验后的原始字节。

浏览器要求：桌面 Chrome/Edge + secure context（HTTPS 或 localhost）。
不支持时 UI 显示专门的功能不可用提示，而不是普通连接失败。

## 8. 兼容性规则

改变以下任何一项都必须 bump `LCR_BLE_PROTOCOL_VERSION`（并同时更新本文档、
固件 `fw_version.h`、前端 `protocol.ts` 与双侧测试）：

- `Z = V/I`、`H = Vout/Vin` 定义或 phase 正方向；
- CSV 单位或列语义（属 schema version，与协议版本联动声明）；
- BLE frame layout、CRC 覆盖范围；
- 数据集完成/封存语义（何时允许开射频、何时可重传）。
