// ============================================================================
// lcr_api.cpp —— 测量服务实现：独立 FreeRTOS Worker + DNT API 唯一调用点
// ----------------------------------------------------------------------------
// 本文件是整个非 DO_NOT_TOUCH 生产代码中【唯一】允许
//     #include "DO_NOT_TOUCH_lcr_api.h"
// 的编译单元（tools/static_check.sh Gate C 强制）。这样做同时避免
// DO_NOT_TOUCH 头文件（全 inline 实现）在多个 Arduino .cpp 里重复展开
// 导致的全局对象/函数重复定义风险。
//
// Worker 约束（详见 lcr_api.h 文件头）：
//   * lcr_api_init() 必须与所有测量调用在同一个 task（DNT ADC 的
//     task-affinity：ISR 通知 init 时保存的 task handle）；
//   * 正常模式 lcr_api_set_diagnostics(false)（测量路径零打印）；
//   * job/event 均为定长 FreeRTOS 队列（4/4），测量期间无 heap 分配；
//   * completion event 不允许静默丢失：event queue 满时只阻塞专用 Worker，
//     UI/loop task 仍可运行并取走事件；收到事件即代表硬件动作已经结束。
//
// 取消语义（chunk-bounded）：requestCancel() 只置标志；Worker 在当前
// job 完成后丢弃后续“测量类”job 并补发 Cancelled 事件（等待方不会
// 挂死），StopTone/Reset 等收尾 job 照常执行 —— 与 plan.md §5.3 一致。
// ============================================================================

#include <Arduino.h>

#include <atomic>

#include "lcr_api.h"

// ★ 唯一的 DNT 入口（Gate C）——应用层其它文件一律通过 lcr_api.h 间接调用
#include "DO_NOT_TOUCH_lcr_api.h"

// ---------------------------------------------------------------------------
// Worker 配置
// ---------------------------------------------------------------------------
static constexpr int        kWorkerPrio  = 3;      // 高于 loop(1)，测量及时
static constexpr uint32_t   kWorkerStack = 16384;  // ESP-IDF FreeRTOS: bytes
static constexpr BaseType_t kWorkerCore  = 1;      // 与 loop 同核；BLE 栈在核 0
static constexpr UBaseType_t kJobQueueLen = 4;
static constexpr UBaseType_t kEventQueueLen = 4;

static QueueHandle_t s_jobQ = nullptr;
static QueueHandle_t s_eventQ = nullptr;
static TaskHandle_t s_workerTask = nullptr;

// 这些 flag 跨 Arduino loop task 与 lcr_worker task 访问。volatile 不能提供
// C++ 跨 task 同步；使用 atomic 明确发布 READY / cancel / busy 状态。
static std::atomic<bool> s_ready{false};
static std::atomic<bool> s_initFailed{false};
static std::atomic<bool> s_jobInFlight{false};
static std::atomic<bool> s_cancelReq{false};
static uint32_t s_nextId = 0;        // 仅在 loop task（submit）侧递增

// ---------------------------------------------------------------------------
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
    return job.fStartHz *
           pow(job.fStopHz / job.fStartHz,
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
        dst.reH = NAN;
        dst.imH = NAN;
    } else {
        const double rad = dst.phaseDeg * M_PI / 180.0;
        dst.reH = dst.hMag * cos(rad);
        dst.imH = dst.hMag * sin(rad);
    }
}

static void pushEvent(const LcrEvent& ev)
{
    // Completion 是状态机的可靠控制面，不能像 telemetry 一样丢包。
    // Worker 是专用 task；队列满时在这里阻塞会让出 CPU，loop task 可继续
    // takeEvent() 并释放队列空间，因此不会把 UI 主循环变成阻塞调用。
    (void)xQueueSend(s_eventQ, &ev, portMAX_DELAY);
}

static void destroyQueues()
{
    if (s_jobQ) {
        vQueueDelete(s_jobQ);
        s_jobQ = nullptr;
    }
    if (s_eventQ) {
        vQueueDelete(s_eventQ);
        s_eventQ = nullptr;
    }
}

