// ============================================================================
// bt_link.cpp —— 蓝牙 SPP 数据通路实现
// ============================================================================

#include "bt_link.h"

#include "hw_config.h"

#include <BluetoothSerial.h>
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

BtLink bt;

static BluetoothSerial s_serialBT;

// ---------------------------------------------------------------------------
bool BtLink::begin()
{
    // 经典蓝牙 SPP。注意：ESP32-S3/C3 无经典蓝牙，仅支持 BLE——
    // 若换用 S3 需将本文件替换为 BLE 实现并保持 bt_link.h 接口不变。
    const bool ok = s_serialBT.begin(BT_DEVICE_NAME);
    s_serialBT.setTimeout(0);          // available()/read() 全部非阻塞
    return ok;
}

// ---------------------------------------------------------------------------
bool BtLink::pushRing(const char* s, int len)
{
    const int used = (m_head - m_tail + RING_SIZE) % RING_SIZE;
    const int free_ = RING_SIZE - 1 - used;
    if (len + 1 > free_) {
        flushSome();                   // 先尽力腾出空间
        const int used2 = (m_head - m_tail + RING_SIZE) % RING_SIZE;
        if (len + 1 > RING_SIZE - 1 - used2) {
            ++m_dropped;               // 仍放不下：整行丢弃（保持行完整性）
            return false;
        }
    }
    for (int i = 0; i < len; ++i) {    // 环形拷贝（含 '\n'，调用方已在行尾补好）
        m_ring[m_head] = s[i];
        m_head = (m_head + 1) % RING_SIZE;
    }
    return true;
}

// ---------------------------------------------------------------------------
void BtLink::flushSome()
{
    if (!m_connected) {                // 未连接：清空缓冲，避免陈旧数据堆积
        if (m_head != m_tail) { m_head = 0; m_tail = 0; }
        return;
    }
    // 每次最多冲刷 1KB，控制单次阻塞时间（SPP 底层缓冲满时 write 会等待）
    int budget = 1024;
    while (m_head != m_tail && budget > 0) {
        const int contig = (m_tail < m_head) ? (m_head - m_tail)
                                             : (RING_SIZE - m_tail);
        const int n = (contig < budget) ? contig : budget;
        const int w = s_serialBT.write((const uint8_t*)(m_ring + m_tail), n);
        if (w <= 0) break;             // 底层阻塞/失败：下轮再试
        m_tail = (m_tail + w) % RING_SIZE;
        budget -= w;
    }
}

// ---------------------------------------------------------------------------
void BtLink::sendLine(const char* line)
{
    const int len = (int)strlen(line);
    char buf[600];
    int n;
    if (len + 1 < (int)sizeof(buf)) {
        n = snprintf(buf, sizeof(buf), "%s\n", line);
    } else {
        ++m_dropped;                   // 单行超长（不应发生）：丢弃
        return;
    }
    pushRing(buf, n);
}

void BtLink::sendLinef(const char* fmt, ...)
{
    char buf[600];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof(buf) - 1) { ++m_dropped; return; }
    buf[n] = '\n';
    pushRing(buf, n + 1);
}

// ---------------------------------------------------------------------------
void BtLink::handleRx()
{
    while (s_serialBT.available() > 0) {
        const char c = (char)s_serialBT.read();
        if (c == '\n' || c == '\r') {
            if (m_rxLen == 0) continue;
            m_rxBuf[m_rxLen] = 0;
            if (strcmp(m_rxBuf, "PING") == 0)        sendLine("PONG");
            else if (strcmp(m_rxBuf, "NAME?") == 0)  sendLine(BT_DEVICE_NAME);
            else if (strcmp(m_rxBuf, "ID?") == 0)
                sendLine("LCR-UI v1 proto=api_contract see ino/README.md");
            m_rxLen = 0;
        } else if (m_rxLen < (int)sizeof(m_rxBuf) - 1) {
            m_rxBuf[m_rxLen++] = c;
        }
    }
}

// ---------------------------------------------------------------------------
bool BtLink::takeStatusChanged()
{
    const bool c = m_statusChanged;
    m_statusChanged = false;
    return c;
}

