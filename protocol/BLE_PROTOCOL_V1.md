# BLE GATT 协议 v1

> 适用固件：v4.1.0（`LCR_BLE_PROTOCOL_VERSION 1`，见 `ino/LCR_UI/fw_version.h`）。
> 真源实现：固件 `ino/LCR_UI/{ble_protocol.h, radio_manager.cpp}`；前端
> `frontend/src/lib/ble/{protocol.ts, lcrDevice.ts}`。任何改变本协议**线格式**的
> 提交必须 bump 版本，而不是悄悄改字段。本文新增的 MTU/自动重试规则不改变
> v1 帧格式，因此仍保持 protocol 1。

## 1. 架构与射频互斥

- BLE 是设备到浏览器的**唯一**无线通道（Classic `BluetoothSerial` 已删除，
  生产源码引用数为 0，CI 静态门禁锁定）。Wi-Fi 不使用。
- 固件**启动时不初始化 BLE**。只有当一次扫频完成、激励停止、ADC/DMA
  释放、数据集封存（seal）之后，用户在设备上确认才开启 BLE advertising。
- 测量窗口内 `RadioState` 必须为 Off：该 invariant 由 `radio_lock` 承载。
- 已连接 BLE 时用户开始新测量：先断开并完成 BLE deinit，再允许采集。
- 传输失败绝不回头修改测量值：sealed dataset 不可变，重传逐字节一致。

## 2. GATT 服务

固定 128-bit UUID：

```text
Service:  6e6f0001-5f31-4c43-a001-6c63722d7631
Control:  6e6f0002-5f31-4c43-a001-6c63722d7631   WRITE WITH RESPONSE
Status:   6e6f0003-5f31-4c43-a001-6c63722d7631   READ + NOTIFY
Metadata: 6e6f0004-5f31-4c43-a001-6c63722d7631   READ
Data:     6e6f0005-5f31-4c43-a001-6c63722d7631   NOTIFY
```

设备广播名：`LCR-Analyzer`；广播包含 Service UUID。

## 3. Metadata（READ，UTF-8 JSON）

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

- `dataset_kind` ∈ {`ONE_PORT_Z`, `TWO_PORT_H`}。
- `crc32` 覆盖 Data 将传输的**整份 CSV 原始字节流**。
- `protocol != 1` 时前端拒绝。
- `byte_count` 必须是正安全整数，`point_count` 必须是非负安全整数。
- 双端口 W 链的 `calibration_state` 为 `raw_w_path`。

## 4. Control（WRITE WITH RESPONSE）

| 字节 | 含义 |
|---|---|
| 0x01 | START_TRANSFER：从 seq 0 开始 |
| 0x02 | RESTART_TRANSFER：从 seq 0 重发整份 sealed dataset |
| 0x03 | ABORT_TRANSFER：停止当前流，保持连接 |
| 0x04 | GET_STATUS：请求一次 Status |

浏览器在 seq gap、坏帧、设备发送错误、CRC 失败或停滞时自动执行
`ABORT_TRANSFER -> drain -> RESTART_TRANSFER`，用户不需要手动重复点击。

## 5. Status（READ + NOTIFY，15 字节）

```text
offset  size  field
0       1     protocol = 1
1       1     state（0 Off /1 Starting /2 Advertising /3 Connected /4 Sending
                 /5 Stopping /6 Error）
2       1     error code
3       4     session id，uint32 little-endian
7       4     bytes sent，uint32 little-endian
11      4     bytes total，uint32 little-endian
```

错误码：0 无；1 BLE 初始化失败；2 未知命令；3 未连接即传输；4 帧编码失败；
5 Data notify 本地提交/transport 失败。Data characteristic 的底层 status callback
出现非 `SUCCESS_NOTIFY` 时，固件把 error 5 通过 atomic mailbox 发布给主 loop；
即使是最后一帧排队后才收到失败 callback，也不得把传输误认为成功。

## 6. Data（NOTIFY，自动分帧 CSV）与上限

```text
offset  size  field
0       2     magic = ASCII 'L','C'
2       1     protocol = 1
3       1     dataset kind（0 = ONE_PORT_Z，1 = TWO_PORT_H）
4       2     seq，uint16 little-endian（modulo 2^16）
6       2     payload_len，uint16 little-endian
8       N     CSV bytes
```

