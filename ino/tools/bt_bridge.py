#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bt_bridge.py —— ESP32 蓝牙(SPP) -> LCR 网站后端(HTTP) 数据桥
================================================================
把 LCR_UI 固件经经典蓝牙发来的行协议消息（见 ino/LCR_UI/bt_link.h）翻译成
docs/api_contract.md 定义的 HTTP 请求，转发给现有 FastAPI 后端；
网站前端无需任何改动即可看到设备数据。

依赖：
  * 蓝牙串口：优先用 pyserial（pip install pyserial）；
    若未安装，Linux 下可直接用系统蓝牙绑定出的 /dev/rfcomm0（termios 裸读）。
  * HTTP：仅标准库 urllib（无需 requests）。

用法示例（Linux）：
    sudo rfcomm bind rfcomm0 00:11:22:33:44:55   # 绑定 ESP32 的蓝牙 MAC
    python3 bt_bridge.py --port /dev/rfcomm0 --backend http://127.0.0.1:8001
用法示例（Windows，配对后用出入站 COM 口）：
    python3 bt_bridge.py --port COM7 --backend http://127.0.0.1:8001

线协议（设备 -> 本桥）：
    {"t":"scan.s", ...} / {"t":"scan.f","d":[...]} / {"t":"scan.go"}
    {"t":"pt.h","f":..,"dt":..,"n":..,"g":[tz,vg,cg],"ratio":..,"ph":..,"m":1|2}
    {"t":"pt.v","k":0,"d":[codes...]} / {"t":"pt.i", ...} / {"t":"pt.e","ok":1}
    {"t":"scan.e","points":N}
    {"t":"csv.b","name":...} / <CSV 行> / {"t":"csv.e","rows":N}
