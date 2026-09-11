// ============================================================================
// lcr_api.h —— 应用层唯一测量接口（UI / 菜单 / 扫频编排 / Dataset 看到的全部）
// ----------------------------------------------------------------------------
// 为什么有这一层 wrapper：
//   硬件测量核心（LCD_CAM 8bit 并行正弦 -> 电阻网络 DAC、3 GPIO -> 74HC595
//   -> TIA/电压/电流增益与双端口控制、两路 ADC 间隔采样、自动量程、校准、
//   阻抗/传递函数计算）已经全部在 DO_NOT_TOUCH_lcr_api.h 中实现并在实板验证。
//   应用层绝不允许出现第二套测量实现（不自己产生波形、不自己碰 ADC、
//   不自己算 Z/H、不自己做量程/校准），一切测量动作只能通过本接口间接
//   调用 DO_NOT_TOUCH 的公开 API。本头文件不 include 任何 DO_NOT_TOUCH_*，
//   因此它可以在 host（PC）上编译，供编排层单测使用。
//
// 为什么必须独立 Worker、且初始化与测量必须在同一个 FreeRTOS task：
//   DO_NOT_TOUCH_lcr_adc.h 在 ADC 初始化时保存
//   xTaskGetCurrentTaskHandle()，之后 ADC ISR 只通知这个保存的 task，
//   而测量调用内部会等待该 task notification。若 lcr_api_init() 在
//   task A（如 Arduino setup）执行、而后续测量在 task B（Worker）执行，
//   ISR 会继续通知 task A，task B 永远等不到通知而死锁。
//   因此正确顺序是（见 lcr_api.cpp 的 LcrWorker）：
//     setup() 只创建 Worker task
//       Worker task entry:
//         lcr_api_set_diagnostics(false)   // 正常模式测量路径零打印
//         lcr_api_init()                   // 与后续所有测量同 task
//         发布 READY / INIT_FAILED 事件
//         之后 lcr_api_measure/sweep/calc/... 全部在本 task 内串行执行
//
// 为什么取消只能发生在 DNT 调用边界：
//   DNT 测量 API 是同步函数，内部有 settle/capture 等真实硬件等待，
//   不能为了“非阻塞”或“立即取消”去打断或修改它。取消语义是：
//   标记 cancelRequested -> 当前 2/3 点 chunk 正常完成 -> 不再提交下一
//   chunk -> Worker 执行 lcr_api_set_freq(0)（停激励）-> 事件确认后
//   解除 measurement lock。UI 必须如实显示“当前测量块完成后停止”，
//   不得声称瞬间停止。
//
// 哪些字段是 DNT 原样返回、哪些只是纯数学转换：
//   * AppZPoint 的 fAct/reOhm/imOhm/magOhm/phaseDeg/D/Q/apiType 与
//     AppWPoint 的 hMag/hDb/phaseDeg、AppCalcResult 全部字段、
//     AppCalSummary 全部字段 —— DNT 原样拷贝，应用层不做任何修改；
//   * AppWPoint.reH/imH —— 仅由 DNT 的 hMag/phaseDeg 做纯数学换算：
//       phaseRad = phaseDeg * pi / 180
//       reH = hMag * cos(phaseRad);  imH = hMag * sin(phaseRad)
//     这不是新测量，只是数据表示转换（复数 H = Vout/Vin 的直角坐标）；
//   * z[i].apiStatus —— LcrZPoint 本身不带状态字；DNT sweep 失败点以
//     type='E' + NaN 标记，本层只把这些标记翻译成
//     LCR_API_ERR_MEASURE(-3)，不杜撰其它错误码。
//
// 队列约束：job/event 队列都有固定上限（lcr_api.cpp 中为 4/4），
// 测量期间不做无界 heap 分配。
// ============================================================================

#pragma once

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// 应用层状态码（服务层语义）。
// DNT 的 LcrApiStatus（>=0 成功 / <0 错误码）原样放在事件的 backendStatus。
// ---------------------------------------------------------------------------
enum class AppLcrStatus : int {
    Ok = 0,
    Busy = 1,          // 已有同类任务在进行
    NotReady = 2,      // Worker 未完成 lcr_api_init()
    QueueFull = 3,     // job 队列满（UI 提交过快）
    Cancelled = 4,     // 因 requestCancel 被丢弃的 job
    BackendError = 5,  // DNT 返回错误（详见 backendStatus）
};

// ---------------------------------------------------------------------------
// 单口阻抗点：字段与 DNT LcrZPoint 一一对应（原样返回）
// ---------------------------------------------------------------------------
struct AppZPoint {
    double fReq;       // 请求频率（Hz）
    double fAct;       // DNT 实际频率（Hz）——CSV 拟合频率的唯一真源
    double reOhm;      // Re(Z)
    double imOhm;      // Im(Z)
    double magOhm;     // |Z|
    double phaseDeg;   // arg(Z)（度）
    double D;          // 损耗角正切
    double Q;          // 品质因数
    char apiType;      // DNT 判型：R / C / L；E = 该点失败
    int apiStatus;     // 0 = DNT 成功；-3 = DNT sweep 失败标记（见文件头）
};

// ---------------------------------------------------------------------------
// 双口传递点：hMag/hDb/phaseDeg 为 DNT 原样；reH/imH 为纯数学换算
// ---------------------------------------------------------------------------
struct AppWPoint {
    double fReq;
    double fAct;
    double hMag;       // |H|（DNT 原样）
    double hDb;        // 20*log10|H|（DNT 原样）
    double phaseDeg;   // arg(H)（度，DNT 原样）
    double reH;        // = hMag*cos(phaseRad) —— 纯数学换算，非新测量
    double imH;        // = hMag*sin(phaseRad) —— 纯数学换算，非新测量
    int apiStatus;     // 0 = DNT 成功；-3 = DNT sweep 失败标记
};

