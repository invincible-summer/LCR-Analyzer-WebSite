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
//   * 事件在 DNT 调用完全返回后才入队 —— 收到事件即代表硬件动作结束。
//
// 取消语义（chunk-bounded）：requestCancel() 只置标志；Worker 在当前
// job 完成后丢弃后续“测量类”job 并补发 Cancelled 事件（等待方不会
// 挂死），StopTone/Reset 等收尾 job 照常执行 —— 与 plan.md §5.3 一致。
// ============================================================================

#include <Arduino.h>

#include "lcr_api.h"

// ★ 唯一的 DNT 入口（Gate C）——应用层其它文件一律通过 lcr_api.h 间接调用
#include "DO_NOT_TOUCH_lcr_api.h"

// ---------------------------------------------------------------------------
// Worker 配置
// ---------------------------------------------------------------------------
static constexpr int      kWorkerPrio  = 3;      // 高于 loop(1)，测量及时
static constexpr uint32_t kWorkerStack = 16384;  // DNT 测量链栈余量
static constexpr BaseType_t kWorkerCore = 1;     // 与 loop 同核；BLE 栈在核 0
static constexpr UBaseType_t kJobQueueLen = 4;
static constexpr UBaseType_t kEventQueueLen = 4;

static QueueHandle_t s_jobQ = nullptr;
static QueueHandle_t s_eventQ = nullptr;
static volatile bool s_ready = false;
static volatile bool s_initFailed = false;
static volatile bool s_jobInFlight = false;
static volatile bool s_cancelReq = false;
static uint32_t s_nextId = 0;        // 仅在 loop task（submit）侧递增

// ---------------------------------------------------------------------------
static bool isMeasurementKind(LcrJobKind k)
{
    return k == LcrJobKind::SweepZChunk || k == LcrJobKind::SweepWChunk ||
           k == LcrJobKind::MeasureAndCalcZ || k == LcrJobKind::SetTone;
}

static void pushEvent(const LcrEvent& ev)
{
    // UI loop 每圈取事件；队列满时短超时重试（有界），极端情况下放弃
    // 该事件并计数（不阻塞 Worker 的测量节奏）。
    for (int i = 0; i < 20; ++i) {
        if (xQueueSend(s_eventQ, &ev, pdMS_TO_TICKS(10)) == pdTRUE) return;
    }
    // 放弃：理论上 UI 存活时不发生
}