"""

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime

# ---------------------------------------------------------------------------
# 串口读取：优先 pyserial，退化到 /dev/rfcomm* 的 termios 裸读
# ---------------------------------------------------------------------------
class LineReader:
    def __init__(self, port, baud=115200):
        self.name = port
        try:
            import serial  # pyserial
            self.ser = serial.Serial(port, baud, timeout=1.0)
            self.raw = None
        except ImportError:
            self.ser = None
            import fcntl
            import termios
            self.raw = os.open(port, os.O_RDWR | os.O_NOCTTY)
            attrs = termios.tcgetattr(self.raw)
            attrs[3] &= ~termios.ICANON & ~termios.ECHO  # 原始模式
            attrs[4] = attrs[5] = baud  # cfgetospeed 近似（SPP 虚拟串口无实际波特率）
            termios.tcsetattr(self.raw, termios.TCSANOW, attrs)
        self.buf = b""

    def readline(self):
        """返回一行（str，去掉行尾），超时/断开返回 None"""
        while b"\n" not in self.buf:
            try:
                if self.ser is not None:
                    chunk = self.ser.read(256)
                else:
                    import select
                    r, _, _ = select.select([self.raw], [], [], 1.0)
                    if not r:
                        continue
                    chunk = os.read(self.raw, 256)
            except OSError as e:
                print(f"[bridge] serial error: {e}", file=sys.stderr)
                return None
            if not chunk:
                continue
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode("utf-8", errors="replace").strip()

    def write(self, data: bytes):
        if self.ser is not None:
            self.ser.write(data)
        else:
            os.write(self.raw, data)


# ---------------------------------------------------------------------------
# HTTP（仅标准库）
# ---------------------------------------------------------------------------
def http_post_json(url, payload, timeout=10):
    req = urllib.request.Request(
        url, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode())


# ---------------------------------------------------------------------------
# 桥主体
# ---------------------------------------------------------------------------
class Bridge:
    def __init__(self, args):
        self.args = args
        self.io = LineReader(args.port)
        self.scan_id = None          # 当前后端 scan_id
        self.freqs = []              # scan.f 累积的频率表
        self.expect_pts = 0
        # 当前频点暂存
        self.pt = None               # dict: f/dt/n/g/m/ratio/ph
        self.v_codes = {}            # k -> [codes]
        self.i_codes = {}
        self.csv_file = None
        self.csv_rows = 0

    # -- 码值 -> 物理量（与固件侧 hw_config.h 常量保持一致） -----------------
    def to_volts(self, code, gain):
        return code * self.args.vref / 4095.0 / max(gain, 1e-9)

    # -- 消息处理 -------------------------------------------------------------
    def handle(self, obj):
        t = obj.get("t")
        if t == "scan.s":
            self.freqs, self.expect_pts = [], int(obj.get("npts", 0))
            print(f"[bridge] scan start: device={obj.get('device')} "
                  f"note={obj.get('note')} npts={self.expect_pts}")
        elif t == "scan.f":
            self.freqs.extend(obj.get("d", []))
        elif t == "scan.go":
            self._post_scan_start(obj)
        elif t == "pt.h":
            self.pt, self.v_codes, self.i_codes = obj, {}, {}
        elif t in ("pt.v", "pt.i"):
            dst = self.v_codes if t == "pt.v" else self.i_codes
            dst[int(obj.get("k", 0))] = obj.get("d", [])
        elif t == "pt.e":
            self._post_point()
        elif t == "scan.e":
            print(f"[bridge] scan end: {obj.get('points')} points "
                  f"(scan_id={self.scan_id})")
            self.scan_id = None
        elif t == "csv.b":
            name = datetime.now().strftime("%Y%m%d_%H%M%S") + "_" + \
                   str(obj.get("name", "sweep")) + ".csv"
            path = os.path.join(self.args.outdir, name)
            os.makedirs(self.args.outdir, exist_ok=True)
            self.csv_file, self.csv_rows = open(path, "w", newline=""), 0
            print(f"[bridge] csv -> {path}")
        elif t == "csv.e":
            if self.csv_file:
                self.csv_file.close()
                print(f"[bridge] csv done: {self.csv_rows} rows")
                self.csv_file = None
        else:
            print(f"[bridge] unknown msg: {obj}")

    def _post_scan_start(self, obj):
        try:
            r = http_post_json(self.args.backend + "/api/scan/start", {
                "device": self.args.device,
                "freq_list": self.freqs,
                "note": f"BT bridge: {obj.get('note', '')}",
            })
            self.scan_id = r["id"]
            print(f"[bridge] scan_id={self.scan_id} ({len(self.freqs)} freqs)")
        except Exception as e:
            print(f"[bridge] scan/start FAILED: {e}", file=sys.stderr)

    def _post_point(self):
        if not self.pt:
            return
        n = int(self.pt.get("n", 0))
        m = int(self.pt.get("m", 1))            # 1=单端口 2=双端口
        tz, vg, cg = (self.pt.get("g") or [1, 1, 1])
        v = self._flatten(self.v_codes, n)
        i = self._flatten(self.i_codes, n)
        if len(v) != n or len(i) != n:
            print(f"[bridge] point incomplete: v={len(v)} i={len(i)} n={n}",
                  file=sys.stderr)
            return
        # 单位换算（契约要求物理量 V/A）：
        #   单端口：v = 待测元件电压；i = 电流（经跨阻+电流增益）
        #   双端口：v = 网络输入电压；i 通道为输出电压（按电流增益档放大）
        voltage = [self.to_volts(x, vg) for x in v]
        if m == 1:
            current = [self.to_volts(x, tz * cg) for x in i]   # V -> A（跨阻）
        else:
            current = [self.to_volts(x, cg) for x in i]        # 输出电压
        if not self.scan_id:
            print("[bridge] point without scan_id (scan/start failed?)",
                  file=sys.stderr)
            return
        try:
            r = http_post_json(
                f"{self.args.backend}/api/scan/{self.scan_id}/point", {
                    "device": self.args.device,
                    "frequency": float(self.pt["f"]),
                    "dt": float(self.pt["dt"]),
                    "n": n - 1,
                    "voltage": voltage,
                    "current": current,
                })
            z = r.get("z_mag")
            print(f"[bridge] f={self.pt['f']:.6g}Hz -> "
                  f"|Z|={z if z is None else round(z, 4)} "
                  f"ph={r.get('z_phase_deg')}")
        except Exception as e:
            print(f"[bridge] point FAILED: {e}", file=sys.stderr)

    @staticmethod
    def _flatten(chunks, n):
        out = []
        for k in sorted(chunks):
            out.extend(chunks[k])
        return out[:n]

    def handle_csv_line(self, line):
        if self.csv_file:
            self.csv_file.write(line + "\n")
            self.csv_rows += 1

    # -- 主循环 ---------------------------------------------------------------
    def run(self):
        print(f"[bridge] listening on {self.args.name}, "
              f"backend={self.args.backend}")
        self.io.write(b"PING\n")               # 握手测试（设备回 PONG）
        while True:
            line = self.io.readline()
            if line is None:
                print("[bridge] serial lost, exiting", file=sys.stderr)
                return 1
            if not line:
                continue
            if line.startswith("{"):
                try:
                    self.handle(json.loads(line))
                except json.JSONDecodeError:
                    print(f"[bridge] bad json: {line[:60]}", file=sys.stderr)
            elif line in ("PONG",) or line.startswith("LCR-UI"):
                print(f"[bridge] device: {line}")
            else:
                self.handle_csv_line(line)     # csv.b/e 之间的纯 CSV 行
        return 0


def main():
    ap = argparse.ArgumentParser(description="ESP32 BT->HTTP bridge")
    ap.add_argument("--port", required=True, help="SPP 串口（如 /dev/rfcomm0 或 COM7）")
    ap.add_argument("--backend", default="http://127.0.0.1:8000",
                    help="LCR 后端地址（FastAPI）")
    ap.add_argument("--device", default="ESP32_LCR_BT", help="上传 device 字段")
    ap.add_argument("--vref", type=float, default=3.3, help="ADC 参考电压")
    ap.add_argument("--outdir", default=".", help="CSV 导出目录")
    args = ap.parse_args()
    sys.exit(Bridge(args).run())


if __name__ == "__main__":
    main()
