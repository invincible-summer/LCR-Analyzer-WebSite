# ESP32-S3 LCR 固件 2026-09-12 运行时 / UI / BLE 修复与验收计划

状态：**实现已落在 `fix/ino-ui-ble-20260912` / PR #5。本文件是实现后的最终架构 review 与验收规范；最新 head 的六个 CI job 全绿后方可合并 `main`。实体开发板验证是独立硬件门禁。不得创建 Tag。**

审计基线：`main@56504745a4a010f940996d6efb83ad281e4b3f3e`。本轮修改限定于应用层输入/UI/BLE、测试、静态门禁与硬件/协议文档；任何 `DO_NOT_TOUCH_*` 测量实现、CSV schema、Try1/Try2/Try3 数学算法均不得因本轮问题被改写。BLE 本轮增强保持 protocol v1 **线格式不变**：仍是同一组 UUID、同一 8-byte Data header、同一 START/RESTART/ABORT/STATUS 命令与整份 CSV CRC32；改变的是 MTU 适配、分片、节流和自动恢复实现。

## 1. 报告问题、根因与落实结果

这批现象不是一个共同 bug，而是输入解码、ST7735S 几何/UI、双端口显示语义、BLE 生命周期/吞吐和功能可达性五类问题叠加。

| 报告项 | 审计结论 | 最终实现 |
|---|---|---|
| 编码器旋转抖动 | 旧 `input.cpp` 只挂 A 相 `CHANGE`，B 相只在 A 边沿瞬时判向；B 接点抖动/丢边会改变方向判断 | A/B 两相都挂 `CHANGE`；完整 2-bit Gray 转移表；相邻反跳自然抵消；非法双比特跳变清半格；host regression 覆盖 CW/CCW/bounce/glitch |
| TFT UI 错位 | `board_profile.cpp` 声明面板 128×160，却 `rotation=1` 令 UI 按 160×128 横屏运行；screen 又有横屏绝对坐标 | 产品 UI 固定 ST7735S portrait `128×160`、`rotation=0`；Menu/Component/One-Port/Two-Port/Signal Generator 按 `tft.width()/height()` 约束布局 |
| 运行一段时间 `StoreProhibited` | `EXCCAUSE=0x1d`、`EXCVADDR=0x11c` 是低地址非法 store；无匹配 ELF 不能把 `PC=0x42069edf` 猜成某行源码。仓库同时存在可证明的 BLE 重复生命周期错误：`deinit(true)` 后再次 init | 只用 `deinit(false)`；初始化失败回真实 `Off`；GATT 指针 deinit 后清空；callback 只发 atomic mailbox；BLE start/stop 打印 heap/PSRAM；若仍复现必须用同构建 ELF/addr2line 定位 |
| 单元件扫频最低 50 Hz | 仅 Component UI 把 F0 写死为 50；产品核心最小频率已是 10 Hz | F0 统一使用 `INSTRUMENT_F_MIN_HZ=10`，F1 继续受 10 kHz 产品上限约束 |
| 双端口增益单位 | 真源 `H=Vout/Vin` 是无量纲复比；网站 parser 已正确计算 `20log10|H|` | 保留 canonical CSV `f,re_h,im_h`；TFT 显示 `GAIN dB / PHASE deg` |
| 双端口 TFT 无相位 | BodePlot 有 phase 能力但旧 Two-Port 只画 gain | 从复 H 计算 `atan2(ImH,ReH)`；仅显示侧 unwrap；上传值保持原始复 H |
| 双端口上传不稳 | screen 与主 loop 双 `radio.poll()`；旧发送路径突发 notify；MTU 跨连接残留；错误后缺自动恢复 | 主 loop 唯一 pump；连接级 MTU reset；128 B CSV 分片；15 ms pacing；Data notify error mailbox；同连接自动 ABORT+RESTART；真实断线自动 reconnect |
| Signal Generator 无法正常进入 | 功能本身已是异步 SetTone/StopTone，但菜单入口被隐藏 | 主菜单第 4 项正式暴露；仍坚持真实 StopTone completion 后才退出 |
| 第二次 BLE 上传稳定崩溃 | 旧 `stopBle()` 使用 `BLEDevice::deinit(true)`；Arduino-ESP32 明确该路径释放 BT stack memory 并阻止再次初始化 | 改 `deinit(false)`；静态门禁禁止回归；每 session/连接重新初始化协商状态与 GATT 对象 |