// ---------------------------------------------------------------------------
void BtLink::poll()
{
    // 连接状态沿检测（UI 顶栏 BT 标记随之亮/灭）
    const bool conn = s_serialBT.hasClient();
    if (conn != m_connected) {
        m_connected = conn;
        m_statusChanged = true;
        if (!conn && m_head != m_tail) { m_head = 0; m_tail = 0; }  // 断开清缓冲
    }

    handleRx();
    flushSome();
}

// ---------------------------------------------------------------------------
// ---- api_contract 封装 ------------------------------------------------------
// ---------------------------------------------------------------------------
void BtLink::sendScanStart(const char* device, const double* freqs, int n,
                           const char* note)
{
    // 头部（总点数）+ 频率表分段（scan.f，每段 ≤64 个）+ 开始标记
    sendLinef("{\"t\":\"scan.s\",\"device\":\"%s\",\"note\":\"%s\",\"npts\":%d}",
              device, note, n);
    static const int CH = 64;
    char line[560];
    for (int k = 0; k < n; k += CH) {
        int off = snprintf(line, sizeof(line), "{\"t\":\"scan.f\",\"k\":%d,\"d\":[", k);
        for (int i = k; i < k + CH && i < n; ++i)
            off += snprintf(line + off, sizeof(line) - off, "%s%.1f",
                            (i == k) ? "" : ",", freqs[i]);
        snprintf(line + off, sizeof(line) - off, "]}");
        sendLine(line);
    }
    sendLine("{\"t\":\"scan.go\"}");
}

void BtLink::sendPoint(const AdcSampleResult& s, double ratio, double phaseDeg,
                       bool onePort)
{
    // 点头部：频率/采样间隔/长度/三级增益 + 本地幅相结果（冗余便于诊断）
    sendLinef("{\"t\":\"pt.h\",\"f\":%.3f,\"dt\":%.6g,\"n\":%d,"
              "\"g\":[%d,%d,%d],\"ratio\":%.5f,\"ph\":%.2f,\"m\":%d}",
              s.actualFreq, 1.0 / s.samplingFreq, s.sampleLength,
              s.transimpedanceGain, s.voltageGain, s.currentGain,
              ratio, phaseDeg, onePort ? 1 : 2);

    // 两路原始码：每 64 点一行分片（k 为该段起始下标）
    static const int CH = 64;
    char d[560];
    for (int c = 0; c < 2; ++c) {
        const short* buf = (c == 0) ? s.outBuffer : s.inBuffer;
        const char* tag = (c == 0) ? "pt.v" : "pt.i";
        for (int k = 0; k < s.sampleLength; k += CH) {
            int off = snprintf(d, sizeof(d), "{\"t\":\"%s\",\"k\":%d,\"d\":[",
                               tag, k);
            for (int i = k; i < k + CH && i < s.sampleLength; ++i) {
                off += snprintf(d + off, sizeof(d) - off, "%s%d",
                                (i == k) ? "" : ",", buf[i]);
                if (off >= (int)sizeof(d) - 16) break;   // 防御：不溢出
            }
            snprintf(d + off, sizeof(d) - off, "]}");
            sendLine(d);
        }
    }
    sendLine("{\"t\":\"pt.e\",\"ok\":1}");
}

void BtLink::sendScanEnd(int points)
{
    sendLinef("{\"t\":\"scan.e\",\"points\":%d}", points);
}

void BtLink::sendCsvSweep(const char* name, const double* f, const double* y,
                          const double* p, int n, bool onePort)
{
    sendLinef("{\"t\":\"csv.b\",\"name\":\"%s\",\"cols\":\"f_hz,%s,phase_deg\"}",
              name, onePort ? "z_mag_ohm" : "gain_db");
    sendLine(onePort ? "f_hz,z_mag_ohm,phase_deg" : "f_hz,gain_db,phase_deg");
    for (int i = 0; i < n; ++i)
        sendLinef("%.2f,%.6g,%.2f", f[i], y[i], p[i]);
    sendLinef("{\"t\":\"csv.e\",\"rows\":%d}", n);
}