// ---------------------------------------------------------------------------
// 单元件换算结果：字段与 DNT LcrCalcResult 一一对应（原样返回）。
// 注意：对 lcr_api_measure_z 的结果调用换算时 apply_calib 必须为
// false —— measure 链已完成校准，再校准一次等于二次校准。
// ---------------------------------------------------------------------------
struct AppCalcResult {
    char type;         // R / C / L
    double rs, cs, ls; // 串联模型参数
    double rp, cp, lp; // 并联模型参数
    double D, Q;
    int apiStatus;     // DNT 换算 API 返回值原样
};

// ---------------------------------------------------------------------------
// 校准状态摘要：由 DNT lcr_api_cal_status() 原样汇总（不杜撰校准 ID）
// ---------------------------------------------------------------------------
struct AppCalSummary {
    uint8_t rangesValid;   // 10 个 TIA 量程中 valid 的个数（0..10）
    bool openValid;        // OPEN 校准是否有效
    bool shortValid;       // SHORT 校准是否有效
};

// ---------------------------------------------------------------------------
// Worker job / event 契约
// ---------------------------------------------------------------------------
enum class LcrJobKind : uint8_t {
    ServiceInit = 0,        // 仅用于事件：Worker 启动结果（READY / INIT_FAILED）
    SweepZChunk,            // 2/3 点单口扫频块（DNT lcr_api_sweep_z）
    SweepWChunk,            // 2/3 点双口扫频块（DNT lcr_api_sweep_w）
    MeasureAndCalcZ,        // 单频 Z 测量 + 纯数学换算(apply_calib=false)
    SetTone,                // 诊断信号发生器：DNT lcr_api_set_freq(f)
    StopTone,               // 停激励：DNT lcr_api_set_freq(0)
    Reset,                  // DNT lcr_api_reset()
    SelfCheck,              // DNT lcr_api_selfcheck()
    ReadCalibrationStatus,  // DNT lcr_api_cal_status()
};

struct LcrJob {
    uint32_t id;             // 由服务端分配（单调递增）
    LcrJobKind kind;

    // ---- SweepZChunk / SweepWChunk：全局几何网格上连续的 2 或 3 点 ----
    double fStartHz;         // 块首点请求频率（= 全局网格端点，不是新网格）
    double fStopHz;          // 块末点请求频率
    uint8_t pointCount;      // 只允许 2 或 3（Worker 校验，其余拒绝）

    // ---- MeasureAndCalcZ / SetTone：单频 ----
    double frequencyHz;
};

struct LcrEvent {
    uint32_t id;             // 对应 job 的 id
    LcrJobKind kind;

    int backendStatus;       // DNT LcrApiStatus 原样（sweep 为成功点数 n_ok）
    uint8_t pointCount;      // z[]/w[] 中有效槽位数（chunk: 2/3）

    AppZPoint z[3];          // SweepZChunk / MeasureAndCalcZ(z[0])
    AppWPoint w[3];          // SweepWChunk
    AppCalcResult calc;      // MeasureAndCalcZ
    AppCalSummary cal;       // ReadCalibrationStatus
    double actualHz;         // SetTone 的 DNT 实际输出频率
};

// ---------------------------------------------------------------------------
// 测量服务抽象：固件由 lcr_api.cpp 的 FreeRTOS Worker 实现；
// host 单测注入 MockLcrService 测试编排层（不模拟第二套 ADC）。
// ---------------------------------------------------------------------------
class ILcrService {
public:
    virtual ~ILcrService() = default;
    // 提交 job（非阻塞；队列满或未就绪返回 false）。
    // job.id 由服务端分配并通过引用回写（调用方用它匹配完成事件）。
    virtual bool submit(LcrJob& job) = 0;
    // 取一条完成事件（非阻塞；无事件返回 false）。事件在 DNT 调用
    // 完全返回之后才入队 —— 收到事件即代表该次硬件动作已结束。
    virtual bool takeEvent(LcrEvent& ev) = 0;
    // 有 job 在执行或排队
    virtual bool busy() const = 0;
    // 请求取消：Worker 在当前 job 完成后丢弃后续“测量类” job（补发
    // Cancelled 事件），StopTone/Reset 等收尾 job 仍会执行。
    virtual void requestCancel() = 0;
};

// ---------------------------------------------------------------------------
// 固件侧服务入口（lcr_api.cpp 实现；host 测试不要链接这些符号）
// ---------------------------------------------------------------------------
// 创建 Worker task（setup 中调用一次）。Worker 内部依次执行
// lcr_api_set_diagnostics(false) -> lcr_api_init() -> 发布 ServiceInit 事件。
bool lcrServiceBegin();
// Worker 是否已完成 DNT 初始化（成功）
bool lcrServiceReady();
// Worker 初始化失败时保存的错误信息（0 = 无 / 未失败）
int lcrServiceInitError();
// 全局服务实例（LCR_UI.ino 用它构造 SweepEngine）
ILcrService& lcrService();
// 便捷转发（UI 层可不经实例直接调用）
bool lcrServiceSubmit(LcrJob& job);
bool lcrServiceTakeEvent(LcrEvent& ev);
bool lcrServiceBusy();
void lcrServiceRequestCancel();