## 2. 硬件与数据手册约束

开发板为 ESP32-S3-WROOM-1-N16R8（16 MB Flash / 8 MB PSRAM）。板载 Micro USB 通过 CH340X + 数字隔离走 UART0 GPIO43/44；产品构建保持 `USB CDC On Boot = Disabled/default`。GPIO26–32 属模组内 Flash/PSRAM，GPIO33–37 在 N16R8 八线存储总线下不能作为普通 UI GPIO。

当前 DNT 真源以代码为准：ADC GPIO1/2；LCD_CAM DAC D0..D7 = GPIO6,7,15,16,17,18,8,9；74HC595 = GPIO21,19,20。因此最终测量保留集是 `{1,2,6,7,8,9,15,16,17,18,19,20,21}`。TFT 使用被 DNT 释放的 `CS=GPIO10, MOSI=11, SCLK=12, RST=13, DC=14`，write-only，无 MISO。

ST7735S 产品唯一 UI 坐标系为 portrait `W=128,H=160`、`tftRotation=0`。4-wire serial 最小 write clock cycle 66 ns，对应约 15.15 MHz；产品固定 10 MHz。若实体模块仍有固定整体平移，只允许在确认玻璃/GM strap/TFT_eSPI init variant 后统一修改 offset，不允许 screen 各自补偿。

本轮 CI 曾暴露 Gate A manifest 中 `DO_NOT_TOUCH_lcr_diag.h` 与 `DO_NOT_TOUCH_lcr_measure.h` 的 hash 已落后于 PR 基线本身；分支对应 DNT blob 与 `main@56504745...` 完全相同，**没有修改 DNT 内容**。因此只把 `dnt_manifest.txt` 重锁到基线已有 blob 的真实 SHA-256，恢复“当前已批准 DNT 内容不可再漂移”的门禁语义。

## 3. 输入接口语义与 EC11 差异处理

`Input` 对外契约不变：screen 只收到 `EncInc/EncDec`，不感知 A/B 细节。实现用 `(A<<1)|B` 四状态；有效相邻 Gray 转移贡献 ±1 quarter-step，反向 bounce 自动抵消，`00<->11`、`01<->10` 等双比特跳变判为非法并丢弃半格累计。

当前 4 quarter-step 发布一个 UI detent，用于保持旧固件的既有机械手感。仓库只有通用 EC11 数据手册，没有在 BOM 中钉死具体 pulse/detent 料号；若实物属于不同组合，只允许在输入层统一调整 transition→detent 归一化，不能让各 screen 自己补偿。实体板必须覆盖慢/快 CW、CCW、轻触/重触，并确认一机械格恰好一个 UI 事件。

## 4. 128×160 UI 与双端口数据语义

顶栏固定 18 px，底部 hint 占最后 12 px；业务内容只使用中间区域。DigitEditor 横坐标由屏宽和控件宽度推导；进度条宽度由 `tft.width()` 推导。主菜单固定四入口：Component、One-Port Z、Two-Port H、Signal Generator。

Two-Port canonical dataset 永远是无量纲复 H。TFT sealed preview 左轴 gain[dB]、右轴 phase[deg]；phase 为连续绘线允许显示侧 unwrap，但上传 `re_h/im_h` 不做 unwrap、不添加派生列。网站继续从复 H 推导 dB/degree，保持现有正确实现。

## 5. BLE 上限确认：上限在“单个 ATT notification”，不是当前 12 KiB 整份数据集

### 5.1 协议/栈的真实限制

