# LCR_UI —— ESP32 本地仪表固件（TFT 彩屏 + 按键 + 编码器）

> LCR 项目的 ESP32 Arduino 固件：在设备本地提供完整的「信号发生器 /
> 单频点复阻抗测量 / 幅频-相频特性扫描」交互界面与全部 DSP 计算，
> 并保留经典蓝牙(SPP)通路把原始波形转发给现有 LCR 网站后端
> （`docs/api_contract.md` 契约，`tools/bt_bridge.py` 桥接）。

---

## 目录

- [功能总览](#功能总览)
- [目录结构](#目录结构)
- [硬件与接线](#硬件与接线)
- [构建与烧录](#构建与烧录)
- [TFT_eSPI 配置（必做）](#tft_espi-配置必做)
- [操作说明](#操作说明)
- [接口契约（给队友）](#接口契约给队友)
- [算法说明](#算法说明)
- [蓝牙数据通路](#蓝牙数据通路)
- [测试](#测试)
- [已知限制与后续优化](#已知限制与后续优化)

---

## 功能总览

| # | 功能 | 说明 |
|---|------|------|
| 1 | 信号发生器 | 频率 5 位数字位编辑（10~10000 Hz）、正弦/方波/三角、占空比、波形预览；OK 启动/暂停（freq=0）/恢复，显示实际输出频率 |
| 2 | 单频点测量 | 频率 + 单/双端口复选框；一次采样 -> 正弦拟合 -> 幅度比/相位差 -> 复阻抗（R/X/\|Z\|/辐角/等效 C·L/等效电阻）或增益 dB/相位；**结果页预留一行** |
| 3 | 幅相频特性 | 对数扫频（起始/终止/每十倍频点数/端口）；双色曲线（\|Z\|+辐角 或 增益+相位）、对数频率轴、自动量程、左右键游标读数；启发式识别高通/低通/带通/带阻+阶数、单端口等效电路估计；OK 一键蓝牙导出 CSV |

所有计算（正弦拟合、复阻抗、识别）均在 ESP32 本地完成，不依赖服务器。

## 目录结构

```
ino/
├── README.md                  本文档
├── LCR_UI/                    Arduino 工程（文件夹名与 .ino 同名）
│   ├── LCR_UI.ino             入口：setup/loop 与模块装配
│   ├── hw_config.h            ★ 全部硬件配置的唯一来源（引脚/屏参/换算/蓝牙）
│   ├── partner_api.h          ★ 队友接口的权威声明 + 幅相约定（对接契约）
│   ├── partner_stubs.cpp      队友接口的 weak 参考实现（合成数据，可独立演示）
│   ├── input.h / input.cpp    4 按键（消抖/连发）+ EC11 编码器（中断解码）
│   ├── display.h / display.cpp TFT 初始化/主题配色/控件（DigitEditor、Checkbox）
│   ├── screens.h              Screen 基类 + 屏幕栈 + 四个界面声明
│   ├── screen_menu.cpp        主菜单
│   ├── screen_siggen.cpp      信号发生器界面
│   ├── screen_measure.cpp     单频点测量界面
│   ├── screen_sweep.cpp       扫频界面（配置/逐点测量/游标/识别）
│   ├── plot.h / plot.cpp      Bode 图组件（对数轴/双 Y 轴/网格/曲线/游标）
│   ├── dsp_fit.h / dsp_fit.cpp 三参数正弦最小二乘拟合（纯 C++，可 PC 单测）
│   ├── analysis.h / analysis.cpp 幅相换算 + 滤波器识别 + 等效电路估计
│   └── bt_link.h / bt_link.cpp 蓝牙 SPP 数据通路（api_contract 行协议）
├── test/test_dsp.cpp          dsp_fit/analysis 的 g++ 本机单测（27 项）
└── tools/
    ├── bt_bridge.py           PC 端 蓝牙->HTTP 桥（对接现有 FastAPI 后端）
    ├── run_tests.sh           运行本机单测
    └── build_check.sh         arduino-cli 编译验证（不改库文件）
```

## 硬件与接线

默认配置面向 **ESP32 DevKit（经典 ESP32）+ ILI9341 240x320 SPI 彩屏
+ 4 个独立按键 + EC11 编码器**。所有引脚集中在
[`LCR_UI/hw_config.h`](LCR_UI/hw_config.h)——**换硬件只改这一个文件**。

| 外设 | 信号 | ESP32 引脚 | 说明 |
|------|------|-----------|------|
| TFT 彩屏 (SPI) | SCLK | GPIO18 | VSPI 时钟 |
| | MISO | GPIO19 | 可不接（本工程不读屏） |
| | MOSI | GPIO23 | 数据 |
| | CS   | GPIO5  | 片选 |
| | DC   | GPIO2  | 数据/命令（strapping 脚，接 DC 无碍，常见接法） |
| | RST  | GPIO4  | 复位 |
| | BL/LED | 3.3V（默认） | 需程控背光时改 `TFT_PIN_BL` |
| 按键 x4 | LEFT | GPIO32 | 一端接 GPIO，另一端接 GND，内部上拉 |
| | RIGHT | GPIO33 | |
| | BACK  | GPIO25 | |
| | OK    | GPIO26 | |
| 编码器 EC11 | A | GPIO27 | 内部上拉，C 端接 GND |
| | B | GPIO14 | |
| | 按键 Z | GPIO13 | 可选；`ENC_SW_AS_OK=true` 时等效 OK 键 |

注意事项：
- **避开** GPIO34~39（输入专用、无内部上拉）；GPIO0/12/15 为 strapping 脚尽量不用。
- 若板卡是 **ESP32-S3/C3**：无经典蓝牙 SPP，`bt_link` 需换 BLE 实现（接口不变，
  见[蓝牙数据通路](#蓝牙数据通路)）；其余功能不受影响。
- 屏幕方向由 `TFT_ROTATION`（默认 1 = 横屏 320x240）决定，所有界面按横屏设计。

### 编码器校准

`ENC_COUNTS_PER_DETENT`（默认 2）：旋转一格若数值跳 2 格改成 4，
反向跳 1 格改成 1。常见 EC11 用 A 相双边沿计数时每格 2 个计数。

## 构建与烧录

环境：`arduino-cli`、esp32 核心 3.3.x、TFT_eSPI 2.5.x。

```bash
# 1) 先按下一节配置 TFT_eSPI 的 User_Setup.h（一次性）

# 2) 编译验证（CI 式，不依赖 User_Setup.h 修改，通过 -D 注入配置）
bash ino/tools/build_check.sh

# 3) 烧录（按实际串口修改；--upload 沿用 build_check 的全部参数再加串口）
arduino-cli compile --upload --fqbn esp32:esp32:esp32 \
    --build-property "compiler.cpp.extra_flags=-DUSER_SETUP_LOADED -DILI9341_DRIVER -DTFT_MOSI=23 -DTFT_MISO=19 -DTFT_SCLK=18 -DTFT_CS=5 -DTFT_DC=2 -DTFT_RST=4 -DLOAD_GLCD -DLOAD_FONT2 -DLOAD_FONT4 -DLOAD_FONT7 -DSPI_FREQUENCY=27000000" \
    --port /dev/ttyUSB0 ino/LCR_UI

# 4) 串口监视（调试日志，115200）
arduino-cli monitor --port /dev/ttyUSB0 --config baudrate=115200
```

首次烧录后即可完整体验三个界面（队友接口未接入时自动使用合成数据，
见 [partner_stubs.cpp](LCR_UI/partner_stubs.cpp)：单端口=并联 RC 1kΩ/1µF，
双端口=一阶低通 fc≈1.6kHz）。

## TFT_eSPI 配置（必做）

TFT_eSPI 的引脚在**库目录** `~/Arduino/libraries/TFT_eSPI/User_Setup.h` 中配置
（Arduino 不允许 sketch 覆盖）。将该文件中 `ESP32` 相关定义改为与
`hw_config.h` 一致（等价于使用下面内容替换原有定义）：

```cpp
// >>> LCR_UI 必需配置（与 LCR_UI/hw_config.h 保持一致）<<<
#define ILI9341_DRIVER
#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS    5
#define TFT_DC    2
#define TFT_RST   4
#define LOAD_GLCD      // font1  8px（坐标刻度）
#define LOAD_FONT2     // font2 16px（标签正文）
#define LOAD_FONT4     // font4 26px（数值）
#define LOAD_FONT7     // font7 48px 七段码（大号频率显示）
#define SPI_FREQUENCY  27000000   // ILI9341 数据手册串行写上限 10MHz，
                                  // 27MHz 为稳定超频惯例；花屏则降回 27M 以下
```

> 说明：`build_check.sh` 不依赖此修改（编译期 -D 注入同一组定义），
> 但**烧录到真实硬件前必须完成**，否则库会按 ESP8266 默认引脚初始化。

## 操作说明

通用：`<` `>` = 左/右键，`OK` = 确定，`BACK` = 返回，`ENC` = 编码器旋转。
按键长按可连发（游标快速移动）；编码器按钮默认等效 OK。

| 界面 | 操作 |
|------|------|
| 主菜单 | ENC / `<` `>` 选择 1/2/3，OK 进入 |
| 信号发生器 | `<` `>` 移动数位光标（移出数字即跳到下一字段：频率->波形->占空比）；ENC 调当前位数字 0-9 循环 / 切波形 / 调占空比；OK 启动输出 -> 再按暂停（freq=0）-> 再按恢复；BACK 停止并退出 |
| 单频点 | 频率编辑同上；`<` `>` 在频率与复选框间切换焦点；ENC 旋转切换 1-PORT/2-PORT；OK 测量并显示结果；OK 重测 / BACK 回配置 |
| 扫频配置 | `<` `>` 字段间移动（起始/终止/点数/端口），ENC 编辑；OK 开始（校验 F0<F1 且 10~10kHz） |
| 扫频进行中 | 曲线实时生长；BACK 中止 |
| 扫频结果 | `<` `>`（支持长按连发）移动游标，ENC 快移 x5；读数行显示频率 + |Z|/增益 + 相位；OK 经蓝牙导出 CSV；BACK 回配置 |

## 接口契约（给队友）

接口声明见 [`LCR_UI/partner_api.h`](LCR_UI/partner_api.h)（**权威版本**，
语义变更请先改这里并同步本节）。本工程调用以下 4 个函数；

```cpp
double generateWave(int freq, WaveType waveType, double dutyCycle);   // 返回实际频率，0=失败；freq=0 停止
AdcSampleResult measureImpedanceAtFreq(int freq, bool isOnePort);     // 阻塞采样一次
ImpedanceCalcResult calculateImpedance(ImpedanceCalcInput input);     // 单端口 -> Z
GainPhaseResult calculateGainPhase(ImpedanceCalcInput input);         // 双端口 -> 增益/相位
```

### ★ amplitudeRatio / phaseDiff 的统一定义

我方计算并填入 `ImpedanceCalcInput` 的两个字段按下述定义（两种模式公式一致）：

```
amplitudeRatio = A(outBuffer) / A(inBuffer)     原始 ADC 码单位（不折算增益）
phaseDiff     = φ(inBuffer) − φ(outBuffer)      单位：度，(−180, 180]

单端口:  ratio = V码/I码         → |Z| = ratio × transimpedanceGain × currentGain / voltageGain
                              ∠Z = −phaseDiff（phaseDiff = 电流超前电压，容性为正）
双端口:  ratio = Vin码/Vout码 = 1/|H|  → gainDb = −20·log10(ratio × 通道增益比)
                              phaseDiff = 输出超前输入为正
```

即：**幅度比一律「激励通道/响应通道」，相位差一律「响应相位 − 激励相位」**。
参考消费方式见 `partner_stubs.cpp` 中的两个 weak 实现。

### 对接流程（无需删除任何文件）

1. 把真实实现的 `.cpp` 直接放进 `ino/LCR_UI/`（与 `partner_stubs.cpp` 并列）；
2. 同名函数的强定义会在链接期**自动覆盖** weak 参考实现；
3. 若模拟前端的码值->物理量换算与 stub 假设不同，只需在实现内部处理，
   我方界面不依赖换算细节（显示完全来自你们的返回值）。

### 其他约定

- `dutyCycle` 取 **0.0~1.0**（正弦忽略）。
- `measureImpedanceAtFreq` 返回的缓冲区需保证到**下一次调用前**有效（我方不释放）。
- 采样窗建议 ≥10 周期、每周 ≥32 点（`docs/api_contract.md` 时间约定）；
  正弦拟合在窗长不足整周期时相位精度下降。
- 屏幕显示文案为英文（TFT_eSPI 内置字体无中文点阵），代码注释为中文。

## 算法说明

**三参数正弦最小二乘拟合（`dsp_fit.cpp`，与后端 `app/dsp/sine_fit.py` 同源）**

对每路采样序列拟合 `x[k] = a·sin(ωt_k) + b·cos(ωt_k) + c`（ω 来自
`actualFreq`，t_k 来自 `samplingFreq`），构造 3x3 法方程并用 Cholesky 求解：

- 幅度 `A = hypot(a,b)`，相位 `φ = atan2(b,a)`，直流 `c`，残差 RMS 作为质量指示；
- 相对 FFT 单 bin：已知频率下无泄漏、无需加窗、相位干净（AWGN 下最优估计）；
- 两路各拟一次 -> 幅度比 + 相位差（上节定义）。

**滤波器识别 / 等效电路估计（`analysis.cpp`，选做功能，界面标注 EST）**

- 滤波器：增益曲线三点中值平滑 -> 按两端 ±3dB 判据分类高/低/带通/带阻 ->
  阻带段（maxG−6dB 以下）对 log10(f)-dB 做最小二乘，斜率÷20dB/dec ≈ 阶数；
- 等效电路：相位均值判 R/C/L 属性，R 随频率的趋势区分串/并联，
  元件值取高频端估计（电抗大、相对误差小）；支持 R、R+C 串、R||C、
  R+L 串、RLC 串联谐振（X 先增后减判谐振）。

## 蓝牙数据通路

保留并把测量数据送达现有 LCR 网站的链路（固件侧 `bt_link.cpp`，
PC 侧 `tools/bt_bridge.py`）：

```
ESP32 --经典蓝牙SPP(行协议)--> bt_bridge.py --HTTP(api_contract)--> FastAPI 后端 --> Vue 前端
```

- 设备名 `LCR-UI`（`hw_config.h` 可改）；连接状态实时显示在屏幕顶栏 `BT*`；
- 行协议（完整定义见 `bt_link.h` 头注释）：`scan.s/scan.f/scan.go` 开始扫频
  -> 每点 `pt.h`（含本地幅相结果与端口模式）+ `pt.v/pt.i` 原始码分片 + `pt.e`
  -> `scan.e` 结束；`csv.b/.../csv.e` 导出 CSV（Excel 可直接打开，
  即任务书 .xlsx 选做项的轻量实现）；`PING/NAME?/ID?` 调试命令；
- 发送走 6KB 环形缓冲，每圈最多冲刷 1KB，**不阻塞 UI**；蓝牙断开自动清空；
- 原始码 + 三级增益原样上传，码值->物理 V/A 换算在桥端集中完成
  （`--vref` 参数，默认 3.3V）。

桥的用法：

```bash
# Linux：先把 ESP32 绑定成 rfcomm（MAC 用 hcitool scan 查）
sudo rfcomm bind rfcomm0 <ESP32-MAC>
python3 ino/tools/bt_bridge.py --port /dev/rfcomm0 --backend http://127.0.0.1:8001

# Windows（已配对）：蓝牙串口 SPP 会有两个 COM 口，选「输出」那个
python ino/tools/bt_bridge.py --port COM7 --backend http://127.0.0.1:8001
```

> 若需要 **BLE/Web Bluetooth**（手机浏览器直连）方案：保持 `bt_link.h`
> 的类接口不变，新增 BLE-Notify 实现替换 `bt_link.cpp` 即可，界面与协议层零改动。

## 测试

```bash
bash ino/tools/run_tests.sh      # PC 端单测（g++，无 Arduino 依赖，27 项断言）
bash ino/tools/build_check.sh    # 固件编译验证（esp32:esp32:esp32）
```

单测覆盖：无噪/含噪正弦拟合精度、角度折算、幅度比与相位差的方向约定
（单端口 V/I 与电流超前为正、双端口 1/|H| 与输出超前为正）、
低通/高通/带通识别与一阶/二阶阶数、并联 RC/串联 RL 等效模型判别。
当前状态：**27/27 通过；固件编译通过（Flash 86%，RAM 20%）。**

## 已知限制与后续优化

- [ ] 中文界面：TFT_eSPI 需 .vlw 点阵字体文件 + LittleFS，当前用英文文案；
- [ ] .xlsx 原生导出（当前 CSV，Excel 直接可开；xlsx 需在设备端实现 zip 打包）；
- [ ] OSL 校准：后端 `app/dsp/calibration.py` 已就绪，固件端待加校准流程
      （单频点结果页的「预留行」即为显示校准状态预留）；
- [ ] 键盘输入频率（任务书可选）：GPIO 紧张，可先用串口命令行扩展；
- [ ] 蓝牙吞吐不足时按行丢弃并计数（`bt.droppedLines()`），可加 RS232 流控；
- [ ] BluetoothSerial 在 esp32 核心 4.x 将默认移除，届时固定 3.x 或迁 BLE；
- [ ] 阶数识别对窄带/高 Q 网络误差偏大（渐近线未充分展开时）；等效电路
      估计为启发式，不处理多谐振网络（网站端矢量拟合可做精确分析）。
