# ESP32 上传数据契约（固件按此实现）

固件不必做任何 DSP / 阻抗计算，只需在 WiFi 上把**原始 V/I 时序**按下面格式 POST 给后端。
所有数字信号处理、阻抗计算、等效电路拟合都在服务器完成；后期改算法无需改固件。

## 时间约定

- 采样间隔 `dt`（秒），采样率 `fs = 1/dt`。
- 样本索引 `k = 0..n`，共 **n+1** 个样本；时间轴 `t[k] = k * dt`。
- 激励频率 `frequency`（Hz），角频率 `ω = 2π·frequency`。
- 建议每个频率采集 ≥10 个完整周期、每周期 ≥32 点（例如 16 周期 × 64 点 = 1024 点）。

## 1. 开始一次扫频

`POST /api/scan/start`

```json
{
  "device": "ESP32_LCR_01",
  "freq_list": [100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000],
  "note": "被测元件 DUT 标注，例如 电容C1 10µF"
}
```

响应：
```json
{
  "id": "131d44aaa6b2",        // ← scan_id，后续每个频率点都带上它
  "device": "ESP32_LCR_01",
  "freq_list": [ ... ],
  "status": "open",
  "created_at": "2026-08-10T16:25:56Z",
  "measurement_count": 0
}
```

## 2. 上传单个频率点（逐点）

`POST /api/scan/{scan_id}/point`

```json
{
  "device": "ESP32_LCR_01",
  "frequency": 1000.0,
  "dt": 0.000015625,           // = 1/(64*1000)，采样间隔（秒）
  "n": 1023,                   // 索引 0..1023 → 1024 个样本
  "voltage": [0.0123, 0.0187, 0.0250, "...", -0.0044],
  "current": [0.000123, 0.000187, 0.000250, "...", -0.000044]
}
```

字段约束（后端会校验，不符返回 422）：
- `len(voltage) == len(current) == n + 1`
- `dt > 0`，`frequency > 0`，`n >= 2`
- 单位：电压 V，电流 A（已是物理量，不是 ADC 原始码）

响应（含本频率点的阻抗结果）：
```json
{
  "id": 42, "scan_id": "131d44aaa6b2",
  "frequency": 1000.0, "dt": 0.000015625, "n": 1023,
  "z_real": 1.0, "z_imag": -159.15, "z_mag": 159.16, "z_phase_deg": -89.64,
  "R": 1.0, "X": -159.15, "D": 0.0063, "Q": 159.15, "esr": 1.0,
  "L_eq": null, "C_eq": 1.000e-6,
  "v_amp": 1.590, "v_phase_deg": 0.36, "i_amp": 0.010, "i_phase_deg": 90.0,
  "resid_rms_v": 0.0008, "resid_rms_i": 0.000005,
  "v_dc": 0.0001, "i_dc": -0.000002
}
```

> 也可以批量：`POST /api/scan/{scan_id}/batch`，body 为 `{"device": "...", "points": [ <point>, <point>, ... ]}`。

## 3. 其它端点（前端用，固件一般不调）

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/scans` | 扫描列表 |
| GET | `/api/scan/{id}` | 扫描详情（全部频率点阻抗） |
| GET | `/api/scan/{id}/measurement/{mid}` | 单点详情（原始波形 + 拟合曲线 + 残差 + FFT） |
| GET | `/api/models` | 可拟合的等效电路模型 |
| POST | `/api/fit` | `{scan_id, model}` → R/L/C/RMSE/精度 + 理论曲线 |
| GET | `/api/scan/{id}/export?format=csv\|json` | 导出 |
| WS  | `/ws/live` | 实时推送：每收到一个点广播 `{type,frequency,z_mag,z_phase_deg}` |

## 固件实现要点（精度命门）

1. **V 与 I 必须同步采样。** 分时轮采会引入时间差 Δt，在 1 kHz 下 10 µs 就带来 3.6° 相位误差。优先外挂双通道同步 ADC（如 ADS131M02 / ADS1256）；若只能分时采，精确测出 Δt 后端会补偿（目前契约未含该字段，可后续加 `i_skew`）。
2. **ESP32 自带 ADC 线性/噪声较差**，做 LCR 建议外挂 delta-sigma ADC。
3. **校准**：先做开/短/负载（OSL）校准再测 DUT，否则容性/感性相位与 D/Q 不准。
4. 上传单位是物理量（V/A）。若固件只给 ADC 原始码，需要在前端不可见处换算——建议固件侧完成 `code → V/A` 换算（参考电压、采样电阻、跨阻）。

## 最小 curl 例子

```bash
BASE=http://localhost:8001
SID=$(curl -s -X POST $BASE/api/scan/start -H 'Content-Type: application/json' \
  -d '{"device":"ESP32_01","freq_list":[1000],"note":"test"}' | python3 -c 'import sys,json;print(json.load(sys.stdin)["id"])')
# 生成一条 1000Hz 的波形（示例：略）
curl -s -X POST $BASE/api/scan/$SID/point -H 'Content-Type: application/json' \
  -d '{"device":"ESP32_01","frequency":1000.0,"dt":1.5625e-5,"n":1023,
       "voltage":[...],"current":[...]}'
```