BLE **有单次 notification 上限**，但不是“整份文件上限”。ATT notification 的
characteristic value 最大为 `negotiated ATT_MTU - 3`。ESP32 默认 MTU=23，
Arduino-ESP32/ESP-IDF 可配置至 517，但实际值由双方协商且可能小于请求值。

本项目固定策略：

- 未协商时 ATT value=20 B；扣除 8 B LCR header 后每帧 **12 B CSV**。
- 固件请求 preferred MTU=185；只有当前连接 `onMtuChanged` 到达后才扩大。
- 无论 MTU 多大，CSV payload 最多 **128 B/frame**，Data notification 总长最多
  136 B；更大 dataset 自动连续分片。
- 每个 `radio.poll()` 最多发一帧，帧间至少 **15 ms**，避免压满 BLE
  host/controller 与浏览器 notification 队列。
- `seq` 按 uint16 modulo 65536 连续，`65535 -> 0` 合法；前后端一致。
- Status 的总字节计数使用 uint32。
- 当前 One-Port/Two-Port CSV 静态 buffer 各 **12 KiB**，所以当前产品的实际
  dataset 上限早于 BLE seq/uint32 边界。

满 12 KiB 数据集的纯软件 pacing 量级：默认 MTU23 时 1024 帧，15 ms 间隔约
15.36 s；能使用 128 B CSV/frame 时 96 帧，约 1.44 s。实际总时间还受连接
interval、浏览器调度和 RF 环境影响。

## 7. 稳定传输与自动恢复

恢复有三层，全部重发同一份 immutable sealed dataset：

1. **同一 GATT 连接内自动重传**：seq gap、非法/截断帧、kind/protocol 不匹配、
   设备 Status error、收到字节超过 `byte_count`、CRC32 错误或连续 15 s 无新增
   有效字节时，前端写 ABORT，等待 120 ms 排空旧 notification，再 RESTART。
   一个连接内最多 4 个 attempt（1 START + 3 RESTART）。
2. **同连接 attempt 全部失败时重建 GATT**：如果连续 4 个 attempt 仍失败，
   即使浏览器仍认为 `gatt.connected=true`，也主动 disconnect，然后用已经授权的
   `BluetoothDevice.gatt.connect()` 重连。这样可以清理卡住的 ATT/GATT 状态，
   不需要再次弹 chooser。
3. **真实 GATT 断线自动重连**：物理/浏览器断线走同一个 reconnect 路径。
   整个 `receiveDataset()` 最多使用 2 次 reconnect；每次重连后重新读 Metadata、
   重新订阅 Status/Data，并从 seq 0 开始。

每轮 RESTART 只接受新的 `seq=0` 作为流起点；在此之前到达的残留 Data frame
直接忽略。最终只有同时满足以下条件才把 CSV 交给 parser/拟合：

- protocol / dataset kind / session status 合法；
- seq 连续（包含 uint16 wrap）；
- `bytesReceived == metadata.byte_count`；
- `CRC32(full CSV bytes) == metadata.crc32`。

内部 assembler 重传时 reset；UI progress 使用历史最大接收量，避免可见进度
从 80% 倒退到 0%。如果两次 GATT reconnect 也无法恢复，才向用户暴露最终错误。

## 8. 前端接收流程（Web Bluetooth）

1. 用户手势触发 `requestDevice()`；
2. GATT connect → 读 Metadata；
3. 订阅 Status/Data → START；
4. MTU-aware 多帧接收、seq 重组；
5. 异常按 §7 自动 retry/reconnect；
6. byte_count + CRC32 最终校验；
7. `TextDecoder('utf-8')` 还原 CSV；
8. One-Port 进入 `parseZCsv()`/拟合；Two-Port 进入 `parseHCsv()`/曲线；
9. 保存功能使用 CRC 通过后的原始字节。

浏览器要求：桌面 Chrome/Edge + HTTPS/localhost。

## 9. 兼容性规则

以下变化必须 bump `LCR_BLE_PROTOCOL_VERSION`：

- Z/H 定义或 phase 正方向；
- CSV 列/单位语义与协议联动改变；
- BLE frame layout、CRC 覆盖范围；
- seal/完成语义发生不兼容变化。

以下属于 v1 兼容 transport policy，不要求 bump：preferred MTU、payload cap、pacing、
retry/reconnect 次数与超时，只要线格式、UUID 和 sealed dataset 语义保持不变。
