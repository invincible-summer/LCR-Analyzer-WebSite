# ESP32-S3 LCR 固件 2026-09-12 运行时 / UI / BLE 修复与验收计划

状态：**实现已落在 `fix/ino-ui-ble-20260912` / PR #5。本文件是实现后的最终架构 review 与验收规范；最新 head 的六个 CI job 全绿后方可合并 `main`。实体开发板验证是独立硬件门禁。不得创建 Tag。**

审计基线：`main@56504745a4a010f940996d6efb83ad281e4b3f3e`。本轮修改限定于应用层输入/UI/BLE、测试与硬件文档；任何 `DO_NOT_TOUCH_*` 测量实现、CSV/BLE protocol schema、网站 Try1/Try2/Try3 数学算法均不得因本轮问题被改写。

## 1. 报告问题、根因与落实结果

这批现象不是一个共同 bug，而是输入解码、ST7735S 几何/UI、双端口显示语义、BLE 生命周期/吞吐和功能可达性五类问题叠加。

| 报告项 | 审计结论 | 最终实现 |
|---|---|---|
| 编码器旋转抖动 | 旧 `input.cpp` 只挂 A 相 `CHANGE`，B 相只在 A 边沿瞬时判向；B 接点抖动/丢边会改变方向判断 | A/B 两相都挂 `CHANGE`；完整 2-bit Gray 转移表；相邻反跳自然抵消；非法双比特跳变清半格；host regression 覆盖 CW/CCW/bounce/glitch |
| TFT UI 错位 | `board_profile.cpp` 声明面板 128×160，却 `rotation=1` 令 UI 按 160×128 横屏运行；screen 又有横屏绝对坐标 | 产品 UI 固定 ST7735S portrait `128×160`、`rotation=0`；Menu/Component/One-Port/Two-Port/Signal Generator 重新按 `tft.width()/height()` 约束布局 |
| 运行一段时间 `StoreProhibited` | `EXCCAUSE=0x1d`、`EXCVADDR=0x11c` 是低地址非法 store；无匹配 ELF 不能把 `PC=0x42069edf` 猜成某行源码。与此同时仓库有一个可证明的 BLE 重复生命周期错误：`deinit(true)` 后再次 init | 消除已证明缺陷并增强观测：只用 `deinit(false)`；初始化失败回真实 `Off`；GATT 指针 deinit 后清空；BLE start/stop 打印 heap/PSRAM；若仍复现必须用同构建 ELF/addr2line 定位 |
| 单元件扫频最低 50 Hz | 仅 `screen_component.cpp` 把 F0 UI 写死为 50；产品核心最小频率已是 10 Hz | Component F0 统一使用 `INSTRUMENT_F_MIN_HZ=10`，F1 继续受 10 kHz 产品上限约束 |
| 双端口增益单位 | 真源 `H=Vout/Vin` 是无量纲复比；网站 parser 原本已正确计算 `20log10|H|`，不应改成 Ohm | 保留 canonical CSV `f,re_h,im_h`；TFT 明确显示 `GAIN dB / PHASE deg`，网站现有正确逻辑保留 |
| 双端口 TFT 无相位 | `BodePlot` 已有 phase 轴/绘制接口，旧 `screen_twoport.cpp` 只画 gain | 从复 H 计算 `atan2(ImH,ReH)`，显示侧最邻近 unwrap，动态 phase range；gain 对零幅值做数值下限保护 |
| 双端口上传不稳 | screen 与 `LCR_UI.loop()` 都在 `radio.poll()`，导致单 loop 双 pump；发送器又可一次泵 4 个 notify；MTU 还会跨连接残留 | 主 loop 成为唯一 pump；一次最多 1 帧、8 ms pacing；每个 session/connect/disconnect 回到 MTU23 保守 payload，只有当前连接 MTU callback 才放大 |
| Signal Generator 无法正常进入 | 功能实现本身已经是异步 `SetTone/StopTone`，但入口被隐藏为三击 Up | 主菜单第 4 项正式暴露 `Signal Generator`；仍坚持非阻塞 job/completion 与真实 StopTone 完成后退出 |
| 第二次 BLE 上传稳定崩溃 | 旧 `stopBle()` 使用 `BLEDevice::deinit(true)`；Arduino-ESP32 3.3.11 官方实现明确说明 `release_memory=true` 会阻止 reinitialization | 改为 `BLEDevice::deinit(false)`；`run_tests.sh` 静态拒绝 `deinit(true)` 回归，并锁住 MTU reset / atomic mailbox / pacing / 单 pump invariant |

