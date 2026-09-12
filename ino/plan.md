# ESP32-S3 LCR 固件 2026-09-12 运行时 / UI / BLE 修复与验收计划

状态：**实现位于 `fix/ino-ui-ble-20260912` / PR #5。本文件是实现后的最终架构 review 与验收规范；只有最新 head 的六个 CI job 全部成功后才允许合并 `main`。实体开发板验证是独立发布门禁。不得创建 Tag。**

审计基线：`main@56504745a4a010f940996d6efb83ad281e4b3f3e`。修改边界限定于输入/UI/BLE transport、测试、静态门禁与文档；`DO_NOT_TOUCH_*` 测量实现、CSV schema、Try1/Try2/Try3 数学算法均不得被本轮改写。BLE 本轮增强保持 protocol v1 **wire format 不变**：UUID、8-byte Data header、START/RESTART/ABORT/STATUS、整份 CSV CRC32 均保持兼容。

## 1. 本轮问题与落实结论

| 报告项 | 根因 | 最终实现 |
|---|---|---|
| 编码器旋转抖动 | 旧实现只监听 A 相 CHANGE、瞬时读取 B 判向，不能可靠过滤 B 相反跳/漏边 | A/B 双相 CHANGE + 完整 2-bit Gray 状态机；4 quarter-step 归一成一个 UI detent；非法双比特跳变清半格；host tests 覆盖 CW/CCW/bounce/glitch |
| TFT UI 错位 | 面板是 128×160，却按 rotation=1 的 160×128 横屏设计 | 产品 UI 固定 portrait 128×160 / rotation=0；Menu/Component/One-Port/Two-Port/Signal Generator 全部按真实几何重排 |
| StoreProhibited / 第二次 BLE 稳定崩溃 | 旧 `BLEDevice::deinit(true)` 会释放 BT stack memory，之后 reinit 不受支持；另有 double-pump、突发 notify、MTU 残留等风险 | `deinit(false)`；GATT pointer 清理；atomic mailbox；主 loop 唯一 radio pump；连接级 MTU reset；notify error 回主 loop；heap/PSRAM 观测 |
| Component 最低 50 Hz | UI 局部硬编码，核心频率下限实际是 10 Hz | 统一使用 `INSTRUMENT_F_MIN_HZ=10` |
| Two-Port 增益单位/相位 | H 是无量纲复比，TFT 旧实现只画幅值且语义混乱 | canonical CSV 保持 `f,re_h,im_h`；TFT 显示 gain[dB] + phase[deg]，phase 只在显示侧 unwrap |
| Signal Generator 难以进入 | 页面本身状态机可用，但菜单入口被隐藏 | 成为第四个正式菜单项；保留异步 SetTone/StopTone 与 completion 驱动退出 |
| BLE 大数据/上传不稳定 | BLE notification 有 MTU 单包上限；旧前端没有真正自动重传/重连 | MTU-aware fragmentation + 128 B cap + 15 ms pacing + seq/CRC + 自动 ABORT/RESTART + 自动 GATT rebuild/reconnect |

## 2. 硬件与显示约束

开发板为 ESP32-S3-WROOM-1-N16R8。最终 DNT 测量引脚真源为：ADC GPIO1/2；LCD_CAM DAC D0..D7=`6,7,15,16,17,18,8,9`；74HC595=`21,19,20`。TFT 使用被 DNT 释放的 GPIO10..14：CS10、MOSI11、SCLK12、RST13、DC14，write-only，10 MHz。ST7735S 产品坐标固定为 128×160 portrait。

本轮没有修改任何 `DO_NOT_TOUCH_*` 文件。CI 曾发现 `dnt_manifest.txt` 中 `DO_NOT_TOUCH_lcr_diag.h` / `DO_NOT_TOUCH_lcr_measure.h` 两条 hash 已经落后于 PR **基线自身**；核对 branch blob 与 `main@56504745...` 相同后，仅重新锁定 manifest 到基线既有 DNT 内容，恢复“之后不允许漂移”的 Gate A 语义，而不是借机改测量代码。

