// ============================================================================
// plot.cpp —— Bode 图绘制实现
// ============================================================================

#include "plot.h"

#include "display.h"

#include <math.h>
#include <stdio.h>

// 1-2-5 优选：返回覆盖 [v0,v1] 的「好看」刻度步长
static double niceStep(double span)
{
    const double raw = span / 5.0;          // 期望 4~6 根刻度线
    const double mag = pow(10.0, floor(log10(raw)));
    const double norm = raw / mag;          // 1~10
    double s;
    if (norm < 1.5)       s = 1;
    else if (norm < 3.5)  s = 2;
    else if (norm < 7.5)  s = 5;
    else                  s = 10;
    return s * mag;
}

// 频率刻度短标签：10 / 100 / 1k / 10k
static void fmtTick(double v, char* buf, int len)
{
    if (v >= 1e6)      snprintf(buf, len, "%gM", v / 1e6);
    else if (v >= 1e3) snprintf(buf, len, "%gk", v / 1e3);
    else               snprintf(buf, len, "%g", v);
}

// ---------------------------------------------------------------------------
void BodePlot::setup(int x, int y, int w, int h)
{
    m_x = x; m_y = y; m_w = w; m_h = h;
}

void BodePlot::setXRange(double f0, double f1)
{
    m_f0log = log10(f0 > 0 ? f0 : 1);
    m_f1log = log10(f1 > f0 ? f1 : f0 * 10);
}

void BodePlot::setMagRange(bool logY, double v0, double v1)
{
    m_magLog = logY;
    if (logY) { m_mag0 = log10(v0 > 0 ? v0 : 1e-12); m_mag1 = log10(v1 > v0 ? v1 : v0 * 10); }
    else      { m_mag0 = v0; m_mag1 = (v1 > v0 ? v1 : v0 + 1); }
}

void BodePlot::setPhRange(double v0, double v1)
{
    m_ph0 = v0; m_ph1 = (v1 > v0 ? v1 : v0 + 1);
}

void BodePlot::setLegend(const char* magLbl, const char* phLbl)
{
    m_magLbl = magLbl;
    m_phLbl = phLbl;
}

// ---------------------------------------------------------------------------
int BodePlot::xOfFreq(double f) const
{
    if (f < 1e-9) f = 1e-9;
    const double t = (log10(f) - m_f0log) / (m_f1log - m_f0log);
    return m_x + (int)(t * (m_w - 1));
}

double BodePlot::magMap(double v) const
{
    return m_magLog ? log10(v > 0 ? v : 1e-12) : v;
}

int BodePlot::yOfMag(double v) const
{
    const double t = (magMap(v) - m_mag0) / (m_mag1 - m_mag0);
    const int y = m_y + (int)((1.0 - t) * (m_h - 1));
    return y < m_y ? m_y : (y > m_y + m_h - 1 ? m_y + m_h - 1 : y);   // 裁剪
}

int BodePlot::yOfPh(double v) const
{
    const double t = (v - m_ph0) / (m_ph1 - m_ph0);
    const int y = m_y + (int)((1.0 - t) * (m_h - 1));
    return y < m_y ? m_y : (y > m_y + m_h - 1 ? m_y + m_h - 1 : y);
}

