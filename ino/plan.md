# ESP32-S3 LCR 固件 2026-09-12 运行时/UI/BLE 修复与验收计划

状态：**实现已落在 `fix/ino-ui-ble-20260912` / PR #5；本文件是实现后的最终架构 review 与验收计划。合并条件是最新 head 六个 CI job 全绿；实板验证是独立硬件门禁。不得创建 Tag。**

审计基线：`main@56504745a4a010f940996d6efb83ad281e4b3f3e`。本轮只修改应用层/UI/BLE 与测试，不改任何 `DO_NOT_TOUCH_*` 测量实现，不改 CSV/BLE protocol schema，不改网站 Try1/Try2/Try3 数学算法。

## 1. 本轮问题结论

本轮报告的 9 组现象不是一个共同 bug，而是四类独立问题叠加：输入去抖、ST7735S 几何/UI 布局、双端口显示语义、BLE 生命周期/发送压力。另有一个 Signal Generator 可达性问题。

| 报告项 | 代码审计结论 | 最终处理 |
|---|---|---|
| 编码器旋转抖动 | `input.cpp` 只挂 A 相 CHANGE，B 相仅作瞬时判向；它不是完整 quadrature 解码，B 相触点抖动可改变方向判断 | A/B 两相都挂 CHANGE；使用完整 2-bit Gray 转移表；4 个有效 quarter-step 才生成一个 detent；非法双比特跳变清半格；host regression 覆盖正转、反转、bounce、glitch |
| TFT UI 错位 | `board_profile.cpp` 声明面板 128x160，却 `rotation=1` 把 UI 当 160x128；多个 screen 还按横屏绝对坐标写布局 | 产品坐标系固定为 ST7735S 原生 portrait `128x160`，`rotation=0`；Menu/Component/One-Port/Two-Port 全部按 `tft.width()/height()` 重新排版 |
| StoreProhibited | 新日志 `EXCCAUSE=0x1d`、`EXCVADDR=0x11c` 表示向接近 NULL 的非法地址写；仅凭 PC 没有匹配 ELF 不能诚实映射源码。但仓库存在一个可确定的重复上传致命生命周期错误：`BLEDevice::deinit(true)` 后再次 `BLEDevice::init()` | 修复确定缺陷并把它变为回归门禁：只允许 `deinit(false)`；BLE callback 只写 mailbox；每连接重置 MTU；单 loop 单 pump；通知节流；串口打印 BLE start/stop 的 heap/PSRAM，便于若仍有异常时用同一构建继续定位 |
| 单元件最低 50 Hz | `screen_component.cpp` 独自把 F0 写死为 `50`；核心 `INSTRUMENT_F_MIN_HZ` 已是 10 Hz | UI 改为使用 `INSTRUMENT_F_MIN_HZ=10`，上限继续服从 10 kHz 产品频段 |
| 双端口增益单位 | 真源 `H=Vout/Vin` 是无量纲复比值；网站 `twoPortCsv.ts` 已正确计算 `20log10|H|`，不是 Ohm | TFT 明确显示 `GAIN dB / PHASE deg`；上传仍保持 `f,re_h,im_h`，避免派生量重复/符号分叉 |
| 双端口 TFT 无相位 | `BodePlot` 已有 `setPhRange/drawCurvePh`，但 `screen_twoport.cpp` 只算/画 gain | 同时从复 H 计算 phase，做最邻近 unwrap 后画右轴 degree 曲线；gain 对 `|H|=0` 做 -240 dB 数值保护 |
| 双端口上传不稳 | screen 自己 `radio.poll()`，而 `LCR_UI.loop()` 又 `radio.poll()`，一次 loop 双 pump；一次 poll 最多 4 notify；第二连接还能继承上一连接的大 MTU payload | 删除 screen 内 pump；主 loop 唯一 owner；单次最多一帧并 8 ms pacing；连接/session 一律先回到 MTU23 的 12-byte CSV payload，收到本连接 MTU event 后才放大 |
| Signal Generator 无法正常进入 | 功能本身已是异步 SetTone/StopTone 状态机，但菜单故意隐藏，要求 3 秒三次 Up | 主菜单正式增加第 4 项 `Signal Generator`；原非阻塞停机/StopTone completion 约束不变 |
| 第二次 BLE 上传稳定崩溃 | `radio_manager.cpp::stopBle()` 使用 `BLEDevice::deinit(true)`；Arduino-ESP32 3.3.11 官方头/实现把参数命名为 `release_memory`，源码注释明确 `true` 会释放内部 BT stack memory 并“prevents reinitialization” | 改 `BLEDevice::deinit(false)`，并在 `run_tests.sh` 中硬性禁止 `BLEDevice::deinit(true)` 回归 |

## 2. 硬件与数据手册约束

### 2.1 ESP32-S3 开发板

板卡为 ESP32-S3-WROOM-1-N16R8（16 MB Flash / 8 MB PSRAM）。课程开发板手册要求板载 CH340X 串口路径使用 GPIO43/44，`USB CDC On Boot` 必须 Disabled；GPIO26-32 属封装内 Flash/PSRAM，GPIO33-37 在 N16R8 八线 PSRAM 配置下不可作为普通 UI 引脚。现有最终 DNT 测量接线继续拥有 GPIO1/2、6/7/8/9/15/16/17/18/19/20/21；UI/TFT 不得抢占。

