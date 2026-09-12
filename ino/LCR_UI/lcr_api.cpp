// ============================================================================
// lcr_api.cpp —— 测量服务：独立 FreeRTOS Worker + DNT API 唯一调用点
// ============================================================================

#include <Arduino.h>
#include <atomic>

#include "lcr_api.h"
#include "DO_NOT_TOUCH_lcr_api.h"

static constexpr int         kWorkerPrio   = 3;
static constexpr uint32_t    kWorkerStack  = 16384;
static constexpr BaseType_t  kWorkerCore   = 1;
static constexpr UBaseType_t kJobQueueLen  = 4;
static constexpr UBaseType_t kEventQueueLen = 4;

static QueueHandle_t s_jobQ = nullptr;
static QueueHandle_t s_eventQ = nullptr;
static TaskHandle_t s_workerTask = nullptr;

static std::atomic<bool> s_ready{false};
static std::atomic<bool> s_initFailed{false};
static std::atomic<bool> s_jobInFlight{false};
static std::atomic<bool> s_cancelReq{false};
static std::atomic<bool> s_bootDiagnostics{false};
static uint32_t s_nextId = 0;

static bool isMeasurementKind(LcrJobKind k)
{
    return k == LcrJobKind::SweepZChunk || k == LcrJobKind::SweepWChunk ||
           k == LcrJobKind::MeasureAndCalcZ || k == LcrJobKind::SetTone;
}

static double requestedFrequency(const LcrJob& job, int i)
{
    if (job.pointCount < 2 || i < 0 || i >= job.pointCount ||
        !isfinite(job.fStartHz) || !isfinite(job.fStopHz) ||
        !(job.fStartHz > 0.0) || !(job.fStopHz > 0.0))
        return NAN;
    if (i == 0) return job.fStartHz;
    if (i + 1 == job.pointCount) return job.fStopHz;
    return job.fStartHz * pow(job.fStopHz / job.fStartHz,
                              (double)i / (double)(job.pointCount - 1));
}

static void failZPoint(AppZPoint& p, double fReq, int status)
{
    p.fReq = fReq;
    p.fAct = NAN;
    p.reOhm = p.imOhm = p.magOhm = p.phaseDeg = NAN;
    p.D = p.Q = NAN;
    p.apiType = 'E';
    p.apiStatus = status;
}

static void failWPoint(AppWPoint& p, double fReq, int status)
{
    p.fReq = fReq;
    p.fAct = NAN;
    p.hMag = p.hDb = p.phaseDeg = NAN;
    p.reH = p.imH = NAN;
    p.apiStatus = status;
}

static void failCalc(AppCalcResult& c, int status)
{
    c.type = 'E';
    c.rs = c.cs = c.ls = c.rp = c.cp = c.lp = NAN;
    c.D = c.Q = NAN;
    c.apiStatus = status;
}

static void copyZPoint(const LcrZPoint& src, AppZPoint& dst)
{
    dst.fReq = src.f_req;       dst.fAct = src.f_act;
    dst.reOhm = src.z_re;       dst.imOhm = src.z_im;
    dst.magOhm = src.z_mag;     dst.phaseDeg = src.phi_z_deg;
    dst.D = src.D;              dst.Q = src.Q;
    dst.apiType = src.type;
    const bool bad = src.type == 'E' || !isfinite(src.f_act) ||
                     !(src.f_act > 0.0) || !isfinite(src.z_re) ||
                     !isfinite(src.z_im);
    dst.apiStatus = bad ? LCR_API_ERR_MEASURE : LCR_API_OK;
}

static void copyWPoint(const LcrWPoint& src, AppWPoint& dst)
{
    dst.fReq = src.f_req;       dst.fAct = src.f_act;
    dst.hMag = src.h_mag;       dst.hDb = src.h_db;
    dst.phaseDeg = src.phase_deg;
    const bool bad = !isfinite(src.f_act) || !(src.f_act > 0.0) ||
                     !isfinite(src.h_mag) || !isfinite(src.phase_deg);
    dst.apiStatus = bad ? LCR_API_ERR_MEASURE : LCR_API_OK;
    if (bad) {
        dst.reH = dst.imH = NAN;
    } else {
        const double rad = dst.phaseDeg * M_PI / 180.0;
        dst.reH = dst.hMag * cos(rad);
        dst.imH = dst.hMag * sin(rad);
    }
}

