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
- 测量窗口（`MeasurementEngine PREPARE..RESULT_READY`）内 `RadioState`
  必须为 Off：该 invariant 由 `radio_lock` 承载，debug 构建轮询断言，
  host 状态机单测覆盖。
- 已连接 BLE 时用户开始新测量：先 `disconnect → BLE deinit → radio-off
  confirmed`，然后才允许启动采集。
- 传输失败绝不回头修改测量值：封存数据集不可变，重传必然逐字节一致。

## 2. GATT 服务

固定 128-bit UUID（不随文件/分支改变）：

```text
Service:  6e6f0001-5f31-4c43-a001-6c63722d7631
Control:  6e6f0002-5f31-4c43-a001-6c63722d7631   WRITE WITH RESPONSE
Status:   6e6f0003-5f31-4c43-a001-6c63722d7631   READ + NOTIFY
Metadata: 6e6f0004-5f31-4c43-a001-6c63722d7631   READ
Data:     6e6f0005-5f31-4c43-a001-6c63722d7631   NOTIFY
```

设备广播名：`LCR-Analyzer`；广播包含 Service UUID（浏览器按此过滤）。

## 3. Metadata（READ，UTF-8 JSON）

字段固定（v2 起以 `calibration_state`/`measurement_backend` 取代历史的
`calibration_id`；浏览器解析器 v1/v2 均接受）：

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

- `calibration_state` 为固件用 `lcr_api_cal_status()` 得到的真实校准摘要；
  双端口 W 链为 `raw_w_path`。
- `dataset_kind` ∈ {`ONE_PORT_Z`, `TWO_PORT_H`}。
- `crc32` 为 Data 特征将要传输的**整份 CSV 字节流**的 CRC-32/ISO-HDLC。
- `protocol` ≠ 1 时前端必须拒绝并提示升级固件。
- `byte_count` 必须为正安全整数；前端在开始接收前校验，异常元数据不能驱动
  无限等待或异常内存申请。

## 4. Control（WRITE WITH RESPONSE，定长命令字节）

| 字节 | 含义 |
|---|---|
| 0x01 | START_TRANSFER（从 seq 0 开始发送） |
| 0x02 | RESTART_TRANSFER（从 seq 0 重发整份 sealed dataset） |
| 0x03 | ABORT_TRANSFER（停止当前发送，保持连接） |
| 0x04 | GET_STATUS（请求一次 Status notify） |

v1 不在 MCU 上解析 JSON；未知命令回错误码 2。浏览器现在会在 seq gap、坏帧、
CRC 失败或接收停滞时自动发送 `ABORT_TRANSFER`，短暂 drain 后发送
`RESTART_TRANSFER`，用户无需手动重新点击。

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
5 本地 notify 提交/transport 失败。错误码 5 会停止当前 stream，但连接保留时
仍允许 `RESTART_TRANSFER` 重新开始。

## 6. Data（NOTIFY，自动分帧 CSV）与上限

帧布局（小端）：

```text
offset  size  field
0       2     magic = ASCII 'L','C'
2       1     protocol = 1
3       1     dataset kind（0 = ONE_PORT_Z，1 = TWO_PORT_H）
4       2     seq，uint16 little-endian（modulo 2^16）
6       2     payload_len，uint16 little-endian
8       N     CSV 字节
```

BLE **确实有单次 notification 上限**，但不是“整份数据只能这么大”。ATT
notification 的 characteristic value 最多为 `negotiated ATT_MTU - 3`：默认
MTU=23 时最多 20 字节；Arduino-ESP32 API 支持的 MTU 范围为 23..517，理论
单通知 characteristic value 上限为 514 字节。项目不依赖最大 MTU：

- 未协商时按 20-byte ATT payload 工作，扣 8-byte LCR header 后每帧只传
  **12 byte CSV**；这是最保守、兼容性最高的路径。
- 固件 preferred MTU 设为 185；只有当前连接实际 `onMtuChanged` 后才使用更大
  payload，peer 不接受时自动保持 MTU23。
