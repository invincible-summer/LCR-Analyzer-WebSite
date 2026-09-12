// ============================================================================
// test_component.cpp —— 未知元件判型/聚合/单点详情回退
// ============================================================================
#include "check.h"

#include "component_meter.h"
#include "lcr_api.h"

#include <math.h>

static void mkR(AppZPoint& z, AppCalcResult& c, double f, double rs)
{
    z = AppZPoint{};
    z.fReq = z.fAct = f;
    z.reOhm = rs; z.imOhm = 0.0; z.magOhm = fabs(rs);
    z.phaseDeg = rs < 0.0 ? 180.0 : 0.0;
    z.D = 1e30; z.Q = 0.0; z.apiType = 'R'; z.apiStatus = 0;
    c = AppCalcResult{};
    c.type = 'R'; c.rs = rs; c.rp = rs;
    c.cs = c.ls = c.cp = c.lp = NAN;
    c.D = z.D; c.Q = z.Q; c.apiStatus = 0;
}

static void mkC(AppZPoint& z, AppCalcResult& c, double f, double cs)
{
    z = AppZPoint{};
    z.fReq = z.fAct = f;
    const double xc = -1.0 / (2.0 * M_PI * f * cs);
    z.reOhm = 0.5; z.imOhm = xc; z.magOhm = hypot(z.reOhm, xc);
    z.phaseDeg = atan2(xc, z.reOhm) * 180.0 / M_PI;
    z.D = 0.001; z.Q = 1000.0; z.apiType = 'C'; z.apiStatus = 0;
    c = AppCalcResult{};
    c.type = 'C'; c.cs = cs; c.rs = 0.5;
    const double d2 = z.reOhm*z.reOhm + z.imOhm*z.imOhm;
    const double G = z.reOhm/d2, B = -z.imOhm/d2;
    c.rp = 1.0/G; c.cp = B/(2.0*M_PI*f);
    c.ls = c.lp = NAN; c.D = z.D; c.Q = z.Q; c.apiStatus = 0;
}

static void mkL(AppZPoint& z, AppCalcResult& c, double f, double ls, double dcr)
{
    z = AppZPoint{};
    z.fReq = z.fAct = f;
    const double xl = 2.0 * M_PI * f * ls;
    z.reOhm = dcr; z.imOhm = xl; z.magOhm = hypot(dcr, xl);
    z.phaseDeg = atan2(xl, dcr) * 180.0 / M_PI;
    z.D = fabs(dcr / xl); z.Q = 1.0 / z.D;
    z.apiType = 'L'; z.apiStatus = 0;
    c = AppCalcResult{};
    c.type = 'L'; c.ls = ls; c.rs = dcr;
    const double d2 = dcr*dcr + xl*xl;
    const double G = dcr/d2, B = -xl/d2;
    c.rp = fabs(G) > 1e-30 ? 1.0/G : NAN;
    c.lp = B < 0.0 ? -1.0/(2.0*M_PI*f*B) : NAN;
    c.cs = c.cp = NAN; c.D = z.D; c.Q = z.Q; c.apiStatus = 0;
}

