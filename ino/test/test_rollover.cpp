// ============================================================================
// test_rollover.cpp —— SweepEngine millis() 回绕边界
// ----------------------------------------------------------------------------
// StopTone 后 20ms quiet guard 必须跨 uint32_t wrap 正确工作：
// deadline = 0xfffffff5 + 20 -> 0x00000009。deadline 前不可 seal，达到后才 seal。
// ============================================================================
#include "check.h"
#include "test_mocks.h"

#include "measurement_types.h"
#include "sweep_engine.h"

#include <stdint.h>

int main()
{
    MockLcrService svc;
    SweepEngine eng(svc);
    SweepConfig cfg{};
    cfg.kind = MeasurementKind::OnePortImpedance;
    cfg.fStartHz = 100.0;
    cfg.fStopHz = 1000.0;
    cfg.pointsPerDecade = 4;   // 5 valid points -> fit-capable
    cfg.maxPoints = 257;

    CHECK(eng.start(cfg) == SweepStatus::Ok);

    // Finish calibration/chunks without advancing wall clock materially, until
    // StopTone has been submitted and is the only pending operation.
    uint32_t t = 0xfffffff0u;
    int guard = 0;
    while (eng.state() == SweepState::Measuring && guard++ < 32) {
        svc.runToIdle();
        eng.poll(t);
    }
    CHECK(eng.state() == SweepState::Stopping);

    // Complete StopTone just before uint32_t wrap. handleEvent() sets
    // quietDeadline = 0x00000009.
    svc.runToIdle();
    t = 0xfffffff5u;
    eng.poll(t);
    CHECK(eng.state() == SweepState::Stopping);
    CHECK(eng.sealedOnePortDataset() == nullptr);

    // After wrap but still one millisecond before the deadline: must wait.
    eng.poll(0x00000008u);
    CHECK(eng.state() == SweepState::Stopping);
    CHECK(eng.sealedOnePortDataset() == nullptr);

    // Exactly at deadline: seal becomes legal.
    eng.poll(0x00000009u);
    CHECK(eng.state() == SweepState::TransferReady);
    const OnePortDataset* d = eng.sealedOnePortDataset();
    CHECK(d != nullptr);
    CHECK(d->sealedUptimeMs == 0x00000009u);

    return test_finish();
}