## 3. 输入/UI 的接口约束

`Input` 是 EC11 唯一归一化层，screen 永远只消费 `EncInc/EncDec`。当前一完整 Gray cycle 对应一个 UI detent；若实体 EC11 具体料号的 pulse/detent 比与该假设不同，只能在输入层统一调整，不得在各页面分别补偿。

UI 唯一逻辑几何为 128×160。新增控件必须从 `tft.width()/height()` 与控件实际宽度推导位置，禁止恢复 160×128 magic coordinate。Two-Port 数据唯一真源仍是复 H，TFT/网站都只做显示派生。

## 4. BLE 上限的确定结论

BLE 的关键上限是**单个 ATT notification 的 characteristic value 长度**，不是“整份数据只能传这么多”。ESP32 默认 ATT MTU=23；MTU 可配置至 517，但连接实际 MTU由双方协商，不能假定客户端一定接受请求值。GATT notification 的最大 value 为 `negotiated ATT_MTU - 3`。

LCR v1 Data frame 自身有 8-byte header，因此默认 MTU23 时：

```text
ATT MTU                    23 B
notification value max     20 B (= 23 - 3)
LCR frame header            8 B
CSV payload / frame        12 B
```

固件现在请求 preferred MTU=185，但**只有本连接实际收到 `onMtuChanged` 后**才使用更大 payload；协商失败/未发生时一直走 12 B/frame 保守路径。即使协商到 185 或更大，项目仍主动把 CSV payload 限为 **128 B/frame**，所以 Data notification 最大 136 B。这样避免把 MTU 517 当作必须追求的吞吐目标，把 RAM、BLE host/controller queue 和 Web Bluetooth event queue 压力控制在可预测范围。

当前 `OnePortDataset` / `TwoPortDataset` CSV 静态 buffer 各为 **12 KiB**，Sweep 最大 257 点；因此当前产品首先受 12 KiB dataset buffer 限制，而不是 BLE 总字节数限制。Status 总字节计数使用 uint32；Data `seq` 是 uint16，现已在固件和前端都定义为 modulo 65536，`65535 -> 0` 为合法连续序列。

满 12 KiB 数据流的纯 pacing 量级（不含 connection interval / RF / 浏览器调度）：

```text
MTU23 保守路径: 12288 / 12  = 1024 frames × 15 ms ≈ 15.36 s
128 B 路径:     12288 / 128 =   96 frames × 15 ms ≈  1.44 s
```

结论：**有单包上限；当前整份 12 KiB 数据集不存在必须一次塞入 BLE 的设计需求。正确实现就是自动分片传输。**

## 5. 发送端 transport 语义

`RadioManager` 是 BLE 生命周期唯一 owner，`LCR_UI.loop()` 是 `radio.poll()` 唯一 caller。测量或 Signal Generator 输出活动期间 BLE 必须 Off；只有真实 StopTone completion、dataset seal 且用户确认后才允许 radio on。

发送状态机的硬约束：

1. session start / connect / disconnect 都把可用 CSV payload 重置为 12 B，绝不继承上一连接 MTU；
2. `BLEDevice::setMTU(185)` 只设置 preferred MTU，实际尺寸只服从当前连接 MTU event；
3. `csvPerFrame = min(negotiatedMTU - 3 - 8, 128)`；未协商就是 12；
4. 整份 sealed CSV 按 `m_bytesSent` 自动切片，调用层不需要计算帧数；
5. 每次 `poll()` 最多 notify **1 frame**，frame 间隔至少 **15 ms**；禁止 burst while-loop；
6. Data characteristic 注册 `onStatus`。任何非 `SUCCESS_NOTIFY` 通过 atomic mailbox 报给主 loop，当前 stream 立即进入 Error；
7. 最后一帧“已提交给 BLE stack”不等于客户端已收到；若最后一帧随后回 notify failure，Connected 也必须翻回 Error，不能误报完成；
8. `deinit(false)` 是唯一 stop 路径；deinit 后所有 GATT pointer 清空；禁止 `deinit(true)`；
9. callback 只能 atomic store，禁止在 BLE callback task 内执行 advertising / notify / deinit 等生命周期操作。