## 2. 硬件与数据手册约束

### 2.1 ESP32-S3-WROOM-1-N16R8 与最终 DNT 引脚

开发板为 ESP32-S3-WROOM-1-N16R8（16 MB Flash / 8 MB PSRAM）。板载 Micro USB 通过 CH340X + 数字隔离走 UART0 GPIO43/44；产品构建必须保持 `USB CDC On Boot = Disabled/default`。GPIO26–32 属模组内 Flash/PSRAM，GPIO33–37 在 N16R8 八线存储总线下不能作为普通 UI GPIO。

当前 DNT 真源必须以代码而不是旧文档为准：ADC GPIO1/2；LCD_CAM DAC D0..D7 = GPIO6,7,15,16,17,18,8,9；74HC595 = GPIO21,19,20。因此最终测量保留集是 `{1,2,6,7,8,9,15,16,17,18,19,20,21}`。本轮没有修改这些 DNT 文件。

TFT 使用已由 DNT 释放的 `CS=GPIO10, MOSI=11, SCLK=12, RST=13, DC=14`，write-only，无 MISO。`docs/HARDWARE_MAPPING.md` 先前仍记录旧候选“DAC 10–14 / TFT 4,5,6,7,21”，与当前 DNT 源码冲突；本轮已同步修正文档，今后禁止以该旧映射接线。

### 2.2 ST7735S 128×160

ST7735S 控制器 RAM 最大为 132×162，但 datasheet 的 128RGB×160 配置（GM=11）给出可见列 `0..127`、行 `0..159`；reset table 在 MV=0 也给出 column end=127、row end=159。因此产品唯一 UI 坐标系定义为 portrait `W=128,H=160`，`tftRotation=0`。

4-wire serial 最小 write clock cycle 为 66 ns，理论上约 15.15 MHz；产品维持 10 MHz，不以牺牲时序余量换 UI 刷新速度。若实体模块仍出现固定整体平移，先确认玻璃/GM strap/TFT_eSPI init variant，再统一修改 `tftXOffset/tftYOffset`；screen 不得各自塞 magic offset。

## 3. 输入接口语义与 EC11 差异处理

`Input` 对外契约不变：screen 只收到 `EncInc/EncDec`，不感知 A/B 细节。实现用 `(A<<1)|B` 四状态；有效相邻 Gray 转移贡献 ±1 quarter-step，反向 bounce 自动抵消，`00<->11`、`01<->10` 等双比特跳变判为非法并丢弃半格累计，避免把丢边/毛刺拼成虚假旋转。

当前 4 quarter-step 发布一个 UI detent，是为了保持旧实现“每机械格约有 A 的上升+下降两次计数”的既有语义。仓库只有通用 EC11 数据手册，没有在 BOM/板文档中把具体编码器料号钉死；ALPS EC11 系列存在 30 detent/15 pulse、20 detent/20 pulse 等不同组合。因此这个归一化必须实板验证。若具体器件确属两 detent 共一 pulse 型，只允许在输入层统一调整“transition→UI detent”映射，不允许 screen 分别补偿。方向相反同理，只能在输入层统一翻转。

自动测试与实板测试分层：host test 证明状态转移数学行为；实体板必须覆盖慢/快 CW、CCW、轻触/重触，确认一格一事件、无多跳/反跳/明显漏格。按键保留 25 ms 稳定消抖、450 ms 首次长按、110 ms 连发。

## 4. 128×160 UI 与双端口语义

顶栏固定 18 px，底部 hint 占最后 12 px，业务内容只使用中间区域。长标题/菜单使用 font1；DigitEditor 的横坐标由 `tft.width()-editor.width()-margin` 推导；所有进度条宽度从 `tft.width()` 计算。Signal Generator 状态文本已改成工程频率格式，避免 `OUT actual ...` 在 128 px 宽度越界。

主菜单四个可见入口固定为 Component、One-Port Z、Two-Port H、Signal Generator。Component 的 F0/F1 都用 5 位频率编辑，合法域为 10..10000 Hz 且 F0<F1。One-Port/Two-Port 三字段配置均使用 portrait 纵向排布。

