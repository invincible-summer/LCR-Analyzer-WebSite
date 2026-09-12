# LCR_UI — ESP32-S3 LCR 仪表固件（v4.1.0 · DNT 测量核心 + 应用编排层）

面向自制 ESP32-S3 开发板（WROOM-1-**N16R8**）的测量固件：TFT 本地仪表 +
三个产品模式 + BLE GATT v1 上传。测量硬件链（LCD_CAM 并行正弦 → 电阻网络
DAC、74HC595 → TIA/V/I 增益与双端口控制、两路 ADC）全部固化在
**DO_NOT_TOUCH** 头文件中并经实板验证 —— 应用层只做编排与后处理。

## 测量架构（唯一依赖边）

```
UI / SweepEngine / ComponentScreen / Dataset / BLE
        │
        ▼
   lcr_api.h            应用层唯一测量接口（host 可编译）
        │
        ▼
   lcr_api.cpp          唯一 include DO_NOT_TOUCH_lcr_api.h 的编译单元；
        │               独立 FreeRTOS 测量 Worker（init 与测量同 task）
        ▼
   DO_NOT_TOUCH_lcr_api.h   已验证硬件链（不可修改）
        │
        ▼
   LCD_CAM 8bit → 电阻网络 DAC；74HC595 → 增益/双端口；两路 ADC → Z/H
```

为什么必须这样（详见 `lcr_api.h` 文件头中文说明与仓库 `plan.md`）：

1. DNT ADC 的 ISR 只通知 `lcr_api_init()` 时保存的 task —— init 与全部
   测量必须在同一个 Worker task（否则死锁）；
2. DNT 测量 API 是同步函数 —— UI 非阻塞由「UI loop 提交 job + Worker
   串行执行」实现，不修改 DNT；
3. 取消只能发生在 DNT 调用边界 —— 当前 2/3 点块完成后停止 + StopTone；
4. 应用层不得重新实现频率生成 / ADC / 量程 / 校准 / Z/H 计算
   （`tools/static_check.sh` Gate C/D 强制）。

## 顶层三个用户模式

| 模式 | 内容 | BLE |
|---|---|---|
| 1 Component R/C/L | 5 个几何频点（API Z + calc），apiType 一致性判型 + 中位数聚合；不确定时 UNKNOWN | 不启动 |
| 2 One-Port Z Sweep | 2/3 点块扫频 → StopTone → seal → `f,re,im`（v2 CSV）| 封存后用户确认才开启 |
| 3 Two-Port H Sweep | 双端口复 H=Vout/Vin（raw W 链，如实标注 raw_w_path）→ `f,re_h,im_h` | 同上 |

隐藏诊断页（信号发生器）：主菜单 3 秒内连按 3 次 `Up` 进入；经 wrapper
调 DNT `lcr_api_set_freq`；离开页面必停激励。

## 目录

```
LCR_UI/
  DO_NOT_TOUCH_*.h      已验证测量核心（绝对不改；Gate A manifest 锁定）
  DO_NOT_TOUCH_EXAMPLE.ino.example  API 调用示例（只读参考）
  lcr_api.h/.cpp        ★ 应用层唯一测量接口 + FreeRTOS Worker（唯一 DNT 入口）
  fw_version.h          固件/协议/schema 版本（4.1.0 / 1 / v2 / v2）
  board_profile.h/.cpp  UI 外设引脚唯一出处（TFT=4/5/6/7/21 + 按键/编码器）
  measurement_types.*   编排层纯类型 + 频率网格 + H 纯数学换算（host 可编译）
  sweep_engine.*        2/3 点块编排：取消/StopTone/seal（host 可测）
  component_meter.*     apiType 一致性判型 + 中位数聚合（host 可测）
  dataset.*             封存数据集 + v2 CSV + CRC32 + metadata JSON
  ble_protocol.h        GATT v1 常量与帧编解码（host 可测）
  radio_manager.*       BLE 生命周期 + 分片发送（radio_lock 互斥）
  radio_lock.*          「测量窗口内射频静默」invariant
  input/display/screens/plot + screen_*.cpp   UI 框架与界面
  Attention/ATTENTION.md  硬件约束警示（与 AGENTS.md 同源）
test/                   host 单测（mock LcrService）+ golden fixture + rollover
tools/build_check.sh    arduino-cli ESP32-S3 编译门禁（FQBN/TFT 注入）
tools/run_tests.sh      host 单测 + golden CSV（oneport + twoport）生成
tools/static_check.sh   Gate A–J（DNT/硬件/GPIO/TFT host/Worker 契约）
tools/dnt_manifest.txt  DO_NOT_TOUCH 文件 SHA-256 清单（Gate A）
tools/bt_bridge.py      （Deprecated）旧 Classic BT→HTTP 桥，正常路径不使用
```

## 构建与烧录

```sh
arduino-cli core install esp32:esp32@3.3.11
arduino-cli lib install TFT_eSPI@2.5.43
bash ino/tools/static_check.sh
bash ino/tools/build_check.sh
bash ino/tools/run_tests.sh
```

- FQBN：`esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=default,
  PartitionScheme=app3M_fat9M_16MB`（脚本内固定）。
- **USB CDC On Boot 必须 Disabled**：本板 Micro USB 经 CH340X 隔离接
  GPIO43/44；开启 CDC 会把 Serial 引到 GPIO19/20，板载 Micro USB 收不到该 CDC。