// ---------------------------------------------------------------------------
void BodePlot::drawFrame()
{
    tft.fillRect(m_x, m_y, m_w, m_h, ui::C_BG);
    tft.drawRect(m_x, m_y, m_w, m_h, ui::C_AXIS);

    // ---- 垂直网格 + X 刻度（十倍频；跨度不足时补 2/5 倍频程） -------------
    const double span = m_f1log - m_f0log;
    const int decadeLo = (int)ceil(m_f0log);
    const int decadeHi = (int)floor(m_f1log);
    char buf[12];
    tft.setTextFont(1);
    for (int d = decadeLo; d <= decadeHi; ++d) {
        const int px = xOfFreq(pow(10.0, d));
        if (px <= m_x + 1 || px >= m_x + m_w - 2) continue;
        tft.drawFastVLine(px, m_y, m_h, ui::C_GRID);
        fmtTick(pow(10.0, d), buf, sizeof(buf));
        tft.setTextColor(ui::C_AXIS, ui::C_BG);
        tft.drawCentreString(buf, px, m_y + m_h + 3, 1);
        // 跨度 < 1.5 个十倍频时加 2× 与 5× 次刻度
        if (span < 1.5) {
            for (int m = 2; m <= 5; m += 3) {
                const int pm = xOfFreq(m * pow(10.0, d));
                if (pm > m_x + 1 && pm < m_x + m_w - 2) {
                    tft.drawFastVLine(pm, m_y, m_h, ui::C_GRID);
                    fmtTick(m * pow(10.0, d), buf, sizeof(buf));
                    tft.drawCentreString(buf, pm, m_y + m_h + 3, 1);
                }
            }
        }
    }

    // ---- 水平网格：左轴（幅度，实线）与右轴（相位，点线） ------------------
    // 幅度：对数轴按十倍频，线性轴按 1-2-5 步长
    if (m_magLog) {
        for (int d = (int)ceil(m_mag0); d <= (int)floor(m_mag1); ++d) {
            const double t = (d - m_mag0) / (m_mag1 - m_mag0);
            const int py = m_y + (int)((1.0 - t) * (m_h - 1));
            if (py <= m_y || py >= m_y + m_h - 1) continue;
            tft.drawFastHLine(m_x + 1, py, m_w - 2, ui::C_GRID);
            fmtTick(pow(10.0, d), buf, sizeof(buf));
            tft.setTextColor(ui::C_CH1, ui::C_BG);
            tft.drawRightString(buf, m_x - 3, py - 4, 1);
        }
    } else {
        const double step = niceStep(m_mag1 - m_mag0);
        for (double v = ceil(m_mag0 / step) * step; v <= m_mag1 + 1e-9; v += step) {
            const int py = yOfMag(v);
            if (py <= m_y || py >= m_y + m_h - 1) continue;
            tft.drawFastHLine(m_x + 1, py, m_w - 2, ui::C_GRID);
            snprintf(buf, sizeof(buf), "%g", v);
            tft.setTextColor(ui::C_CH1, ui::C_BG);
            tft.drawRightString(buf, m_x - 3, py - 4, 1);
        }
    }
    // 相位刻度（右轴，固定步长 90 度，点线网格区分于幅度）
    const double pstep = (m_ph1 - m_ph0) > 180.0 ? 90.0 : 45.0;
    for (double v = ceil(m_ph0 / pstep) * pstep; v <= m_ph1 + 1e-9; v += pstep) {
        const int py = yOfPh(v);
        if (py <= m_y || py >= m_y + m_h - 1) continue;
        for (int px = m_x + 3; px < m_x + m_w - 3; px += 6)   // 点线
            tft.drawPixel(px, py, ui::C_GRID);
        snprintf(buf, sizeof(buf), "%g", v);
        tft.setTextColor(ui::C_CH2, ui::C_BG);
        tft.drawString(buf, m_x + m_w + 3, py - 4);
    }

    // ---- 图例（数据区左上角，双色） ---------------------------------------
    tft.fillRect(m_x + 4, m_y + 4, 8, 8, ui::C_CH1);
    tft.setTextFont(1);
    tft.setTextColor(ui::C_FG, ui::C_BG);
    tft.drawString(m_magLbl, m_x + 15, m_y + 4);
    const int lx = m_x + 15 + tft.textWidth(m_magLbl, 1) + 12;
    tft.fillRect(lx, m_y + 4, 8, 8, ui::C_CH2);
    tft.drawString(m_phLbl, lx + 11, m_y + 4);
}

// ---------------------------------------------------------------------------
void BodePlot::drawCurveMag(const double* f, const double* v, int n, uint16_t color)
{
    for (int i = 1; i < n; ++i) drawSegMag(f, v, i, color);
}

void BodePlot::drawCurvePh(const double* f, const double* v, int n, uint16_t color)
{
    for (int i = 1; i < n; ++i) drawSegPh(f, v, i, color);
}

void BodePlot::drawSegMag(const double* f, const double* v, int i, uint16_t color)
{
    if (i < 1) return;
    if (!isfinite(v[i]) || !isfinite(v[i - 1])) return;   // 无效点跳过
    tft.drawLine(xOfFreq(f[i - 1]), yOfMag(v[i - 1]),
                 xOfFreq(f[i]),     yOfMag(v[i]), color);
}

void BodePlot::drawSegPh(const double* f, const double* v, int i, uint16_t color)
{
    if (i < 1) return;
    if (!isfinite(v[i]) || !isfinite(v[i - 1])) return;
    tft.drawLine(xOfFreq(f[i - 1]), yOfPh(v[i - 1]),
                 xOfFreq(f[i]),     yOfPh(v[i]), color);
}

// ---------------------------------------------------------------------------
void BodePlot::drawCursor(double f)
{
    const int px = xOfFreq(f);
    if (px <= m_x || px >= m_x + m_w) return;
    for (int y = m_y + 1; y < m_y + m_h - 1; y += 6)      // 虚线：3 亮 3 暗
        tft.drawFastVLine(px, y, 3, ui::C_ACCENT);
}