Two-Port 的 canonical dataset 永远是无量纲复 H。TFT sealed preview 使用左轴 gain[dB]、右轴 phase[deg]；phase 为了连续绘线允许显示侧 unwrap，但上传 `re_h/im_h` 不做 unwrap、不增加冗余派生列。网站 `twoPortCsv.ts` 当前从复 H 正确推导 dB/degree，属于已经足够好的实现，本轮保持现状。

## 5. BLE 生命周期、跨 task 同步与吞吐

`RadioManager` 是 BLE 唯一 owner。测量或 Signal Generator 激励活动期间必须满足 `radio_lock`：BLE Off；只有 sweep 完成真实 StopTone、dataset seal 且用户确认后才启动 BLE。

生命周期为 `Off -> Starting -> Advertising -> Connected -> Sending -> Connected`；Back 后 `Stopping -> Off`。停止只调用 `BLEDevice::deinit(false)`；绝不允许 `deinit(true)`。每次 init 重新创建 server/service/characteristic，每次 deinit 立即清空所有 GATT raw pointer；初始化失败也必须回真正的 `Off`，不能留下“stack 已 deinit、状态却 Error”的半初始化对象。

BLE callback 与 Arduino loop 属不同 FreeRTOS task。`volatile` 不是 C++ 跨线程同步，因此 connect/disconnect/MTU/control command mailbox 使用 `std::atomic`；callback 只做 atomic `store`，`RadioManager::poll()` 用 `exchange` 消费。callback 内禁止 `startAdvertising/stopAdvertising/notify/deinit` 等重操作，避免 GATT callback 栈上的重入/生命周期交叉。

`LCR_UI.loop()` 是 `radio.poll()` 唯一调用点，screen 不得再次 pump。MTU 是连接属性：session start、connect、disconnect 都先恢复 BLE 默认 MTU23 所对应的 ATT payload 20 bytes，再减项目 8-byte frame header，得到 12 CSV data bytes/frame；只有当前连接的 `onMtuChanged` 到达后才扩大，且 data payload cap=128 bytes。发送器每次 poll 最多 notify 一帧并设置 8 ms pacing；浏览器 START/RESTART/ABORT/STATUS、seq、CRC 协议不变。

## 6. StoreProhibited 的结论边界

现场日志 `EXCCAUSE=0x1d StoreProhibited` 与 `EXCVADDR=0x0000011c` 说明 CPU 在向无效低地址执行 store，常见于 NULL/失效对象偏移访问；但没有与现场固件完全一致的 ELF/map，不能把 `PC=0x42069edf` 声称映射到某一行源码。

本轮能确定并修复的是重复 BLE 生命周期违反官方 API 语义，以及 double-pump、突发 notify、MTU 跨连接污染、callback 重操作等高风险组合。BLE start/stop 额外记录 session、bytes、free heap、free PSRAM。若按本计划实体板连续 20 个 session 后仍出现 panic，必须保存该次构建 ELF 与完整 backtrace，用 Xtensa addr2line 解 PC/backtrace；不再凭地址猜函数。

这也与之前“启动时 TFT_eSPI/SPI_PORT 导致 EXCVADDR≈0x10”的旧问题严格区分：旧 boot-time TFT 问题由 `USE_FSPI_PORT` / SPI2 构建门禁处理；本次是运行一段时间后的新崩溃，不能混为同一个根因。

## 7. 自动化验收与回归门禁

PR 最新 head 必须通过仓库现有六个 GitHub Actions job。firmware job 至少包含：DNT manifest/GPIO/radio-lock/Signal Generator static gates；Arduino ESP32-S3 production compile（`esp32:esp32@3.3.11`、TFT_eSPI `2.5.43`）；host `test_input`；BLE source invariant 检查拒绝 `deinit(true)`、volatile callback mailbox、screen-local double pump，以及缺失 MTU reset/atomic exchange/pacing 的回归。

最终 diff review 的硬条件：没有任何 `DO_NOT_TOUCH_*` 内容变化；没有 CSV schema/protocol version 变化；前端 Two-Port parser 继续从 `re_h/im_h` 推导 gain/phase；UI 裸 GPIO 只由 `board_profile.cpp` 统一拥有；没有给测量、BLE、Signal Generator 引入 `delay()` / busy-wait；README、`docs/HARDWARE_MAPPING.md`、BoardProfile、build/static gate 对当前引脚必须一致。