## 6. 接收端自动恢复语义

浏览器接收分成“同连接恢复”和“连接重建”两层，所有 attempt 都针对同一 sealed dataset。

### 6.1 同一 GATT 连接自动 ABORT + RESTART

每个连接允许 **4 个 transfer attempt**：1 次 START + 最多 3 次 RESTART。以下任一情况使当前 attempt 立即失效：

- Data magic / frame length 非法；
- protocol 或 dataset kind 不匹配；
- seq gap；
- 实收字节超过 metadata `byte_count`；
- Status session 不匹配或设备报告 BLE error code；
- byte_count 到齐但整份 CRC32 不匹配；
- 连续 15 s 无新增有效字节。

恢复流程固定为：停止接受当前流 → `ABORT_TRANSFER` → 120 ms drain → assembler reset → `RESTART_TRANSFER`。下一 attempt 只接受新的 `seq=0` 作为流起点，在此之前到达的上一轮残留 Data frame 全部丢弃。

### 6.2 同连接重试耗尽后自动重建 GATT

如果同一个 GATT connection 连续 4 个 attempt 仍失败，不能简单把错误丢给用户。即使 Web Bluetooth 仍显示 `gatt.connected=true`，也认为 ATT/GATT 状态可能已卡住：前端主动 `disconnect()`，然后直接对**已经授权的同一 `BluetoothDevice`**执行 `gatt.connect()`；不会再次弹 chooser。

真实 RF/GATT 断线也走同一路径。`receiveDataset()` 最多使用 **2 次 reconnect**。每次重连后必须重新：

```text
getPrimaryService
read Metadata
subscribe Status
subscribe Data
START from seq=0
```

如果两次 GATT rebuild/reconnect 仍不能完成，才向用户暴露最终传输错误。

### 6.3 数据提交边界

任何 attempt 的部分数据都不能进入 CSV parser。只有以下条件全部满足才提交：

```text
protocol / dataset kind / session state correct
AND seq continuous (including uint16 wrap)
AND bytesReceived == metadata.byte_count
AND CRC32(full CSV raw bytes) == metadata.crc32
```

重传期间内部 assembler 可以 reset；对 UI 的 progress 使用历史最大接收量，使用户看到的进度保持单调，不因自动重试从 80% 倒退到 0%。

## 7. StoreProhibited 的定位边界

现场 `EXCCAUSE=0x1d StoreProhibited`、`EXCVADDR=0x0000011c` 能证明低地址非法 store，但没有现场同构建 ELF/map 时不能把 `PC=0x42069edf` 猜成具体源码行。本轮能确定并消除的是 BLE `deinit(true)->reinit`、double-pump、突发 notify、MTU 跨连接污染、callback 重操作、notify failure 未反馈与前端没有自动恢复等真实缺陷。

固件记录 session id、byte_count、free heap、free PSRAM，以及 MTU event 后的 ATT payload / csv-per-frame。若实体板仍 panic，必须保存该构建 ELF 和完整 backtrace，用 Xtensa addr2line 符号化。

## 8. 自动化门禁

最新 PR head 必须通过六个 GitHub Actions job。Firmware gate 必须锁住：

- DNT SHA manifest 与 GPIO/radio-lock 架构；
- Arduino-ESP32 3.3.11 + TFT_eSPI 2.5.43 的 ESP32-S3 production compile；
- 禁止 `BLEDevice::deinit(true)`；
- conservative MTU reset 必须存在；
- callback mailbox 必须 atomic；
- Data notify error mailbox 必须存在；
- preferred MTU=185；CSV payload cap=128；pacing=15 ms；
- seq space=65536；
- screen 不得二次调用 `radio.poll()`。

Frontend Vitest 必须覆盖：正常多帧重组、非法 seq、CRC/byte_count、uint16 seq wrap、首轮 seq gap 后自动 `ABORT -> RESTART -> 成功`。TypeScript type-check 与 production build 同样是硬门禁。