TFT 继续使用已审核映射 `CS=GPIO10, MOSI=11, SCLK=12, RST=13, DC=14`，write-only，无 MISO。SPI 固定 10 MHz；ST7735S 4-wire serial datasheet 最小 write clock cycle 66 ns，对应约 15.15 MHz 上限，因此 10 MHz 保持安全余量，不为了 UI 性能升频。

### 2.2 ST7735S 128x160 几何

ST7735S 控制器自身 RAM 最大 132x162，但 datasheet 的 128RGBx160 配置（GM=11）明确给出 visible mapping：column pointer `0..127`，row pointer `0..159`；reset table 在 MV=0 时同样给出 column end 127 / row end 159。因此本项目 UI 的逻辑坐标定义为 portrait `W=128,H=160`。`board_profile.cpp::tftRotation=0` 是产品约束，不允许 screen 再通过假定 160x128 来补偿。

若实物仍出现整体固定平移，必须先确认模块玻璃/GM strap 与 TFT_eSPI init variant；只有确定是模块级可见窗口 offset 后才能修改 `tftXOffset/tftYOffset`。不能用每个 screen 各自减坐标的方式“修”硬件 offset。

## 3. 输入接口语义

`Input` 对外 API 不变：UI 仍只接收 `EncInc/EncDec`，一格机械 detent 对应一个事件。实现层使用 `(A<<1)|B` 四状态：正方向 `00->01->11->10->00`，反方向相反。相邻 bounce 往返自然正负抵消；`00<->11`、`01<->10` 等双比特跳变视为丢边/毛刺并清空当前半格，不跨毛刺拼出一个虚假 detent。

必须保留两个层次的测试。host test 验证状态机数学行为；实板用快速/慢速、正反向、轻触/重触至少各 50 格确认“无多跳、无丢格、方向一致”。如果实物编码器的机械方向与 UI 期望相反，只允许在输入层统一翻转正负语义，不允许各 screen 单独交换 EncInc/EncDec。

## 4. UI 128x160 布局约束

所有页面保持顶栏 18 px、底部 hint 最后 12 px；可交互内容只能使用 y=19..147。长字符串使用 font1；DigitEditor 的 x 坐标由 `tft.width()-editor.width()-margin` 计算。任何新增 screen 不能再出现假定 `W=160/H=128` 的 magic layout。

主菜单四项在 128 宽内用 font1，Signal Generator 不再有隐藏手势。Component 配置页 F0/F1 使用 5 位编辑器，合法域统一为 10..10000 Hz 且必须 F0<F1。One-Port 与 Two-Port 的三字段配置采用 portrait 纵向布局。运行页、sealed 页、BLE 页都把进度条宽度写成 `tft.width()-margin`。

双端口 sealed preview 的语义固定为 Bode：左轴 gain dB，右轴 phase degree。canonical dataset 永远仍是无量纲复 H；TFT 和网站都只能由 `re_h/im_h` 派生显示量。phase 绘图允许为了连续曲线做 unwrap，但上传值不做 unwrap、不新增派生 CSV 列。

## 5. BLE 生命周期、线程与吞吐约束

RadioManager 是唯一 BLE owner。测量/Signal Generator 活动期间必须满足 `radio_lock` 的互斥 invariant；只有 sealed dataset 后用户确认才启动 BLE。

生命周期固定为：`Off -> Starting -> Advertising -> Connected -> Sending -> Connected`，Back 后 `Stopping -> Off`。停止必须调用 `BLEDevice::deinit(false)`；绝不允许 `deinit(true)`。这里的 `false` 不是泄漏 workaround，而是 Arduino-ESP32 3.3.11 为“以后可以重新 init”提供的生命周期语义。每次 init 后重新创建 server/service/characteristic，每次 deinit 后立即把全部 GATT raw pointer 置空。

BLE callback 不允许执行 `startAdvertising/stopAdvertising/notify/deinit` 等重入操作，只可写轻量 mailbox：connect、disconnect、MTU、control command。`RadioManager::poll()` 在 Arduino 主 loop 消费 mailbox 并推进状态。`LCR_UI.loop()` 是 `radio.poll()` 唯一调用点；任何 screen 再调用都应被测试脚本拒绝。

MTU 是“连接属性”，不是“设备永久属性”。因此 session start、connect、disconnect 都先把可发送 CSV data payload 设为 `20 ATT payload - 8 LCR frame header = 12 bytes`；只有当前连接的 `onMtuChanged` 到达后才扩大，且项目继续 cap 在 128 CSV bytes/frame。发送器一次 poll 最多 notify 一帧，并使用 8 ms pacing。浏览器协议仍使用现有 START/RESTART/ABORT/STATUS；CRC/seq 契约不变。

## 6. StoreProhibited 的定位边界