BLE GATT notification 不是任意长度消息。对当前 ESP32/Arduino BLE 栈，单次 characteristic notification 的有效 value 受当前连接 ATT MTU 约束：**最大 characteristic value = negotiated ATT_MTU - 3**。ESP32 默认 MTU 为 23；Espressif 文档和 Arduino-ESP32 API 允许配置到 517，但实际连接值必须由双方协商，不能假定 peer 一定接受较大的 MTU。

因此默认 MTU23 时：

```text
ATT_MTU                   = 23 B
ATT notification value    = 23 - 3 = 20 B
LCR v1 application header = 8 B
CSV data / notification   = 12 B
```

项目不把“MTU 最大值”当作稳定性目标。固件请求 preferred MTU=185；只有当前连接实际收到 `onMtuChanged` 后才扩大 payload。即使协商到 185 或更大，本项目仍把 **CSV data cap 固定为 128 B/frame**，即一个 Data notification 最多 `8+128=136 B`。peer 不协商时则自动退回 12 B/frame。

### 5.2 当前产品数据集上限

One-Port 和 Two-Port 的 canonical CSV buffer 各为 **12 KiB**，Sweep 最大 257 点。因此当前产品首先受到 dataset 静态 buffer 的 12 KiB 上限，而不是 BLE “整份消息”上限。Status 的 `byte_count/bytes_sent/bytes_total` 是 uint32；Data `seq` 是 uint16，但现在 sender 与 receiver 都按 modulo 65536 解释，`65535 -> 0` 为合法连续序列。

以满 12 KiB 为例，仅计算 15 ms 软件 pacing、不含连接调度/浏览器开销：

```text
保守 MTU23:  12288 / 12  = 1024 frames，约 15.36 s pacing 下限
128 B/frame: 12288 / 128 =   96 frames，约  1.44 s pacing 下限
```

因此当前实现不存在“数据大于一个 BLE notification 就传不了”的问题；整份 CSV 从设计上就是多帧字节流。

## 6. BLE 稳定传输实现约束

`RadioManager` 是 BLE 生命周期唯一 owner；`LCR_UI.loop()` 是 `radio.poll()` 唯一调用点。测量或 Signal Generator 激励期间 BLE 必须 Off；只有真实 StopTone 完成、dataset seal 且用户确认后才能启动射频。

### 6.1 发送端分片与流控

固件的发送语义固定如下：

1. Session、connect、disconnect 都先把 CSV payload 回到默认 MTU23 的 **12 B/frame**，绝不继承上一连接 MTU。
2. `BLEDevice::setMTU(185)` 只表达本机 preferred MTU；实际发送大小只相信本连接 `onMtuChanged`。
3. `m_attPayload = min(negotiated_MTU-3-8, 128)`；若没有 MTU event 则始终为 12。
4. 整份 CSV 根据 `m_bytesSent` 自动连续切片，直到 `m_bytesSent == m_bytesTotal`。调用方不需要知道帧数。
5. 每个 `poll()` **最多发一帧**，两帧之间至少 **15 ms**。不使用 burst loop，不以压满 controller queue 换峰值吞吐。
6. Data characteristic 注册 `onStatus`；任何非 `SUCCESS_NOTIFY` 结果写入 atomic error mailbox。主 loop 收到后停止当前 stream 并进入 Error。即使最后一帧已经从应用层“提交”后才收到失败 callback，也必须把 `Connected` 翻为 `Error`，不能误报成功。
7. `seq` 为 uint16 modulo 2^16；payload length 为 uint16；Status 总字节计数为 uint32。
8. `BLEDevice::deinit(false)` 是唯一 stop 方式；每次 deinit 后所有 GATT raw pointer 清空。禁止 `deinit(true)`。

### 6.2 接收端自动恢复

浏览器接收不再把偶发 seq gap/CRC 错误直接抛给用户处理，而是自动完成恢复：

**同一 GATT 连接内**：每份数据最多 4 个 attempt（首次 START + 最多 3 次 RESTART）。发生以下任一情况即判定当前 attempt 不可信：