// Worker task entry：init 与全部测量严格同 task（见文件头）
static void lcrWorkerTask(void*)
{
    // 1. 正常模式诊断关闭（诊断开关只影响打印，不影响测量）
    lcr_api_set_diagnostics(true);

    // 2. DNT 整体初始化（ADC/LCD_CAM/74HC595/校准装载，内部打印已门控）
    const bool ok = lcr_api_init();

    // 3. 发布 READY / INIT_FAILED。先入队事件，再 release-store 状态；setup
    // acquire-load READY 后，能看到 Worker 初始化完成前的全部写入。
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
        vTaskDelete(nullptr);      // init 失败：Worker 退出，服务不可用
        return;
    }

    // 4. 串行执行 job（单消费者：物理测量天然互斥）
    for (;;) {
        LcrJob job{};
        if (xQueueReceive(s_jobQ, &job, portMAX_DELAY) != pdTRUE) continue;

        // 取消生效点：当前 job 已完成，后续测量类 job 直接丢弃
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

            // DNT sweep_z only guarantees all output slots after it enters the
            // measurement loop. ERR_PARAM/ERR_BUF_TOO_SMALL return before any
            // output write; never read t[] on those paths. ERR_MEASURE_ALL is
            // different: every slot was written as type='E'+NaN, so preserve it.
            const bool outputsWritten = r > 0 || r == LCR_API_ERR_MEASURE_ALL;
            for (int i = 0; i < job.pointCount; ++i) {
                if (outputsWritten)
                    copyZPoint(t[i], ev.z[i]);
                else
                    failZPoint(ev.z[i], requestedFrequency(job, i), r);
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
                if (outputsWritten)
                    copyWPoint(t[i], ev.w[i]);
                else
                    failWPoint(ev.w[i], requestedFrequency(job, i), r);
            }
            break;
        }

        case LcrJobKind::MeasureAndCalcZ: {
            ev.pointCount = 1;
            LcrZPoint p{};
            const int r1 = lcr_api_measure_z(job.frequencyHz, &p);
            ev.backendStatus = r1;
            if (r1 != LCR_API_OK) {
                // lcr_api_measure_z() explicitly returns before writing *out
                // when lcr_derive_z() fails. Do not read p on that path.
                failZPoint(ev.z[0], job.frequencyHz, r1);
                failCalc(ev.calc, r1);   // calc was not attempted
                break;
            }

            copyZPoint(p, ev.z[0]);
            if (ev.z[0].apiStatus != LCR_API_OK) {
                ev.backendStatus = LCR_API_ERR_MEASURE;
                failCalc(ev.calc, LCR_API_ERR_MEASURE);
                break;
            }

            // apply_calib=false 强约束：measure 链已完成校准，
            // 再校准一次等于二次校准（plan.md §6.1）
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
            // DNT 把错误码作为负 double 返回（-6/-7），原样透传
            ev.backendStatus = (fa > 0.0) ? 0 : (int)fa;
            break;
        }

        case LcrJobKind::StopTone:
            lcr_api_set_freq(0);            // 停激励（硬件静音由 DNT 内部完成）
            ev.backendStatus = 0;
            break;

        case LcrJobKind::Reset:
            lcr_api_reset();
            ev.backendStatus = 0;
            break;

        case LcrJobKind::SelfCheck:
            lcr_api_selfcheck();            // 内部按 g_lcr_diag 门控打印
            ev.backendStatus = 0;
            break;

        case LcrJobKind::ReadCalibrationStatus: {
            LcrCalStatus st{};
            const int r = lcr_api_cal_status(&st);
            ev.backendStatus = r;
            if (r == LCR_API_OK) {
                uint8_t nv = 0;
                for (int i = 0; i < 10; ++i)
                    if (st.range[i].valid) ++nv;
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
        // StopTone completion 是“取消流程已完全收尾”的发布边界。必须先清
        // cancel，再把 completion 放入队列；否则 UI 可先观察到完成事件，
        // 随即提交的新测量仍有机会被旧 cancel 标志错误丢弃。
        if (job.kind == LcrJobKind::StopTone)
            s_cancelReq.store(false, std::memory_order_release);
        pushEvent(ev);
    }
}

// ---------------------------------------------------------------------------
// ILcrService 实现（提交侧运行在 loop task；执行侧在 Worker task）
// ---------------------------------------------------------------------------
class LcrWorkerService : public ILcrService {
public:
    bool submit(LcrJob& job) override
    {
        if (!s_ready.load(std::memory_order_acquire) || !s_jobQ) return false;
        job.id = ++s_nextId;               // loop task 单写者，无需锁；回写
        if (xQueueSend(s_jobQ, &job, 0) != pdTRUE) return false;
        return true;
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

bool lcrServiceBegin()
{
    if (s_workerTask) return true;                 // 已创建并运行

    // 清理任何一次失败创建留下的半初始化对象，保证幂等语义真实。
    destroyQueues();
    s_ready.store(false, std::memory_order_relaxed);
    s_initFailed.store(false, std::memory_order_relaxed);
    s_jobInFlight.store(false, std::memory_order_relaxed);
    s_cancelReq.store(false, std::memory_order_relaxed);
    s_nextId = 0;

    s_jobQ = xQueueCreate(kJobQueueLen, sizeof(LcrJob));
    s_eventQ = xQueueCreate(kEventQueueLen, sizeof(LcrEvent));
    if (!s_jobQ || !s_eventQ) {
        destroyQueues();
        return false;
    }

    // ★ init 与全部测量都在这个新 task 内执行（ADC task-affinity 约束）
    const BaseType_t created =
        xTaskCreatePinnedToCore(lcrWorkerTask, "lcr_worker", kWorkerStack,
                                nullptr, kWorkerPrio, &s_workerTask,
                                kWorkerCore);
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
               ? (int)AppLcrStatus::BackendError
               : 0;
}

bool lcrServiceSubmit(LcrJob& job) { return s_service.submit(job); }
bool lcrServiceTakeEvent(LcrEvent& ev) { return s_service.takeEvent(ev); }
bool lcrServiceBusy() { return s_service.busy(); }
void lcrServiceRequestCancel() { s_service.requestCancel(); }
