// ============================================================================
// test_mocks.h —— ILcrService 的 host 端 mock（ino/test 专用）
// ----------------------------------------------------------------------------
// plan.md §17.1：host 测试不再模拟另一套 ADC/测量算法，只 mock
// “LcrService 后端结果”，测试编排层（SweepEngine / 判型 / 封存）。
// Mock 行为模型（与 lcr_api.cpp Worker 的可观察契约一致）：
//   * submit 分配单调 id、入队（容量 4）；
//   * step() 模拟 Worker 串行执行当前 job：按脚本生成事件；
//   * requestCancel 后，已排队未执行的“测量类” job 补发 Cancelled 事件；
//   * 事件在“job 执行完”后入队（与 Worker 相同的因果顺序）。
// ============================================================================

#pragma once

#include "lcr_api.h"

#include <math.h>
#include <queue>
#include <vector>

class MockLcrService : public ILcrService {
public:
    // ---- 可配置行为 -------------------------------------------------------
    // 单口 Z 脚本：按 chunk 首频生成点（默认成功）；返回 false 表示该 chunk
    // 整体后端错误。可强制指定失败点（requestedHz 精确匹配）。
    std::vector<double> forceFailAt;        // 请求频率命中即失败
    bool failAllZ = false;
    bool failCalStatus = false;
    // 双口：一阶 RC 低通 H(f)=1/(1+j f/fc)
    double wFcHz = 1591.5;
    double wGain = 1.0;
    // W chunk 全部失败
    bool failAllW = false;
    // 事件推送前回调（测试注入额外场景，如队列满）
    // 记录所有提交过的 job（断言“取消后不再提交新 chunk”用）
    std::vector<LcrJob> submittedJobs;
    int submitCalls = 0;

    bool submit(LcrJob& job) override {
        if (m_events.size() >= 8) return false;   // 模拟队列满
        job.id = ++m_nextId;                      // 与真实服务一致：回写 id
        m_jobs.push(job);
        submittedJobs.push_back(job);
        ++submitCalls;
        return true;
    }
    bool takeEvent(LcrEvent& ev) override {
        if (m_events.empty()) return false;
        ev = m_events.front();
        m_events.pop();
        return true;
    }
    bool busy() const override {
        return !m_jobs.empty() || m_executing;
    }
    void requestCancel() override { m_cancel = true; }

    // 执行至空闲（Worker 串行语义：每步先做当前 job，再看取消丢弃）
    void runToIdle() {
        while (busy()) step();
    }
    // 只推进一个 job（含取消丢弃）
    void step() {
        if (m_jobs.empty()) return;
        LcrJob job = m_jobs.front();
        m_jobs.pop();
        m_executing = true;
        if (m_cancel && isMeasurement(job.kind)) {
            LcrEvent ev{};
            ev.id = job.id;
            ev.kind = job.kind;
            ev.backendStatus = (int)AppLcrStatus::Cancelled;
            m_events.push(ev);
            m_executing = false;
            if (m_jobs.empty()) m_cancel = false;
            return;
        }
        LcrEvent ev = execute(job);
        m_events.push(ev);
        m_executing = false;
        if (m_jobs.empty()) m_cancel = false;
    }

private:
    static bool isMeasurement(LcrJobKind k) {
        return k == LcrJobKind::SweepZChunk || k == LcrJobKind::SweepWChunk ||
               k == LcrJobKind::MeasureAndCalcZ || k == LcrJobKind::SetTone;
    }

    bool hitForceFail(double f) const {
        for (double t : forceFailAt)
            if (fabs(t - f) < 1e-9) return true;
        return false;
    }