- Data frame magic/长度非法；
- protocol 或 dataset kind 不一致；
- seq 不连续；
- 收到字节超过 metadata `byte_count`；
- 完整 byte_count 到齐但 CRC32 不匹配；
- 连续 15 s 没有新增有效数据。

恢复顺序固定为：停止接收当前流 → 写 `ABORT_TRANSFER` → 等 120 ms drain 已进入 host/controller/browser queue 的旧通知 → assembler reset → 写 `RESTART_TRANSFER`。新 attempt 只把新的 `seq=0` 作为流起点，在此之前到达的残留旧帧直接丢弃。

**真实 GATT 断线**：不重新弹设备 chooser，而是使用用户已经授权的 `BluetoothDevice.gatt.connect()` 自动重连，最多 2 次。重连后重新读取 Metadata、重新订阅 Status/Data，然后从 seq 0 重传 sealed dataset。

最终只有同时满足下面四个条件，数据才能进入 CSV parser / Try1/2/3：

```text
protocol/kind correct
+ seq continuous (including uint16 wrap)
+ bytesReceived == metadata.byte_count
+ CRC32(full CSV bytes) == metadata.crc32
```

任何中间 attempt 的内容都不会提交给拟合。Dataset 已 seal，因此重传逐字节确定；内部 assembler 在 retry 时可以清零，但 UI 对外进度使用历史最大接收量，避免视觉进度倒退。

### 6.3 callback / task 并发

BLE callback 与 Arduino loop 位于不同 FreeRTOS task。connect/disconnect/MTU/control/notify-error mailbox 全部使用 `std::atomic`；callback 只允许轻量 `store`，`RadioManager::poll()` 用 `exchange` 消费。callback 内禁止 `advertising/notify/deinit` 等生命周期操作。

## 7. StoreProhibited 的结论边界

现场 `EXCCAUSE=0x1d StoreProhibited` 与 `EXCVADDR=0x0000011c` 表明向无效低地址 store；无现场同构建 ELF/map 时，不能把 `PC=0x42069edf` 猜成具体源码行。本轮可以确定并修复的是 BLE `deinit(true)->reinit`、double-pump、突发 notify、MTU 跨连接污染、callback 重操作和缺少自动恢复等真实缺陷。

BLE start/stop 记录 session、bytes、free heap、free PSRAM；MTU 协商后额外记录 `ATT payload` 与实际 `csv/frame`。若实体板仍 panic，必须保留该次构建 ELF 和完整 backtrace，用 Xtensa addr2line 定位，不再凭 PC 地址猜测。

## 8. 自动化验收与回归门禁

PR 最新 head 必须通过现有六个 GitHub Actions job。Firmware job 至少锁住：

- DNT manifest、GPIO、radio-lock、Signal Generator static gates；
- ESP32-S3 production compile：Arduino-ESP32 3.3.11 + TFT_eSPI 2.5.43；
- 禁止 `BLEDevice::deinit(true)`；
- 强制 session/connection conservative MTU reset；
- callback mailbox 必须 atomic；
- Data notify error mailbox 存在；
- preferred MTU=185；
- CSV payload cap=128；
- pacing=15 ms；
- seq space=65536；
- screen 不得二次调用 `radio.poll()`。

前端 Vitest 必须覆盖正常多帧重组、seq gap、CRC/byte_count、uint16 seq wrap，以及“首轮 seq gap → 自动 ABORT+RESTART → 最终收到同一 CSV”的完整恢复路径。TypeScript type-check 与 production build 也必须通过。

最终 diff review 必须再次确认：没有任何 `DO_NOT_TOUCH_*` 内容变化；没有改变 BLE v1 wire layout / UUID / CRC 覆盖范围；没有改变 CSV schema；Two-Port 仍从 `re_h/im_h` 推导 gain/phase；没有给测量、BLE、Signal Generator 引入 busy-wait。

## 9. 实体开发板发布门禁

软件 CI 全绿不能替代物理验证。实体 ESP32-S3-N16R8 + ST7735S 至少执行：

