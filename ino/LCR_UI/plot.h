// ============================================================================
// plot.h —— Bode 图绘制（对数频率轴 + 双 Y 轴 + 游标）
// ----------------------------------------------------------------------------
// 面向 ILI9341 320x240 横屏的小型科学绘图组件，供扫频界面使用：
//   * X 轴：对数频率（10 Hz ~ 10 kHz），主刻度按十倍频；
//   * 左 Y 轴：幅度量（|Z| 自动选对数/线性；增益 dB 线性），1-2-5 优选刻度；
//   * 右 Y 轴：相位（线性，通常固定 −180~180）；
//   * 支持逐段增量绘制（扫频进行中曲线实时生长）与整条重绘；
//   * 游标为垂直虚线，由界面层在重绘后调用 drawCursor 叠加。
// ============================================================================

#pragma once

#include <stdint.h>

class BodePlot {
public:
    /// 数据区位置与大小（轴标签的留白由调用方扣减后传入）
    void setup(int x, int y, int w, int h);

    void setXRange(double f0, double f1);              ///< 对数 X 轴范围 (Hz)
    void setMagRange(bool logY, double v0, double v1); ///< 左轴：v0<v1
    void setPhRange(double v0, double v1);             ///< 右轴：v0<v1
    void setLegend(const char* magLbl, const char* phLbl);

    void drawFrame();                                  // 框/网格/刻度/图例
    void drawCurveMag(const double* f, const double* v, int n, uint16_t color);
    void drawCurvePh(const double* f, const double* v, int n, uint16_t color);
    /// 增量绘制第 i 个点（连线自 i-1 起），i>=1 有效
    void drawSegMag(const double* f, const double* v, int i, uint16_t color);
    void drawSegPh(const double* f, const double* v, int i, uint16_t color);
    void drawCursor(double f);                         // 垂直虚线游标

    int xOfFreq(double f) const;

private:
    int yOfMag(double v) const;
    int yOfPh(double v) const;
    double magMap(double v) const;                     // 对数轴取 log10

    int m_x = 0, m_y = 0, m_w = 10, m_h = 10;
    double m_f0log = 1, m_f1log = 4;                   // log10(f)
    bool   m_magLog = false;
    double m_mag0 = 0, m_mag1 = 1;                     // 对数模式下为 log10 值
    double m_ph0 = -180, m_ph1 = 180;
    const char* m_magLbl = "MAG";
    const char* m_phLbl = "PH";
};