最终 diff review 要再次确认：PR changed files 中没有任何 `DO_NOT_TOUCH_*`；BLE protocol v1 wire layout / UUID / CRC 范围未改；CSV schema 未改；Try1/2/3 未改；Two-Port 仍从 `re_h/im_h` 派生显示；测量/BLE/Signal Generator 没有新增 busy-wait。

## 9. 实体板发布门禁

CI 全绿不能代替 RF/硬件验证。实体 ESP32-S3-N16R8 至少执行：

1. 上电 10 次，完整 128×160 UI；
2. 编码器 CW/CCW 各 100 detent（慢 50 + 快 50），无多跳/反跳；
3. Component 从 10 Hz 起始测已知 R/C/L（含 L+DCR）；
4. One-Port / Two-Port 交替至少 **20 个 BLE session**，每次 sweep→seal→transfer→CRC pass→Back；
5. 至少验证一次 MTU23 保守路径（12 B/frame）能完整传输接近 12 KiB 数据；有较大 MTU 时确认协商后 frame payload 按实际 MTU 扩大且不超过 128；
6. 至少 3 次传输中主动断开客户端，确认自动 reconnect、不重新弹 chooser、最终 CRC pass；
7. 至少 3 次触发 RESTART 路径；若可注入连续错误，验证 4 个同连接 attempt 失败后自动 GATT rebuild；
8. 第二次、第三次上传重点记录 heap/PSRAM，不能单调下降；
9. Signal Generator 10 Hz / 1 kHz / 10 kHz start/stop，pending/running 时 Back 都必须 StopTone completion 后退出；
10. ≥30 分钟 idle/sweep/cancel/BLE/siggen soak，不得 StoreProhibited、WDT、无法恢复断线或 silent stall。

每个 BLE session 建议记录：session id、byte_count、ATT payload、csv/frame、总时长、transfer retry 次数、GATT reconnect 次数、CRC、start/stop heap/PSRAM。这样才能区分 RF、MTU、host queue、GATT state 与内存生命周期问题。

## 10. 最终 review 结论与合并规则

本轮 BLE review 的确定结论是：**BLE 有单 notification 的 MTU 上限；当前产品没有“12 KiB 整份数据超过 BLE 总上限”的问题。最终方案必须是 MTU-aware 自动分片、有界 pacing、seq/byte_count/CRC 端到端校验，以及失败后的自动 retry + GATT rebuild/reconnect。当前实现已按这个模型收敛。**

保留不需要修改的正确部分：DNT 测量核心、CSV schema、BLE v1 wire format、Try1/2/3、网站 Two-Port dB/phase 推导、Signal Generator 的异步 SetTone/StopTone。修改只落在真正有问题的 UI/input/BLE transport 与测试门禁。

合并规则：**只有 PR #5 最新 head 六个 CI job 全部 success，并完成最终 diff review 后才 squash merge `main`；不创建 Tag。**实体板门禁未完成前只能声明“软件与自动化通过”，不能声明现场 RF/硬件已百分之百无故障。

### 参考真源

- Espressif ESP-FAQ / ESP-AT BLE：默认 MTU=23、可设置到 517、连接实际值取协商结果；notification 单次 data length 最大 `MTU-3`。
- Espressif Arduino-ESP32 `BLEDevice.cpp`：`setMTU()` 范围语义；`deinit(false)` / release-memory 生命周期语义。
- Espressif Arduino-ESP32 `BLECharacteristic`：notify 与 `SUCCESS_NOTIFY` / GATT error callback 状态。
- 仓库 `ino/LCR_UI/{ble_protocol.h,radio_manager.cpp,dataset.h}`：本项目帧、分片、12 KiB dataset、Status/CRC 真源。
- 仓库 `frontend/src/lib/ble/{protocol.ts,lcrDevice.ts}`：seq wrap、自动 restart、自动 GATT rebuild/reconnect 真源。
- Sitronix ST7735S Datasheet、课程开发板手册、仓库 `DO_NOT_TOUCH_*` 与 EC11 数据手册：显示、板卡与测量硬件真源。
