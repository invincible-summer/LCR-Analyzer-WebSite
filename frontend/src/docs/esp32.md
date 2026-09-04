# ESP32 接口（规划中 · 稍后完成）

> 🚧 **状态**：本页描述的功能尚未实现，接口契约为当前设计意图，落地时可能调整。

本站与 ESP32 的连接有两条通路，分别处于不同状态：

## 1. 波形上传（已实现 · HTTP）

ESP32 作为"纯采集前端"，把每个频点的原始 V/I 波形 POST 给后端，服务端完成全部 DSP（正弦拟合 → 阻抗 → 不确定度）：

```
POST /api/scan/start                 → scan_id
POST /api/scan/{scan_id}/point       → 单频点波形（voltage[] / current[] / dt / n）
```

完整契约见仓库 `docs/api_contract.md`（固件照此实现）。这条通路产出的扫描可以直接在拟合页「从历史扫描导入」使用。

## 2. 蓝牙直传测量文件（规划中 · 稍后完成）

目标形态：ESP32 固件通过 BLE（拟用 Nordic UART Service 或自定义特征）把测得的 $f/\mathrm{Re}(Z)/\mathrm{Im}(Z)$ 数据文件直接推给浏览器（Web Bluetooth API），免去组网与后端：

- 拟合页「蓝牙导入」按钮亮起，配对后流式接收 CSV；
- 收包校验、进度显示、断线重连；
- 数据落盘为与文件上传完全相同的 `ZPoint[]`，后续流程不变。

## 固件侧参考

- ESP32 本地仪表固件（TFT UI + 本地 DSP）在 `ino/` 目录独立维护，跨团队接口见 `ino/LCR_UI/partner_api.h`；
- 蓝牙通路的具体特征 UUID、MTU 与分包协议将在实现时补入 `docs/api_contract.md` 并同步本页。