static void pushEvent(const LcrEvent& ev)
{
    (void)xQueueSend(s_eventQ, &ev, portMAX_DELAY);
}

static void destroyQueues()
{
    if (s_jobQ) { vQueueDelete(s_jobQ); s_jobQ = nullptr; }
    if (s_eventQ) { vQueueDelete(s_eventQ); s_eventQ = nullptr; }
}

static void lcrWorkerTask(void*)
{
    // GPIO4 由 setup 在创建 Worker 前采样；必须先设置诊断，再初始化 DNT，
    // 才能同时门控 init 与后续测量日志。测量逻辑本身不依赖该开关。
    lcr_api_set_diagnostics(s_bootDiagnostics.load(std::memory_order_acquire));
    const bool ok = lcr_api_init();

    {
        LcrEvent ev{};
        ev.id = 0;
        ev.kind = LcrJobKind::ServiceInit;
        ev.backendStatus = ok ? 0 : (int)AppLcrStatus::BackendError;
        pushEvent(ev);
    }
    if (ok) {
        s_ready.store(true, std::memory_order_release);
    } else {
        s_initFailed.store(true, std::memory_order_release);
        s_workerTask = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    for (;;) {
        LcrJob job{};
        if (xQueueReceive(s_jobQ, &job, portMAX_DELAY) != pdTRUE) continue;

        if (s_cancelReq.load(std::memory_order_acquire) && isMeasurementKind(job.kind)) {
            LcrEvent ev{};
            ev.id = job.id;
            ev.kind = job.kind;
            ev.backendStatus = (int)AppLcrStatus::Cancelled;
            pushEvent(ev);
            continue;
        }

        s_jobInFlight.store(true, std::memory_order_release);
        LcrEvent ev{};
        ev.id = job.id;
        ev.kind = job.kind;
        ev.backendStatus = 0;

        switch (job.kind) {
        case LcrJobKind::SweepZChunk: {
            if (job.pointCount != 2 && job.pointCount != 3) {
                ev.backendStatus = LCR_API_ERR_PARAM;
                break;
            }
            LcrZPoint t[3]{};
            const int r = lcr_api_sweep_z(job.fStartHz, job.fStopHz,
                                          job.pointCount, t, 3);
            ev.backendStatus = r;
            ev.pointCount = job.pointCount;
            const bool outputsWritten = r > 0 || r == LCR_API_ERR_MEASURE_ALL;
            for (int i = 0; i < job.pointCount; ++i) {
                if (outputsWritten) copyZPoint(t[i], ev.z[i]);
                else failZPoint(ev.z[i], requestedFrequency(job, i), r);
            }
            break;
        }

        case LcrJobKind::SweepWChunk: {
            if (job.pointCount != 2 && job.pointCount != 3) {
                ev.backendStatus = LCR_API_ERR_PARAM;
                break;
            }
            LcrWPoint t[3]{};
            const int r = lcr_api_sweep_w(job.fStartHz, job.fStopHz,
                                          job.pointCount, t, 3);
            ev.backendStatus = r;
            ev.pointCount = job.pointCount;
            const bool outputsWritten = r > 0 || r == LCR_API_ERR_MEASURE_ALL;
            for (int i = 0; i < job.pointCount; ++i) {
                if (outputsWritten) copyWPoint(t[i], ev.w[i]);
                else failWPoint(ev.w[i], requestedFrequency(job, i), r);
            }
            break;
        }

        case LcrJobKind::MeasureAndCalcZ: {
            ev.pointCount = 1;
            LcrZPoint p{};
            const int r1 = lcr_api_measure_z(job.frequencyHz, &p);
            ev.backendStatus = r1;
            if (r1 != LCR_API_OK) {
                failZPoint(ev.z[0], job.frequencyHz, r1);
                failCalc(ev.calc, r1);
                break;
            }

            copyZPoint(p, ev.z[0]);
            if (ev.z[0].apiStatus != LCR_API_OK) {
                ev.backendStatus = LCR_API_ERR_MEASURE;
                failCalc(ev.calc, LCR_API_ERR_MEASURE);
                break;
            }

            // measure_z 已完成校准；这里必须 apply_calib=false，禁止二次校准。
            LcrCalcResult c{};
            const int r2 = lcr_api_calc(p.f_act, p.z_re, p.z_im, false, &c);
            if (r2 == LCR_API_OK) {
                ev.calc.type = c.type;
                ev.calc.rs = c.rs;  ev.calc.cs = c.cs;  ev.calc.ls = c.ls;
                ev.calc.rp = c.rp;  ev.calc.cp = c.cp;  ev.calc.lp = c.lp;
                ev.calc.D = c.D;    ev.calc.Q = c.Q;
                ev.calc.apiStatus = r2;
            } else {
                failCalc(ev.calc, r2);
                ev.backendStatus = r2;
            }
            break;
        }

        case LcrJobKind::SetTone: {
            const double fa = lcr_api_set_freq(job.frequencyHz);
            ev.actualHz = fa;
            ev.backendStatus = (fa > 0.0) ? 0 : (int)fa;
            break;
        }

        case LcrJobKind::StopTone:
            lcr_api_set_freq(0);
            ev.backendStatus = 0;
            break;

        case LcrJobKind::Reset:
            lcr_api_reset();
            ev.backendStatus = 0;
            break;

        case LcrJobKind::SelfCheck:
            lcr_api_selfcheck();
            ev.backendStatus = 0;
            break;

        case LcrJobKind::ReadCalibrationStatus: {
            LcrCalStatus st{};
            const int r = lcr_api_cal_status(&st);
            ev.backendStatus = r;
            if (r == LCR_API_OK) {
                uint8_t nv = 0;
                for (int i = 0; i < 10; ++i) if (st.range[i].valid) ++nv;
                ev.cal.rangesValid = nv;
                ev.cal.openValid = st.open_valid;
                ev.cal.shortValid = st.short_valid;
            }
            break;
        }

        default:
            ev.backendStatus = LCR_API_ERR_PARAM;
            break;
        }

        s_jobInFlight.store(false, std::memory_order_release);
        if (job.kind == LcrJobKind::StopTone)
            s_cancelReq.store(false, std::memory_order_release);
        pushEvent(ev);
    }
}

class LcrWorkerService : public ILcrService {
public:
    bool submit(LcrJob& job) override
    {
        if (!s_ready.load(std::memory_order_acquire) || !s_jobQ) return false;
        job.id = ++s_nextId;
        return xQueueSend(s_jobQ, &job, 0) == pdTRUE;
    }

    bool takeEvent(LcrEvent& ev) override
    {
        return s_eventQ && xQueueReceive(s_eventQ, &ev, 0) == pdTRUE;
    }

    bool busy() const override
    {
        return s_jobInFlight.load(std::memory_order_acquire) ||
               (s_jobQ && uxQueueMessagesWaiting(s_jobQ) > 0);
    }

    void requestCancel() override
    {
        s_cancelReq.store(true, std::memory_order_release);
    }
};

static LcrWorkerService s_service;

ILcrService& lcrService() { return s_service; }

bool lcrServiceBegin(bool diagnosticsEnabled)
{
    if (s_workerTask) return true;

    destroyQueues();
    s_ready.store(false, std::memory_order_relaxed);
    s_initFailed.store(false, std::memory_order_relaxed);
    s_jobInFlight.store(false, std::memory_order_relaxed);
    s_cancelReq.store(false, std::memory_order_relaxed);
    s_bootDiagnostics.store(diagnosticsEnabled, std::memory_order_release);
    s_nextId = 0;

    s_jobQ = xQueueCreate(kJobQueueLen, sizeof(LcrJob));
    s_eventQ = xQueueCreate(kEventQueueLen, sizeof(LcrEvent));
    if (!s_jobQ || !s_eventQ) {
        destroyQueues();
        return false;
    }

    const BaseType_t created =
        xTaskCreatePinnedToCore(lcrWorkerTask, "lcr_worker", kWorkerStack,
                                nullptr, kWorkerPrio, &s_workerTask, kWorkerCore);
    if (created != pdPASS) {
        s_workerTask = nullptr;
        destroyQueues();
        return false;
    }
    return true;
}

bool lcrServiceReady()
{
    return s_ready.load(std::memory_order_acquire);
}

int lcrServiceInitError()
{
    return s_initFailed.load(std::memory_order_acquire)
               ? (int)AppLcrStatus::BackendError : 0;
}

bool lcrServiceSubmit(LcrJob& job) { return s_service.submit(job); }
bool lcrServiceTakeEvent(LcrEvent& ev) { return s_service.takeEvent(ev); }
bool lcrServiceBusy() { return s_service.busy(); }
void lcrServiceRequestCancel() { s_service.requestCancel(); }