本轮不能把用户给出的 `PC=0x42069edf` 宣称为某一行源码，因为缺少与现场固件完全一致的 ELF/map；这种映射必须使用该次构建的符号文件。可以确定的是 `EXCCAUSE=0x1d StoreProhibited` 且 `EXCVADDR=0x11c` 属低地址非法 store，符合 NULL/失效对象附近成员写入的典型形式，而重复 BLE 生命周期中恰好存在官方明确禁止的 `deinit(true)->reinit` 路径。

因此修复策略分两层：先消除已证明的生命周期缺陷；同时在 BLE session start/stop 打印 session、bytes、free heap、free PSRAM。若实板按本计划连续 20 次上传后仍能复现，下一步必须保存该次 CI/本机构建 ELF，并用 Xtensa addr2line 解 `0x42069edf` 及完整 backtrace，不再凭地址猜函数。

## 7. 自动化验收

PR 最新 head 必须通过既有六个 GitHub Actions job。firmware job 的最低要求是：DNT manifest/GPIO/radio-lock/Signal Generator static gates 全过；Arduino ESP32-S3 production compile 使用 `esp32:esp32@3.3.11`、TFT_eSPI `2.5.43`；host tests 新增 `test_input`；新增 BLE source invariant 检查拒绝 `deinit(true)`、screen 双 pump、缺失 MTU reset/mailbox/pacing。

最终 diff review 必须再次确认：没有改 `DO_NOT_TOUCH_*`；没有改 CSV schema/protocol version；Two-Port 网站 parser 仍由 `re_h/im_h` 计算 dB/degree；所有新增裸 GPIO 只存在 `board_profile.cpp`；没有 runtime `delay()/while wait hardware` 被引入测量、BLE 或 Signal Generator 状态机。

## 8. 实板发布门禁

软件 CI 通过后仍需在同一块 ESP32-S3-N16R8 + ST7735S 上做以下一轮完整验收，结果应附到 PR 或测试记录：

1. 上电 10 次均显示完整 128x160 portrait UI；四个菜单项、三个配置页、运行页、sealed/preview/BLE 页无越界、重叠、整体偏移；颜色/方向正确。
2. 编码器正反向各至少 100 detent（慢速 50 + 快速 50），每格恰好一个 UI 事件；按键 25 ms debounce 与长按 repeat 无回归。
3. Component F0 可设 10 Hz；用已知 R/C/L 在 10 Hz 起始范围实际完成测量，不只验证编辑框。
4. One-Port 完成 sweep->seal->BLE->Back->第二次 sweep；Two-Port 同样执行。连续交替至少 20 个 BLE session，含主动 Back、中途断连、RESTART_TRANSFER；不得 panic/重启，CRC 必须一致。
5. Two-Port TFT 同时可见 gain dB 与 phase degree；网站导入同一 CSV 后 gain/phase 与 TFT 在采样点数值语义一致（允许 TFT 像素/rounding 差异）。
6. Signal Generator 从主菜单正常进入；10 Hz、1 kHz、10 kHz 各 start/stop；Back 在 SetTone pending、running 两种时机均不阻塞，且只有 StopTone completion 后退出。
7. 30 分钟 soak：空闲、扫频、BLE 上传交替运行；串口持续观察 `# BLE session start/stop` 的 free heap/PSRAM。不可出现单调不可恢复内存下降或 `StoreProhibited`。

## 9. 最终 review 结论

代码级 review 通过的核心条件已经在实现中形成闭环：问题 5 的 50 Hz 是 UI 常量而不是硬件限制；问题 6 的网站 dB 语义本来正确，保留现状而不是为了“修改而修改”；问题 7 是 TFT 未调用已有 phase plot 能力；问题 9 是隐藏入口；问题 8/10 则由 BLE double-pump、发送突发、MTU 跨连接污染及不可逆 `deinit(true)` 共同放大。修复没有侵入 DNT 测量核心，也没有改变网站/协议数据真源。

合并策略：PR #5 最新 head 六个 CI job 全绿后 squash merge `main`；**不创建 Tag**。若 firmware compile 或 host regression 失败，先修分支并重新完整 review，禁止带红灯合并。实板门禁未执行前，只能声称“软件修复已合入并通过自动化验证”，不能声称现场崩溃已被物理设备百分之百验证消失。

### 参考真源

- Sitronix ST7735S Datasheet v1.3：128x160 memory/display mapping、MADCTL、4-line serial timing。
- 课程《开发板硬件手册》：ESP32-S3-WROOM-1-N16R8、GPIO/Flash/PSRAM/UART、USB CDC On Boot 约束。
- 课程《ESP32 蓝牙交互功能技术文档》：BLE service/notify/connection 的基础用法；项目生产实现以 Arduino-ESP32 3.3.11 实际 API 为准。
- Espressif Arduino-ESP32 3.3.11 `BLEDevice.h/.cpp`：`deinit(bool release_memory=false)`；源码说明 `release_memory=true` prevents reinitialization。
- Espressif ESP32-S3 Guru Meditation/Backtrace 文档：StoreProhibited/EXCVADDR 的非法 store 诊断语义与 addr2line 定位方法。
