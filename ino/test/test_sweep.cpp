// ============================================================================
// test_sweep.cpp —— 频率网格 / 2-3 chunk 规划 / SweepEngine 编排全路径
// ----------------------------------------------------------------------------
// 覆盖 plan.md §17.1：
//   1  对数频率网格            2  偶数 N 的 2 点 chunk
//   3  奇数 N 的 2+3 chunk     4  chunk 端点与全局几何频率一致
//   5  取消后不再提交新 chunk   6  StopTone 未完成不能 TransferReady
//   7  Z API 失败点不进 CSV     8  NaN/Inf 不进 CSV
//   9  少于 4 个 Z 点不可进入网站拟合状态
//   10 H magnitude/phase -> Re/Im
// （16 CRC / 17 BLE seq / 18 metadata v2 在 test_csv / test_misc）
// ============================================================================
#include "check.h"
#include "test_mocks.h"

#include "dataset.h"
#include "measurement_types.h"
#include "sweep_engine.h"

#include <math.h>
#include <string.h>

// 把引擎推到终态（每轮 mock 全执行 + poll；t 按步进保证越过静默期限）
static void driveToEnd(MockLcrService& svc, SweepEngine& eng, uint32_t& t)
{
    int guard = 0;
    while (eng.state() == SweepState::Measuring || eng.state() == SweepState::Stopping) {
        svc.runToIdle();
        eng.poll(t);
        t += 25;                       // > kQuietGuardMs，越过 seal 静默期
        if (++guard > 100000) break;
    }
}