- **TFT_eSPI 2.5.43 在 ESP32-S3 上必须定义 `USE_FSPI_PORT`**。该发布版的
  S3 默认分支把 `SPI_PORT` 设为 Arduino 的 `FSPI`，而 Arduino-ESP32 3.3.11
  在 S3 上把 `FSPI` 定义为逻辑 bus index 0；TFT_eSPI 的 S3 direct-register
  路径需要外设号 SPI2=2。默认组合可在 `tft.init()` 触发
  `StoreProhibited / EXCVADDR=0x00000010`。`build_check.sh` 显式注入
  `-DUSE_FSPI_PORT`，TFT_eSPI 2.5.43 对应路径会选择 `SPI_PORT=2`；
  `display.cpp` 再做 compile-time `SPI_PORT==2` 门禁。不要删除这一约束。
- TFT 引脚经编译期 `-D` 注入：SCK=4、MOSI=5、CS=6、DC=7、RST=21、MISO 未用。
  `static_check.sh` 强制这些值与 `board_profile.cpp` 一致。
- ST7735S v1.3 4-line serial write timing 要求 `TSCYCW >= 66 ns`，即理论
  SCL 上限约 15.15 MHz；产品固定 **10 MHz**，编译和静态门禁均拒绝更高值。
- 烧录速率 115200；一键下载电路兼容 Arduino 默认 RTS/DTR 时序。

烧录修复版后，串口在 `tft.init()` 前应首先看到类似：

```text
LCR-UI v4.1.0 booting (BLE protocol 1, z-schema v2, h-schema v2)
TFT init: TFT_eSPI 2.5.43, SPI_PORT=2, SCLK=4 MOSI=5 CS=6 DC=7 RST=21 @ 10000000 Hz
```

如果第二行中的 `SPI_PORT` 不是 2，当前受控构建本应在编译阶段失败；如果第二行出现后
仍发生 panic，则必须保存新的 ELF + backtrace 重新解码，不能继续把旧的 FSPI=0
根因套用到新故障。

## 关键设计规则（违反即 bug）

1. **DO_NOT_TOUCH 边界**：11 个 DNT 文件 hash 由 Gate A 锁定；
   `DO_NOT_TOUCH_lcr_api.h` 只允许 `lcr_api.cpp` 一个 include 点（Gate C）；
   应用层禁止出现 `out_freq/lcr_adc_/lcr_measure_/HC595_PIN_` 等低层符号
   （Gate D）；禁止恢复外部 DAC/I2S 激励/自写 ADC 假设（Gate E）。
2. **同 task 约束**：`lcr_api_init()` 与全部测量调用都在 `lcr_worker`
   task 内（ADC task-affinity，见 `lcr_api.h`）。
3. **射频互斥**：启动不初始化 BLE；测量窗口（含块间与 Stopping）射频
   静默；StopTone 完成 + seal 后才 `startBleForSealedDataset()`。
4. **取消有界且诚实**：UI 显示 STOPPING AFTER BLOCK；当前块完成 →
   StopTone 事件 → 才解除 measurement lock / 返回。
5. **actual f 全链路**：CSV `f` 用 DNT `f_act`（不用 requested）；
   失败点不进 CSV、不伪造 0；DNT 未写出参的失败路径也不得读取局部对象。
6. **无二次校准**：measure 链已校准，`lcr_api_calc(..., false)`；
   双端口如实标注 `raw_w_path`，不复用单端口校准。
7. **GPIO 集中**：UI 引脚只在 `board_profile.cpp`（Gate F 检查不与
   DNT 保留集 {1,2,8–18} / strap / PSRAM 冲突）；测量 GPIO 全在 DNT。
8. **队列有界且 completion 可靠**：job/event 队列 4/4；job submit 对 UI
   非阻塞。completion event 是状态机控制面，不能静默丢弃；队列满时只能让专用
   Worker 等待 UI 消费，不能让 `m_pendingId` 永久悬空。
9. **跨 task 状态不是 volatile 同步**：ready/busy/cancel 等使用 C++ atomic；
   FreeRTOS queue 负责 job/event 对象跨 task 传递。
10. **时间是 uptime**：`millis()` 为 `uint32_t` 回绕计数，不是 Unix epoch；
    deadline 比较必须使用回绕安全差值，内部 seal 字段名为 `sealedUptimeMs`。

## 数据流（模式 2/3）

```
UI 配置 → SweepEngine：网格 → 2/3 点块 job
  → Worker：lcr_api_sweep_z / lcr_api_sweep_w（块内 fast settle）
  ← 事件（f_act, z_re/z_im 或 h_mag/phase→re/im）
  → 失败点入 DatasetDiag；有效点入 dataset 工作区
  → StopTone（lcr_api_set_freq(0)）事件 → 20ms 静默 → seal
  → canonical v2 CSV（f,re,im / f,re_h,im_h）+ CRC32 + metadata
  →（用户确认）RadioManager/BLE GATT v1 → 浏览器 parseZCsv/parseHCsv
```

Schema/协议契约：`protocol/CSV_SCHEMA_V2.md`（v1 兼容）、
`protocol/BLE_PROTOCOL_V1.md`。硬件映射与实板验收清单：
`docs/HARDWARE_MAPPING.md`（TFT GPIO4/5/6/7/21 映射仍需实物 continuity；
控制器数据手册不能替代具体模组的 offset/invert/color 实测）。