// Worker task entry：init 与全部测量严格同 task（见文件头）
static void lcrWorkerTask(void*)
{
    // 1. 正常模式诊断关闭（诊断开关只影响打印，不影响测量）
    lcr_api_set_diagnostics(false);

    // 2. DNT 整体初始化（ADC/LCD_CAM/74HC595/校准装载，内部打印已门控）
    const bool ok = lcr_api_init();

    // 3. 发布 READY / INIT_FAILED
    {
        LcrEvent ev{};
        ev.id = 0;
        ev.kind = LcrJobKind::ServiceInit;
        ev.backendStatus = ok ? 0 : (int)AppLcrStatus::BackendError;
        pushEvent(ev);
    }
    if (ok) {
        s_ready = true;
    } else {
        s_initFailed = true;
        vTaskDelete(nullptr);      // init 失败：Worker 退出，服务不可用
        return;
    }

    // 4. 串行执行 job（单消费者：物理测量天然互斥）
    for (;;) {
        LcrJob job;
        if (xQueueReceive(s_jobQ, &job, portMAX_DELAY) != pdTRUE) continue;

        // 取消生效点：当前 job 已完成，后续测量类 job 直接丢弃
        if (s_cancelReq && isMeasurementKind(job.kind)) {
            LcrEvent ev{};
            ev.id = job.id;
            ev.kind = job.kind;
            ev.backendStatus = (int)AppLcrStatus::Cancelled;
            pushEvent(ev);
            continue;
        }

        s_jobInFlight = true;
        LcrEvent ev{};
        ev.id = job.id;
        ev.kind = job.kind;
        ev.backendStatus = 0;

        switch (job.kind) {
        case LcrJobKind::SweepZChunk: {
            if (job.pointCount != 2 && job.pointCount != 3) {
                ev.backendStatus = -1;              // LCR_API_ERR_PARAM
                break;
            }
            LcrZPoint t[3];
            const int r = lcr_api_sweep_z(job.fStartHz, job.fStopHz,
                                          job.pointCount, t, 3);
            ev.backendStatus = r;
            ev.pointCount = job.pointCount;
            for (int i = 0; i < job.pointCount; ++i) {
                AppZPoint& p = ev.z[i];
                p.fReq = t[i].f_req;   p.fAct = t[i].f_act;
                p.reOhm = t[i].z_re;   p.imOhm = t[i].z_im;
                p.magOhm = t[i].z_mag; p.phaseDeg = t[i].phi_z_deg;
                p.D = t[i].D;          p.Q = t[i].Q;
                p.apiType = t[i].type;
                // LcrZPoint 无状态字：DNT sweep 失败点以 type='E'+NaN 标记，
                // 此处只翻译为 MEASURE 错误码（见 lcr_api.h 文件头说明）
                const bool bad = (t[i].type == 'E') || isnan(t[i].z_re) ||
                                 isnan(t[i].z_im) || !(t[i].f_act > 0.0);
                p.apiStatus = bad ? -3 : 0;
            }
            break;
        }

        case LcrJobKind::SweepWChunk: {
            if (job.pointCount != 2 && job.pointCount != 3) {
                ev.backendStatus = -1;              // LCR_API_ERR_PARAM
                break;
            }
            LcrWPoint t[3];
            const int r = lcr_api_sweep_w(job.fStartHz, job.fStopHz,
                                          job.pointCount, t, 3);
            ev.backendStatus = r;
            ev.pointCount = job.pointCount;
            for (int i = 0; i < job.pointCount; ++i) {
                AppWPoint& p = ev.w[i];
                p.fReq = t[i].f_req;    p.fAct = t[i].f_act;
                p.hMag = t[i].h_mag;    p.hDb = t[i].h_db;
                p.phaseDeg = t[i].phase_deg;
                const bool bad = isnan(t[i].h_mag) || isnan(t[i].phase_deg) ||
                                 !(t[i].f_act > 0.0);
                p.apiStatus = bad ? -3 : 0;
                // 纯数学换算（不是新测量）：复 H = |H|·e^{jφ}
                if (bad) { p.reH = NAN; p.imH = NAN; }
                else {
                    const double rad = p.phaseDeg * M_PI / 180.0;
                    p.reH = p.hMag * cos(rad);
                    p.imH = p.hMag * sin(rad);
                }
            }
            break;
        }

        case LcrJobKind::MeasureAndCalcZ: {
            LcrZPoint p;
            const int r1 = lcr_api_measure_z(job.frequencyHz, &p);
            ev.z[0].fReq = p.f_req;     ev.z[0].fAct = p.f_act;
            ev.z[0].reOhm = p.z_re;     ev.z[0].imOhm = p.z_im;
            ev.z[0].magOhm = p.z_mag;   ev.z[0].phaseDeg = p.phi_z_deg;
            ev.z[0].D = p.D;            ev.z[0].Q = p.Q;
            ev.z[0].apiType = p.type;
            ev.z[0].apiStatus = r1;
            ev.pointCount = 1;
            ev.backendStatus = r1;
            if (r1 == LCR_API_OK) {
                // apply_calib=false 强约束：measure 链已完成校准，
                // 再校准一次等于二次校准（plan.md §6.1）
                LcrCalcResult c;
                const int r2 = lcr_api_calc(p.f_act, p.z_re, p.z_im, false, &c);
                ev.calc.type = c.type;
                ev.calc.rs = c.rs;  ev.calc.cs = c.cs;  ev.calc.ls = c.ls;
                ev.calc.rp = c.rp;  ev.calc.cp = c.cp;  ev.calc.lp = c.lp;
                ev.calc.D = c.D;    ev.calc.Q = c.Q;
                ev.calc.apiStatus = r2;
                if (r2 != LCR_API_OK) ev.backendStatus = r2;
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
            LcrCalStatus st;
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
            ev.backendStatus = -1;          // LCR_API_ERR_PARAM
            break;
        }

        s_jobInFlight = false;
        pushEvent(ev);
        if (job.kind == LcrJobKind::StopTone)
            s_cancelReq = false;            // 取消流程的收尾 job 已执行
    }
}

// ---------------------------------------------------------------------------
// ILcrService 实现（提交侧运行在 loop task；执行侧在 Worker task）
// ---------------------------------------------------------------------------
class LcrWorkerService : public ILcrService {
public:
    bool submit(LcrJob& job) override
    {
        if (!s_ready || !s_jobQ) return false;
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
        return s_jobInFlight ||
               (s_jobQ && uxQueueMessagesWaiting(s_jobQ) > 0);
    }
    void requestCancel() override { s_cancelReq = true; }
};

static LcrWorkerService s_service;

ILcrService& lcrService() { return s_service; }

bool lcrServiceBegin()
{
    if (s_jobQ) return true;                       // 幂等
    s_jobQ = xQueueCreate(kJobQueueLen, sizeof(LcrJob));
    s_eventQ = xQueueCreate(kEventQueueLen, sizeof(LcrEvent));
    if (!s_jobQ || !s_eventQ) return false;
    s_ready = false;
    s_initFailed = false;
    s_cancelReq = false;
    s_nextId = 0;
    // ★ init 与全部测量都在这个新 task 内执行（ADC task-affinity 约束）
    return xTaskCreatePinnedToCore(lcrWorkerTask, "lcr_worker", kWorkerStack,
                                   nullptr, kWorkerPrio, nullptr,
                                   kWorkerCore) == pdPASS;
}

bool lcrServiceReady() { return s_ready; }
int  lcrServiceInitError() { return s_initFailed ? (int)AppLcrStatus::BackendError : 0; }

bool lcrServiceSubmit(LcrJob& job) { return s_service.submit(job); }
bool lcrServiceTakeEvent(LcrEvent& ev) { return s_service.takeEvent(ev); }
bool lcrServiceBusy() { return s_service.busy(); }
void lcrServiceRequestCancel() { s_service.requestCancel(); }