1. 上电 10 次，128×160 portrait UI 完整，无越界/重叠/固定偏移。
2. 编码器 CW/CCW 各至少 100 detent（慢速 50 + 快速 50），一格一事件。
3. Component 从 10 Hz 起始完成已知 R/C/L（含 L+DCR）测量。
4. One-Port 与 Two-Port 交替完成至少 **20 个 BLE session**：每次 sweep→seal→connect→transfer→CRC pass→Back；不得 panic/reboot。
5. 传输验证至少包含两类 MTU：能协商较大 MTU时确认串口出现 `csv/frame=128` 或实际协商值；同时至少一次强制/使用 MTU23 兼容路径，确认 12 B/frame 仍能完整传输 12 KiB 级流。
6. 至少 3 次在传输中主动断开客户端，确认网页自动 reconnect 后完整恢复且不再次弹 chooser；至少 3 次人为制造/测试 RESTART 路径，最终 CRC 一致。
7. 重复第二次、第三次上传尤其检查 `# BLE session start/stop` heap/PSRAM，不得出现单调不可恢复内存下降。
8. Two-Port TFT gain/phase 与网站同一 CSV 的数值语义一致。
9. Signal Generator 10 Hz / 1 kHz / 10 kHz start/stop；SetTone pending 和 running 时 Back 均非阻塞且必须 StopTone completion 后退出。
10. 30 分钟以上 soak：idle、sweep、cancel、BLE upload、Signal Generator 交替；不得出现 StoreProhibited、WDT、断线后无法恢复或传输静默卡死。

建议实板记录每个 BLE session：session id、byte_count、协商 ATT payload、csv/frame、总用时、重试次数、CRC、start/stop heap/PSRAM。这样若仍有偶发问题，可以区分 RF 质量、MTU/吞吐、内存生命周期与应用状态机，而不是把所有异常归为“蓝牙不稳”。

## 10. 最终 review 结论与合并规则

本轮 BLE review 的核心结论是：**有单包上限，但当前产品没有“12 KiB 数据集必须一次塞进 BLE”的设计错误。正确方案是 MTU-aware fragmentation + 有界发送节流 + end-to-end CRC + 自动 retry/reconnect。**现已按该模型落实，并同时补上未来长流的 uint16 seq wrap 语义。

保留的正确部分包括 DNT 测量核心、CSV schema、BLE v1 wire format、网站 Two-Port dB/phase 推导、Signal Generator 异步 SetTone/StopTone。修改集中在真正需要增强的 BLE transport policy 与接收恢复，不为了“重构”去改正确的测量/拟合逻辑。

合并策略：**只有 PR #5 最新 head 的六个 CI job 全部 `success`，完成最终 diff review 后才 squash merge 到 `main`；不创建 Tag。**实体板门禁未完成前，只能声称软件实现和自动化已通过，不能声称现场 RF/硬件已经百分之百无故障。

### 参考真源

- Espressif ESP-FAQ / ESP-AT BLE 文档：默认 ATT MTU=23、可配置到 517、双方协商取实际值；GATT server notification 单次 data length 最大为 `MTU-3`。
- Espressif Arduino-ESP32 BLE `BLEDevice.cpp`：`setMTU()` 接受 >23 且 <=517；`deinit(false)` 保留以后重新初始化能力，`release_memory=true` 会阻止 reinitialization。
- Espressif Arduino-ESP32 BLE `BLECharacteristic`：notify/status callback 语义，包括 `SUCCESS_NOTIFY` 与 GATT/无客户端/无订阅等错误状态。
- 仓库 `ino/LCR_UI/{ble_protocol.h,radio_manager.cpp,dataset.h}`：本项目 Data frame、MTU 适配、12 KiB dataset、CRC/Status 真源。
- 仓库 `frontend/src/lib/ble/{protocol.ts,lcrDevice.ts}`：浏览器 seq 重组、CRC 校验、自动 RESTART/reconnect 真源。
- Sitronix ST7735S Datasheet v1.3；课程《开发板硬件手册》；仓库 `DO_NOT_TOUCH_*` 与 EC11 数据手册：显示、板卡和测量硬件约束。