int main()
{
    // ---- 1. 对数频率网格 --------------------------------------------------
    {
        SweepConfig cfg{};
        cfg.fStartHz = 100.0; cfg.fStopHz = 10000.0;
        cfg.pointsPerDecade = 10; cfg.maxPoints = 257;
        double f[257];
        const size_t n = buildFrequencyPlan(cfg, f, 257);
        CHECK(n == 21);                       // 2 个十倍频 -> 21 点
        CHECK_NEAR(f[0], 100.0, 1e-12);       // 首尾精确
        CHECK_NEAR(f[n - 1], 10000.0, 1e-12);
        for (size_t i = 1; i < n; ++i) CHECK(f[i] > f[i - 1]);
        // 相邻点等比
        const double r = f[1] / f[0];
        for (size_t i = 1; i < n - 1; ++i)
            CHECK_NEAR(f[i + 1] / f[i], r, 1e-9);
        // 越界配置被拒
        SweepConfig bad = cfg; bad.fStartHz = 5.0;
        CHECK(buildFrequencyPlan(bad, f, 257) == 0);
    }

    // ---- 2/3/4. chunk 规划：偶数 N / 奇数 N / 端点一致性 -------------------
    {
        double g8[8];
        for (int i = 0; i < 8; ++i) g8[i] = 100.0 * pow(10.0, i / 7.0);
        SweepChunk c[8];
        const size_t nC = planSweepChunks(g8, 8, c, 8);
        CHECK(nC == 4);
        for (size_t k = 0; k < 4; ++k) {
            CHECK(c[k].nPts == 2);
            CHECK_NEAR(c[k].fStartHz, g8[2 * k], 0.0);
            CHECK_NEAR(c[k].fStopHz, g8[2 * k + 1], 0.0);
        }

        double g9[9];
        for (int i = 0; i < 9; ++i) g9[i] = 100.0 * pow(10.0, i / 8.0);
        const size_t nC9 = planSweepChunks(g9, 9, c, 8);
        CHECK(nC9 == 4);
        CHECK(c[3].nPts == 3);
        CHECK_NEAR(c[3].fStartHz, g9[6], 0.0);
        CHECK_NEAR(c[3].fStopHz, g9[8], 0.0);
        // 3 点块中点 = 几何中项 = 全局网格第 7 点（DNT 3 点 sweep 同序列）
        CHECK_NEAR(sqrt(c[3].fStartHz * c[3].fStopHz), g9[7], 1e-6);
        for (size_t k = 0; k < 3; ++k) CHECK(c[k].nPts == 2);
        // N=2 单块 / N=3 单块
        double g2[2] = {100.0, 200.0};
        CHECK(planSweepChunks(g2, 2, c, 8) == 1 && c[0].nPts == 2);
        double g3[3] = {100.0, 200.0, 400.0};
        CHECK(planSweepChunks(g3, 3, c, 8) == 1 && c[0].nPts == 3);
    }

    // ---- 5/6/7/8/9. SweepEngine 全路径（mock 后端）------------------------
    {
        MockLcrService svc;
        SweepEngine eng(svc);
        SweepConfig cfg{};
        cfg.kind = MeasurementKind::OnePortImpedance;
        cfg.fStartHz = 100.0; cfg.fStopHz = 1000.0;
        cfg.pointsPerDecade = 4;       // 1 dec -> 5 点（奇数 -> 2+3 chunk）
        cfg.maxPoints = 257;

        // 强制一个点失败（第 3 个网格点）：失败点不进 CSV、不伪造 0
        double grid[257];
        const size_t n = buildFrequencyPlan(cfg, grid, 257);
        CHECK(n == 5);
        svc.forceFailAt.push_back(grid[2]);

        CHECK(eng.start(cfg) == SweepStatus::Ok);
        uint32_t t = 1000;
        driveToEnd(svc, eng, t);

        CHECK(eng.state() == SweepState::TransferReady);
        const OnePortDataset* d = eng.sealedOnePortDataset();
        CHECK(d != nullptr);
        CHECK(d->nPoints == 4);                        // 5 计划 - 1 失败
        CHECK(d->diag.plannedPoints == 5);
        CHECK(d->diag.failedPoints == 1);
        CHECK_NEAR(d->diag.failures[0].requestedHz, grid[2], 1e-9);
        CHECK(d->diag.failures[0].apiStatus == -3);
        // CSV 内容：失败点频率绝不出现；有效行 = 4
        CHECK(strstr(d->csv, "f,re,im") != nullptr);
        CHECK(strstr(d->csv, "lcr-z-csv-v2") != nullptr);
        CHECK(strstr(d->csv, "measurement_backend=DO_NOT_TOUCH_lcr_api") != nullptr);
        CHECK(strstr(d->csv, "calibration_state=cal:3/10,open:ok,short:--") != nullptr);
        CHECK(strstr(d->csv, "drive_vrms") == nullptr);   // 杜撰元数据已删
        CHECK(strstr(d->csv, "factory-none") == nullptr);
        int rows = 0;
        for (const char* p = d->csv; (p = strchr(p, '\n')) != nullptr; ++p)
            if (p[1] >= '0' && p[1] <= '9') ++rows;
        CHECK(rows == 4);
        // CRC 覆盖 exact bytes
        CHECK(d->crc32 == crc32Of((const uint8_t*)d->csv, d->csvLen));

        // 提交序列核对：1 个 ReadCalibrationStatus + 2 个 SweepZChunk(2/3) + 1 StopTone
        int nCal = 0, nChunk = 0, nStop = 0;
        for (const LcrJob& j : svc.submittedJobs) {
            if (j.kind == LcrJobKind::ReadCalibrationStatus) ++nCal;
            else if (j.kind == LcrJobKind::SweepZChunk) ++nChunk;
            else if (j.kind == LcrJobKind::StopTone) ++nStop;
        }
        CHECK(nCal == 1); CHECK(nChunk == 2); CHECK(nStop == 1);
        // chunk 请求频率 = 全局网格端点（requested，非新网格）
        const LcrJob* chunks[2] = {nullptr, nullptr};
        int ci = 0;
        for (const LcrJob& j : svc.submittedJobs)
            if (j.kind == LcrJobKind::SweepZChunk) chunks[ci++] = &j;
        CHECK(chunks[0]->pointCount == 2);
        CHECK_NEAR(chunks[0]->fStartHz, grid[0], 0.0);
        CHECK_NEAR(chunks[0]->fStopHz, grid[1], 0.0);
        CHECK(chunks[1]->pointCount == 3);
        CHECK_NEAR(chunks[1]->fStartHz, grid[2], 0.0);
        CHECK_NEAR(chunks[1]->fStopHz, grid[4], 0.0);
    }

    // ---- 6. StopTone 未完成不能 TransferReady ------------------------------
    {
        MockLcrService svc;
        SweepEngine eng(svc);
        SweepConfig cfg{};
        cfg.kind = MeasurementKind::OnePortImpedance;
        cfg.fStartHz = 100.0; cfg.fStopHz = 400.0;
        cfg.pointsPerDecade = 20;      // 6 点
        cfg.maxPoints = 257;
        CHECK(eng.start(cfg) == SweepStatus::Ok);
        uint32_t t = 0;
        // 只执行测量类 job，扣住 StopTone 不执行
        while (eng.state() == SweepState::Measuring) {
            svc.runToIdle();
            eng.poll(t);
            t += 5;
        }
        CHECK(eng.state() == SweepState::Stopping);   // 提交了 StopTone 等待中
        // mock 执行 StopTone 事件 + 时间越过静默期 -> 才可能 seal
        svc.runToIdle();
        eng.poll(t);
        eng.poll(t + 1000);
        CHECK(eng.state() == SweepState::TransferReady);
    }


    // ---- 5. 取消：当前块完成后不再提交新 chunk；不封存 ---------------------
    {
        MockLcrService svc;
        SweepEngine eng(svc);
        SweepConfig cfg{};
        cfg.kind = MeasurementKind::OnePortImpedance;
        cfg.fStartHz = 100.0; cfg.fStopHz = 10000.0;
        cfg.pointsPerDecade = 10;      // 21 点 -> 11 chunks
        cfg.maxPoints = 257;
        CHECK(eng.start(cfg) == SweepStatus::Ok);
        uint32_t t = 0;
        int guard = 0;
        bool cancelled = false;
        while ((eng.state() == SweepState::Measuring ||
                eng.state() == SweepState::Stopping) && guard++ < 1000) {
            svc.runToIdle();
            eng.poll(t);
            t += 10;
            if (!cancelled) {
                // 第一个 chunk 已提交后立即取消
                bool sawChunk = false;
                for (const LcrJob& j : svc.submittedJobs)
                    if (j.kind == LcrJobKind::SweepZChunk) sawChunk = true;
                if (sawChunk) { eng.cancel(); cancelled = true; }
            }
        }
        CHECK(cancelled);
        int chunksAfter = 0, stopAfter = 0;
        for (const LcrJob& j : svc.submittedJobs) {
            if (j.kind == LcrJobKind::SweepZChunk) ++chunksAfter;
            if (j.kind == LcrJobKind::StopTone) ++stopAfter;
        }
        CHECK(chunksAfter == 1);               // 仅取消前那 1 块，之后无新块
        CHECK(stopAfter == 1);                 // 收尾 StopTone 恰一次
        CHECK(eng.state() == SweepState::Cancelled);
        CHECK(eng.sealedOnePortDataset() == nullptr);   // 取消不封存
    }

    // ---- 9. 少于 4 个 Z 点：Insufficient，不生成可拟合数据集 ----------------
    {
        MockLcrService svc;
        SweepEngine eng(svc);
        SweepConfig cfg{};
        cfg.kind = MeasurementKind::OnePortImpedance;
        cfg.fStartHz = 100.0; cfg.fStopHz = 1000.0;
        cfg.pointsPerDecade = 2;      // 3 点
        cfg.maxPoints = 257;
        CHECK(eng.start(cfg) == SweepStatus::Ok);
        uint32_t t = 0;
        driveToEnd(svc, eng, t);
        CHECK(eng.state() == SweepState::Insufficient);
        CHECK(eng.sealedOnePortDataset() == nullptr);
    }

    // ---- 10. H magnitude/phase -> Re/Im（纯数学 + 双口编排全路径）----------
    {
        double re = -1, im = -1;
        wMagPhaseToComplex(2.0, 60.0, &re, &im);
        CHECK_NEAR(re, 1.0, 1e-12);              // 2*cos(60)
        CHECK_NEAR(im, sqrt(3.0), 1e-12);        // 2*sin(60)
        wMagPhaseToComplex(0.5, -90.0, &re, &im);
        CHECK_NEAR(re, 0.0, 1e-12);
        CHECK_NEAR(im, -0.5, 1e-12);
        wMagPhaseToComplex(NAN, 10.0, &re, &im);
        CHECK(isnan(re) && isnan(im));

        MockLcrService svc;
        SweepEngine eng(svc);
        SweepConfig cfg{};
        cfg.kind = MeasurementKind::TwoPortTransfer;
        cfg.fStartHz = 100.0; cfg.fStopHz = 1000.0;
        cfg.pointsPerDecade = 5;      // 6 点
        cfg.maxPoints = 257;
        CHECK(eng.start(cfg) == SweepStatus::Ok);
        uint32_t t = 0;
        driveToEnd(svc, eng, t);
        CHECK(eng.state() == SweepState::TransferReady);
        const TwoPortDataset* d = eng.sealedTwoPortDataset();
        CHECK(d != nullptr);
        CHECK(d->nPoints == 6);
        CHECK(strstr(d->csv, "lcr-h-csv-v2") != nullptr);
        CHECK(strstr(d->csv, "f,re_h,im_h") != nullptr);
        CHECK(strstr(d->csv, "calibration_state=raw_w_path") != nullptr);
        for (int i = 0; i < 6; ++i) {
            const double x = d->points[i].f / svc.wFcHz;
            const double mag = 1.0 / sqrt(1.0 + x * x);
            const double ph = -atan(x) * 180.0 / M_PI;
            double expRe, expIm;
            wMagPhaseToComplex(mag, ph, &expRe, &expIm);
            CHECK_NEAR(d->points[i].reH, expRe, 1e-9);
            CHECK_NEAR(d->points[i].imH, expIm, 1e-9);
        }
    }

    // ---- 8b. NaN/Inf 记录不进 CSV（formatter 直测）--------------------------
    {
        ZPointRec recs[3] = {{100.0, 10.0, -1.0}, {NAN, 5.0, 5.0}, {200.0, INFINITY, 0.0}};
        char csv[256];
        const size_t n = formatOnePortCsv(recs, 3, "cal:0/10,open:--,short:--",
                                          csv, sizeof(csv));
        CHECK(n > 0);
        CHECK(strstr(csv, "100,10,-1") != nullptr);
        CHECK(strstr(csv, "nan") == nullptr);
        CHECK(strstr(csv, "inf") == nullptr);
        int rows = 0;
        for (const char* p = csv; (p = strchr(p, '\n')) != nullptr; ++p)
            if (p[1] >= '0' && p[1] <= '9') ++rows;
        CHECK(rows == 1);
    }

    return testSummary("test_sweep");
}