    LcrEvent execute(const LcrJob& job) {
        LcrEvent ev{};
        ev.id = job.id;
        ev.kind = job.kind;
        ev.backendStatus = 0;
        switch (job.kind) {
        case LcrJobKind::ReadCalibrationStatus: {
            if (failCalStatus) { ev.backendStatus = -5; break; }
            ev.cal.rangesValid = 3;
            ev.cal.openValid = true;
            ev.cal.shortValid = false;
            break;
        }
        case LcrJobKind::SweepZChunk: {
            ev.pointCount = job.pointCount;
            int nOk = 0;
            for (int i = 0; i < job.pointCount; ++i) {
                const double fr = geoPoint(job, i);
                AppZPoint& p = ev.z[i];
                p.fReq = fr;
                const double frac = (fr - 100.0) / (10000.0 - 100.0);
                if (!failAllZ && !hitForceFail(fr)) {
                    p.fAct = fr;
                    p.reOhm = 990.0 + 20.0 * frac;
                    p.imOhm = -5.0 + 10.0 * frac;
                    p.magOhm = hypot(p.reOhm, p.imOhm);
                    p.phaseDeg = atan2(p.imOhm, p.reOhm) * 180.0 / M_PI;
                    p.D = 0.01; p.Q = 100.0;
                    p.apiType = 'R';
                    p.apiStatus = 0;
                    ++nOk;
                } else {
                    p.fAct = fr;
                    p.reOhm = NAN; p.imOhm = NAN; p.magOhm = NAN;
                    p.phaseDeg = NAN; p.D = NAN; p.Q = NAN;
                    p.apiType = 'E';
                    p.apiStatus = -3;
                }
            }
            ev.backendStatus = nOk > 0 ? nOk : -4;
            break;
        }
        case LcrJobKind::SweepWChunk: {
            ev.pointCount = job.pointCount;
            int nOk = 0;
            for (int i = 0; i < job.pointCount; ++i) {
                const double fr = geoPoint(job, i);
                AppWPoint& p = ev.w[i];
                p.fReq = fr;
                if (!failAllW && !hitForceFail(fr)) {
                    p.fAct = fr;
                    const double x = fr / wFcHz;
                    const double mag = wGain / sqrt(1.0 + x * x);
                    const double ph = -atan(x) * 180.0 / M_PI;
                    p.hMag = mag;
                    p.hDb = 20.0 * log10(mag);
                    p.phaseDeg = ph;
                    const double rad = ph * M_PI / 180.0;
                    p.reH = mag * cos(rad);
                    p.imH = mag * sin(rad);
                    p.apiStatus = 0;
                    ++nOk;
                } else {
                    p.fAct = fr;
                    p.hMag = NAN; p.hDb = NAN; p.phaseDeg = NAN;
                    p.reH = NAN; p.imH = NAN;
                    p.apiStatus = -3;
                }
            }
            ev.backendStatus = nOk > 0 ? nOk : -4;
            break;
        }
        case LcrJobKind::MeasureAndCalcZ: {
            AppZPoint& p = ev.z[0];
            p.fReq = job.frequencyHz;
            p.fAct = job.frequencyHz;
            p.reOhm = 1000.0;
            p.imOhm = 0.0;
            p.magOhm = 1000.0;
            p.phaseDeg = 0.0;
            p.D = 0.0;
            p.Q = 1e30;
            p.apiType = 'R';
            p.apiStatus = 0;
            ev.pointCount = 1;
            ev.calc.type = 'R';
            ev.calc.rs = 1000.0;
            ev.calc.cs = NAN; ev.calc.ls = NAN;
            ev.calc.rp = 1000.0; ev.calc.cp = NAN; ev.calc.lp = NAN;
            ev.calc.D = 0.0; ev.calc.Q = 1e30;
            ev.calc.apiStatus = 0;
            break;
        }
        case LcrJobKind::SetTone:
            ev.actualHz = job.frequencyHz;
            ev.backendStatus = 0;
            break;
        case LcrJobKind::StopTone:
            ev.backendStatus = 0;
            break;
        default:
            break;
        }
        return ev;
    }

    // chunk 内第 i 个点的请求频率（几何中项规则，与 DNT 3 点 sweep 一致）
    static double geoPoint(const LcrJob& job, int i) {
        if (job.pointCount == 2) return i == 0 ? job.fStartHz : job.fStopHz;
        const double mid = sqrt(job.fStartHz * job.fStopHz);
        return i == 0 ? job.fStartHz : (i == 1 ? mid : job.fStopHz);
    }

    std::queue<LcrJob> m_jobs;
    std::queue<LcrEvent> m_events;
    uint32_t m_nextId = 0;
    bool m_executing = false;
    bool m_cancel = false;
};
