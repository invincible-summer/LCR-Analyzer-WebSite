# ESP32 设备接入

本站与 ESP32-S3 LCR 仪表有两条数据通路：

## 1. BLE 直传测量数据集（v4.1.0 已实现 · 主通路）

设备固件（`ino/`，v4.1.0）完成一次扫频并把**封存数据集**经 BLE GATT v1
推给浏览器（Web Bluetooth API），免组网、免后端：

```
设备：sweep 全部频点 → 停激励/释放 ADC → dataset seal（CSV + CRC32）
      → 用户在设备上确认 → 才开启 BLE advertising
浏览器：requestDevice（按 Service UUID 过滤）→ GATT connect
      → 读 Metadata（protocol/schema/point_count/byte_count/crc32）
      → START_TRANSFER → 按 seq 重组 Data 帧 → 整份 CRC32 校验
      → TextDecoder → CSV 文本
```

- 协议细节（UUID、帧布局、命令字节、Status 负载）：
  仓库 `protocol/BLE_PROTOCOL_V1.md`；
- 数据模式（`lcr-z-csv-v1` / `lcr-h-csv-v1`）：`protocol/CSV_SCHEMA_V1.md`。

### 拟合页（单端口）

「蓝牙导入」按钮把收到的 `ONE_PORT_Z` 数据集喂给**现有** `parseZCsv()` →
`loadPoints()` → WASM 拟合——与文件上传完全相同的数据路径，没有第二套
拟合格式。「保存设备 CSV」下载通过 CRC 校验后的原始字节，便于复现实验。

### 扫频页（双端口）

「导入设备曲线（BLE）」接收 `TWO_PORT_H` 数据集：真源是复数
`H = Vout/Vin`，Bode 增益/相位与 Nyquist 全部由 `re_h/im_h` 推导
（`frontend/src/lib/twoPortCsv.ts`），与服务器扫描存储完全分离
（`frontend/src/store/device.ts`）。

### 浏览器要求

Web Bluetooth 需要桌面 Chrome/Edge 且 secure context（HTTPS 或
localhost）。页面加载时检测 `navigator.bluetooth`；不支持时按钮禁用并给
出专门的「功能不可用」提示，而不是普通的连接失败。设备端协议版本 ≠ 1
时前端拒绝并提示升级固件。

## 2. 波形上传（HTTP · 后端历史通路）

ESP32 作为"纯采集前端"把每个频点的原始 V/I 波形 POST 给后端，服务端完成
全部 DSP（正弦拟合 → 阻抗 → 不确定度）：

```
POST /api/scan/start                 → scan_id
POST /api/scan/{scan_id}/point       → 单频点波形（voltage[] / current[] / dt / n）
```

完整契约见 `docs/api_contract.md`。这条通路产出的扫描在拟合页「从历史
扫描导入」使用，与本页 BLE 通路互不影响。

## 固件侧

- 固件目录 `ino/`（架构与构建见 `ino/README.md`；硬件映射见
  `docs/HARDWARE_MAPPING.md`）；
- 测量期间射频完全关闭（radio_lock invariant），采样完成并封存后才允许
  初始化 BLE——上传的是拟合所需的 `f,Re,Im`（或 `f,re_h,im_h`）CSV，
  不是原始波形。