int main()
{
    const double f5[5] = {100.0, 178.9, 320.0, 571.0, 1000.0};

    // 单个类型异常容忍；两个异常必须 UNKNOWN，并保留中位频点详情。
    {
        AppZPoint z[5]; AppCalcResult c[5];
        for (int i = 0; i < 5; ++i) mkR(z[i], c[i], f5[i], 1000.0 + 10*i);
        z[2].apiType = 'C'; c[2].type = 'C'; c[2].cs = 1e-7;
        ComponentEstimate e = summarizeComponent(z, c, 5);
        CHECK(e.type == ComponentEstimate::Type::Resistor);
        z[3].apiType = 'C'; c[3].type = 'C'; c[3].cs = 1e-7;
        e = summarizeComponent(z, c, 5);
        CHECK(e.type == ComponentEstimate::Type::Unknown);
        CHECK(e.detailIndex == 2);                  // 中位频率真实测得 -> 必须保留
        CHECK(e.nR == 3 && e.nC == 2);
    }

    // “数据错误不算没测出来”：中位点 calc 失败，详情仍选择中位测量 Z。
    {
        AppZPoint z[5]; AppCalcResult c[5];
        for (int i = 0; i < 5; ++i) mkR(z[i], c[i], f5[i], 1000.0);
        c[2].apiStatus = -1;
        const ComponentEstimate e = summarizeComponent(z, c, 5);
        CHECK(e.detailIndex == 2);
        CHECK(e.nMeasured == 5);
        CHECK(e.nCalcValid == 4);
    }

    // 中位频点根本没测出来 -> 从低频向高频找第一个真实测得点。
    {
        AppZPoint z[5]; AppCalcResult c[5];
        for (int i = 0; i < 5; ++i) mkR(z[i], c[i], f5[i], 1000.0);
        z[2].apiStatus = -3; z[2].apiType = 'E'; z[2].reOhm = z[2].imOhm = NAN;
        const ComponentEstimate e = summarizeComponent(z, c, 5);
        CHECK(e.detailIndex == 0);
        CHECK(e.nMeasured == 4);
    }

    // R 中位数聚合，同时代表点频率必须与该真实样本绑定。
    {
        AppZPoint z[5]; AppCalcResult c[5];
        const double rs[5] = {998.0, 1002.0, 1001.0, 1005.0, 999.0};
        for (int i = 0; i < 5; ++i) mkR(z[i], c[i], f5[i], rs[i]);
        const ComponentEstimate e = summarizeComponent(z, c, 5);
        CHECK(e.type == ComponentEstimate::Type::Resistor);
        CHECK_NEAR(e.rOhm, 1001.0, 1e-12);
        CHECK(e.representativeIndex == 2);
        CHECK_NEAR(e.representativeFreqHz, f5[2], 1e-12);
    }

    {
        AppZPoint z[5]; AppCalcResult c[5];
        const double cs[5] = {99e-9, 101e-9, 100e-9, 102e-9, 98e-9};
        for (int i = 0; i < 5; ++i) mkC(z[i], c[i], f5[i], cs[i]);
        const ComponentEstimate e = summarizeComponent(z, c, 5);
        CHECK(e.type == ComponentEstimate::Type::Capacitor);
        CHECK_NEAR(e.cFarad, 100e-9, 1e-15);
    }

    {
        AppZPoint z[5]; AppCalcResult c[5];
        const double ls[5] = {9.9e-3, 10.1e-3, 10.0e-3, 10.2e-3, 9.8e-3};
        const double dcr[5] = {8.0, 9.0, 8.5, 8.2, 8.8};
        for (int i = 0; i < 5; ++i) mkL(z[i], c[i], f5[i], ls[i], dcr[i]);
        const ComponentEstimate e = summarizeComponent(z, c, 5);
        CHECK(e.type == ComponentEstimate::Type::Inductor);
        CHECK_NEAR(e.lHenry, 10.0e-3, 1e-12);
        CHECK_NEAR(e.dcrOhm, 8.5, 1e-12);
        CHECK(!e.dcrWarn);
    }

    // 负 DCR 但电抗占主导：保留 INDUCTOR + WARN，不能误标 ACTIVE。
    {
        AppZPoint z[5]; AppCalcResult c[5];
        for (int i = 0; i < 5; ++i) mkL(z[i], c[i], f5[i], 10e-3, -0.5 - 0.1*i);
        const ComponentEstimate e = summarizeComponent(z, c, 5);
        CHECK(e.type == ComponentEstimate::Type::Inductor);
        CHECK(e.dcrOhm < 0.0);
        CHECK(e.dcrWarn);
    }

    // 接近 180 degree 的负阻是真正强有源证据：不得再显示 UNKNOWN。
    {
        AppZPoint z[5]; AppCalcResult c[5];
        for (int i = 0; i < 5; ++i) {
            mkR(z[i], c[i], f5[i], -1000.0);
            z[i].imOhm = 5.0;
            z[i].magOhm = hypot(z[i].reOhm, z[i].imOhm);
            z[i].phaseDeg = atan2(z[i].imOhm, z[i].reOhm) * 180.0 / M_PI;
        }
        const ComponentEstimate e = summarizeComponent(z, c, 5);
        CHECK(e.type == ComponentEstimate::Type::Active);
        CHECK(e.detailIndex == 2);
    }

    // 单频显示分类：NEGATIVE-R 只允许接近 180 degree。
    {
        AppZPoint z; AppCalcResult c;
        mkR(z, c, 1000, 1000);
        CHECK(classifyImpedanceNature(z) == ImpedanceNature::Resistive);
        mkC(z, c, 1000, 100e-9);
        CHECK(classifyImpedanceNature(z) == ImpedanceNature::Capacitive);
        mkL(z, c, 1000, 10e-3, 5.0);
        CHECK(classifyImpedanceNature(z) == ImpedanceNature::Inductive);
        mkR(z, c, 1000, -1000);
        z.imOhm = 5; z.phaseDeg = 179.7;
        CHECK(classifyImpedanceNature(z) == ImpedanceNature::NegativeResistive);
        z.phaseDeg = 170.0; z.imOhm = 100.0;
        CHECK(classifyImpedanceNature(z) == ImpedanceNature::Inductive);
    }

    return testSummary("test_component");
}