- 即使协商到很大 MTU，项目仍把 CSV 数据限制为 **128 byte/frame**，所以
  一条 Data notification 最大 136 字节。更大的数据集自动拆成多帧，而不是
  继续增大单包。
- 固件每次 `poll()` 最多发一帧，间隔 **15 ms**。这样牺牲少量峰值吞吐，
  换取 ESP32 host/controller queue 与 Web Bluetooth 事件队列的稳定余量。
- `seq` 在线上是 uint16，按 modulo 65536 递增：`65535 → 0` 是合法回绕。
  固件原本就自然回绕；前端现已使用同一规则，不会把长流的合法回绕误判成 gap。
- Status 的 `byte_count/bytes_sent/bytes_total` 使用 uint32。当前产品数据集更早
  受到固件静态 CSV buffer 限制：One-Port 与 Two-Port 各 **12 KiB**，因此
  当前真实应用上限远小于任何 seq/uint32 协议边界。

换言之：**当前 12 KiB 数据集没有“BLE 总传输大小上限”问题；真正需要自动
处理的是每通知 MTU 上限、队列压力和偶发丢通知/断线。**这些现在都由分帧、
pacing、seq/CRC 与自动重试处理。

## 7. 稳定传输与自动恢复

接收端使用两层恢复机制，均不改变 sealed dataset：

1. **同一 GATT 连接内自动重传**：发现 seq gap、非法/截断帧、dataset kind
   错误、CRC32 错误或连续 15 s 没有新增字节时，前端先 ABORT，等待 120 ms
   让旧 notification 排空，再 RESTART。最多 3 次自动重试。
2. **GATT 断线自动重连**：如果链路真正断开，浏览器不重新弹 chooser，而是
   用已授权的 `BluetoothDevice.gatt.connect()` 自动重连，最多 2 次；重连后
   重新读取 metadata、重新订阅 notifications，并从 seq 0 重传。

每一轮 RESTART 开始时，接收端只接受新的 `seq=0` 作为流起点；重启前残留在
host/controller/browser queue 的旧帧全部忽略。最终只有同时满足以下条件才把
CSV 交给解析/拟合：

- 帧协议版本正确、dataset kind 正确；
- seq 连续（含 uint16 wrap）；
- 实收字节数严格等于 metadata `byte_count`；
- 整份 CRC32 与 metadata 完全一致。

重传时内部 assembler 会 reset，但 UI 暴露的进度使用历史最大接收量，因此
不会从 80% 突然倒退到 0%。

## 8. 前端接收流程（Web Bluetooth）

1. 用户点击触发 `navigator.bluetooth.requestDevice({filters:[{services:[UUID]}]})`；
2. GATT connect → 读 Metadata（版本/schema/长度校验）；
3. 订阅 Status/Data notifications → 写 `START_TRANSFER`；
4. 分帧接收并按 seq 重组；异常按 §7 自动恢复；
5. `byte_count` + CRC32 最终校验；
6. `TextDecoder('utf-8')` 得到 CSV 文本；
7. 单端口：`parseZCsv()` → `loadPoints()` → WASM 拟合；双端口：
   `parseHCsv()` → 复 H 曲线；
8. “保存收到的 CSV”下载的是通过 CRC 校验后的原始字节。

浏览器要求：桌面 Chrome/Edge + secure context（HTTPS 或 localhost）。

## 9. 兼容性规则

改变以下任何一项都必须 bump `LCR_BLE_PROTOCOL_VERSION`（并同时更新本文档、
固件 `fw_version.h`、前端 `protocol.ts` 与双侧测试）：

- `Z = V/I`、`H = Vout/Vin` 定义或 phase 正方向；
- CSV 单位或列语义（属 schema version，与协议版本联动声明）；
- BLE frame layout、CRC 覆盖范围；
- 数据集完成/封存语义（何时允许开射频、何时可重传）。

以下属于 **v1 兼容实现策略**，不要求 bump：MTU preferred value、单帧 payload
cap、notification pacing、自动 retry/reconnect 次数，只要帧格式与 sealed dataset
语义不变。