## 8. 实体开发板发布门禁

软件 CI 全绿只能证明源码/构建/host 回归通过，不能替代物理验证。实体 ESP32-S3-N16R8 + ST7735S 至少执行：

1. 上电 10 次，完整显示 128×160 portrait UI；四入口及 Config/Run/Sealed/BLE/Signal Generator 页面无越界、重叠、固定偏移，颜色/方向正确。
2. 编码器 CW/CCW 各至少 100 detent（慢速 50 + 快速 50），一机械格恰好一个 UI 事件；若具体 EC11 pulse/detent 与当前假设不同，按 §3 仅调整输入层归一化。
3. Component F0 可设且实际完成 10 Hz 起始测量；用已知 R/C/L（含 L+DCR）验证结果重复性。
4. One-Port 执行 sweep→seal→BLE→Back→第二轮；Two-Port 同样执行；两模式交替至少 20 个 BLE session，包含主动 Back、中途断连、RESTART_TRANSFER，CRC 一致且不得 panic/reboot。
5. Two-Port TFT 同时可见 gain dB 与 phase deg；网站导入同一 CSV 后在采样点的 gain/phase 语义一致（允许 TFT 像素和 rounding 差异）。
6. Signal Generator 从主菜单进入；10 Hz、1 kHz、10 kHz 各 start/stop；在 SetTone pending 与 running 两种时机按 Back，UI 不阻塞，且只有 StopTone completion 后退出。
7. 30 分钟以上 soak：idle、sweep、cancel、BLE upload、Signal Generator 交替；串口检查 `# BLE session start/stop` 的 heap/PSRAM。不得出现单调不可恢复内存下降、StoreProhibited 或 watchdog reset。

## 9. 最终 review 结论与合并规则

本轮保留了已经正确的部分而不是为了“有修改而修改”：DNT 测量核心、CSV/BLE v1 数据协议、网站 Two-Port dB/phase 推导、Signal Generator 的异步 SetTone/StopTone 状态机都保留其原有正确语义。修改集中在真正失配的输入解码、128×160 UI、菜单可达性、TFT phase 绘制与 BLE 生命周期/发送状态机。

最终代码架构形成以下闭环：DNT 是测量唯一真源；BoardProfile 是 UI GPIO/显示几何唯一真源；Input 是 EC11 归一化唯一真源；Dataset 的复 H 是 Two-Port 数据唯一真源；RadioManager 是 BLE 生命周期唯一 owner；Arduino `loop()` 是 BLE pump 唯一 owner。`docs/HARDWARE_MAPPING.md` 已与当前 DNT/BoardProfile 同步，不再保留冲突的旧候选接线。

合并策略：**只有 PR #5 最新 head 的六个 CI job 全部 `success`，再 squash merge 到 `main`；不创建 Tag。** 若 firmware compile、host regression、static gate 任一失败，先在分支修复并重新完整 review，禁止带红灯合并。实体板门禁未完成前，只能声称“软件修复已通过自动化并合入”，不能声称现场崩溃已经在硬件上百分之百消失。

### 参考真源

- Sitronix ST7735S Datasheet v1.3：128×160 memory/display mapping、MADCTL、4-line serial timing。
- 课程《开发板硬件手册》：ESP32-S3-WROOM-1-N16R8、GPIO/Flash/PSRAM/UART、USB CDC On Boot 约束。
- 仓库 `DO_NOT_TOUCH_sinwave.h` / `DO_NOT_TOUCH_lcr_measure.h` / `DO_NOT_TOUCH_lcr_adc.h`：最终测量 GPIO 真源。
- 仓库 `编码器数据手册 (1)/(2).PDF` 与 ALPS Alpine EC11 系列资料：不同料号的 pulse/detent 组合及机械接点特性。
- 课程《ESP32 蓝牙交互功能技术文档》：BLE service/notify/connection 基础；生产实现以 Arduino-ESP32 3.3.11 实际 API 为准。
- Espressif Arduino-ESP32 3.3.11 `BLEDevice.h/.cpp`：`deinit(bool release_memory=false)`；`release_memory=true` prevents reinitialization。
- Espressif ESP32-S3 Fatal Errors / Guru Meditation 文档：StoreProhibited/EXCVADDR 诊断语义与 ELF/backtrace 符号化要求。
