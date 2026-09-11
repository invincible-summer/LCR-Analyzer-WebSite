// ============================================================================
// test_component.cpp —— 单元件判型/聚合（plan.md §17.1 第 11-15 项）
//   11 apiType 一致性        12 R rs 聚合      13 C cs 聚合
//   14 L ls + rs(DCR) 聚合   15 负 DCR 不 clamp（WARN）
// ============================================================================
#include "check.h"

#include "component_meter.h"
#include "lcr_api.h"

#include <math.h>

// 构造一对 (AppZPoint, AppCalcResult)
static void mkR(AppZPoint& z, AppCalcResult& c, double f, double rs)
{
    z = AppZPoint{};
    z.fReq = z.fAct = f;
    z.reOhm = rs; z.imOhm = 0.0; z.magOhm = rs; z.phaseDeg = 0.0;
    z.D = 0.0; z.Q = 1e30; z.apiType = 'R'; z.apiStatus = 0;
    c = AppCalcResult{};
    c.type = 'R'; c.rs = rs; c.rp = rs;
    c.cs = NAN; c.ls = NAN; c.cp = NAN; c.lp = NAN;
    c.D = 0.0; c.Q = 1e30; c.apiStatus = 0;
}
static void mkC(AppZPoint& z, AppCalcResult& c, double f, double cs)
{
    z = AppZPoint{};
    z.fReq = z.fAct = f;
    const double xc = -1.0 / (2.0 * M_PI * f * cs);
    z.reOhm = 0.5; z.imOhm = xc; z.magOhm = fabs(xc);
    z.phaseDeg = -90.0; z.D = 0.001; z.Q = 1000.0;
    z.apiType = 'C'; z.apiStatus = 0;
    c = AppCalcResult{};
    c.type = 'C'; c.cs = cs; c.rs = 0.5; c.cp = NAN;
    c.ls = NAN; c.lp = NAN; c.D = 0.001; c.Q = 1000.0; c.apiStatus = 0;
}
static void mkL(AppZPoint& z, AppCalcResult& c, double f, double ls, double dcr)
{
    z = AppZPoint{};
    z.fReq = z.fAct = f;
    const double xl = 2.0 * M_PI * f * ls;
    z.reOhm = dcr; z.imOhm = xl; z.magOhm = hypot(dcr, xl);
    z.phaseDeg = atan2(xl, dcr) * 180.0 / M_PI;
    z.D = dcr / xl; z.Q = xl / dcr;
    z.apiType = 'L'; z.apiStatus = 0;
    c = AppCalcResult{};
    c.type = 'L'; c.ls = ls; c.rs = dcr;
    c.cs = NAN; c.cp = NAN; c.lp = NAN;
    c.D = z.D; c.Q = z.Q; c.apiStatus = 0;
}

int main()
{
    const double f5[5] = {100.0, 178.9, 320.0, 571.0, 1000.0};

    // ---- 11. 类型不一致 -> UNKNOWN（不强行猜）----------------------------
    {
        AppZPoint z[5]; AppCalcResult c[5];
        for (int i = 0; i < 5; ++i) mkR(z[i], c[i], f5[i], 1000.0 + 10 * i);
        z[2].apiType = 'C'; c[2].type = 'C'; c[2].cs = 1e-7;   // 1 点异常（允许）
        ComponentEstimate e = summarizeComponent(z, c, 5);
        CHECK(e.type == ComponentEstimate::Type::Resistor);
        CHECK(e.nValid == 5);
        CHECK(e.nConsistent == 4);
        // 2 点异常 -> UNKNOWN
        z[3].apiType = 'C'; c[3].type = 'C'; c[3].cs = 1e-7;
        e = summarizeComponent(z, c, 5);
        CHECK(e.type == ComponentEstimate::Type::Unknown);
    }
    // 有效点 <3 -> UNKNOWN
    {
        AppZPoint z[2]; AppCalcResult c[2];
        mkR(z[0], c[0], 100.0, 1000.0);
        mkR(z[1], c[1], 200.0, 1000.0);
        const ComponentEstimate e = summarizeComponent(z, c, 2);
        CHECK(e.type == ComponentEstimate::Type::Unknown);
    }
    // apiStatus!=0 / NaN 点不算有效
    {
        AppZPoint z[5]; AppCalcResult c[5];
        for (int i = 0; i < 5; ++i) mkR(z[i], c[i], f5[i], 1000.0);
        z[0].apiStatus = -3;                    // 失败点
        z[1].reOhm = NAN;                       // 非有限
        c[2].apiStatus = -1;                    // 换算失败
        z[3].apiType = 'E';                     // sweep 失败标记
        const ComponentEstimate e = summarizeComponent(z, c, 5);
        CHECK(e.nValid == 1);
        CHECK(e.type == ComponentEstimate::Type::Unknown);   // <3
    }

    // ---- 12. R: median(rs) ------------------------------------------------
    {
        AppZPoint z[5]; AppCalcResult c[5];
        const double rs[5] = {998.0, 1002.0, 1001.0, 1005.0, 999.0};
        for (int i = 0; i < 5; ++i) mkR(z[i], c[i], f5[i], rs[i]);
        const ComponentEstimate e = summarizeComponent(z, c, 5);
        CHECK(e.type == ComponentEstimate::Type::Resistor);
        CHECK_NEAR(e.rOhm, 1001.0, 1e-12);      // median{998,999,1001,1002,1005}
        CHECK(!e.dcrWarn);
    }

    // ---- 13. C: median(cs) --------------------------------------------------
    {
        AppZPoint z[5]; AppCalcResult c[5];
        const double cs[5] = {99e-9, 101e-9, 100e-9, 102e-9, 98e-9};
        for (int i = 0; i < 5; ++i) mkC(z[i], c[i], f5[i], cs[i]);
        const ComponentEstimate e = summarizeComponent(z, c, 5);
        CHECK(e.type == ComponentEstimate::Type::Capacitor);
        CHECK_NEAR(e.cFarad, 100e-9, 1e-15);
    }

    // ---- 14/15. L: median(ls) + median(rs)=DCR；负 DCR 不 clamp -----------
    {
        AppZPoint z[5]; AppCalcResult c[5];
        const double ls[5] = {9.9e-3, 10.1e-3, 10.0e-3, 10.2e-3, 9.8e-3};
        const double dcr[5] = {8.0, 9.0, 8.5, 8.2, 8.8};
        for (int i = 0; i < 5; ++i) mkL(z[i], c[i], f5[i], ls[i], dcr[i]);
        const ComponentEstimate e = summarizeComponent(z, c, 5);
        CHECK(e.type == ComponentEstimate::Type::Inductor);
        CHECK_NEAR(e.lHenry, 10.0e-3, 1e-12);   // median
        CHECK_NEAR(e.dcrOhm, 8.5, 1e-12);       // median(DCR)
        CHECK(!e.dcrWarn);
    }
    {
        // 负 DCR：不 max(0,.) 钳位，判异常并置 WARN（plan.md §6.4）
        AppZPoint z[5]; AppCalcResult c[5];
        for (int i = 0; i < 5; ++i) mkL(z[i], c[i], f5[i], 10e-3, -0.5 - 0.1 * i);
        const ComponentEstimate e = summarizeComponent(z, c, 5);
        CHECK(e.type == ComponentEstimate::Type::Inductor);
        CHECK(e.dcrOhm < 0.0);
        CHECK(e.dcrWarn);
    }
    return testSummary("test_component");
}
