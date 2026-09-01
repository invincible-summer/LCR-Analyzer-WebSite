// ============================================================================
// bt_link.h —— 蓝牙数据通路（经典蓝牙 SPP / BluetoothSerial）
// ----------------------------------------------------------------------------
// 作用：把 ESP32 上的测量数据送到「LCR 网站」一侧，保留并打通现有数据链路。
// 传输模型：行协议（一行一条 JSON 控制消息或一条 CSV 数据行，\n 结尾），
// 对端（手机/电脑）运行 tools/bt_bridge.py，把消息按 docs/api_contract.md
// 转成 HTTP 请求发给现有 FastAPI 后端 —— 网站前端无需任何改动。
//
// 线协议（设备 -> 桥）：
//   {"t":"scan.s","device":"LCR-UI","note":"...","npts":31}   开始扫频（头部）
//   {"t":"scan.f","k":0,"d":[100.0,200.0,...]}                频率表分段(≤64/段)
//   {"t":"scan.go"}                                            频率表发送完毕
//   {"t":"pt.h","f":1000.0,"dt":1.56e-5,"n":1023,"g":[tz,vg,cg],   点头部
//              "ratio":2.001,"ph":-45.2,"m":1}   本地幅相结果; m:1=单端口 2=双端口
//   {"t":"pt.v","k":0,"d":[123,124,...]}      激励通道原始码分片(k=起始下标)
//   {"t":"pt.i","k":0,"d":[...]}              响应通道原始码分片
//   {"t":"pt.e","ok":1}                        一个频点结束
//   {"t":"scan.e","points":31}                 整次扫频结束
//   {"t":"csv.b","name":"..."} / CSV 行 / {"t":"csv.e","rows":31}     导出 CSV
// 桥 -> 设备（调试命令，均回显）：
//   PING -> PONG ； NAME? -> 设备名 ； ID? -> 固件/协议标识
//
// 设计要点：
//   * 所有发送先进 6KB 环形缓冲，poll() 每次最多冲刷 ~1KB，绝不长时间阻塞 UI；
//   * 缓冲满且蓝牙吞吐不足时按行丢弃并计数（droppedLines 可查），不丢已发数据；
//   * 原始码 + 增益原样上传，码值 -> 物理 V/A 的换算在桥端完成（单一出处）。
// ============================================================================

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "partner_api.h"

class BtLink {
public:
    /// 初始化并开始 SPP 广播（setup 中调用一次）
    bool begin();

    /// 主循环周期调用：冲刷发送缓冲 + 处理下行命令 + 连接状态沿检测
    void poll();

    bool connected() const { return m_connected; }

    /// 连接状态是否发生变化（读后清零；界面用它刷新顶栏 BT 标记）
    bool takeStatusChanged();

    /// 行式发送（自动补 '\n'）。大数组请用 sendLinef 分片调用。
    void sendLine(const char* line);
    void sendLinef(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    // ---- api_contract 封装 -------------------------------------------------
    void sendScanStart(const char* device, const double* freqs, int n, const char* note);
    /// 上传一个频点：头部（含本地幅相结果与端口模式）+ 两路原始码分片 + 结束标记
    void sendPoint(const AdcSampleResult& s, double ratio, double phaseDeg,
                   bool onePort);
    void sendScanEnd(int points);
    /// 扫频结果导出为 CSV（Excel 可直接打开；任务书 xlsx 选做项的轻量实现）
    void sendCsvSweep(const char* name, const double* f, const double* y,
                      const double* p, int n, bool onePort);

    uint32_t droppedLines() const { return m_dropped; }

private:
    void flushSome();                 // 向 SPP 冲刷一段缓冲
    bool pushRing(const char* s, int len);
    void handleRx();                  // 下行命令解析

    static constexpr int RING_SIZE = 6144;
    char    m_ring[RING_SIZE];
    volatile int m_head = 0, m_tail = 0;   // 环形缓冲读写下标
    char    m_rxBuf[40] = {0};             // 下行命令行缓冲
    int     m_rxLen = 0;
    bool    m_connected = false;
    bool    m_statusChanged = false;
    uint32_t m_dropped = 0;
};

extern BtLink bt;
