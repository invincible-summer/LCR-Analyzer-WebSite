// ============================================================================
// radio_lock.h —— 「测量期间射频静默」invariant（host 可编译）
// ----------------------------------------------------------------------------
// 强 invariant（plan.md §6.1）：
//   MeasurementEngine.state ∈ {PREPARE..RESULT_READY}  =>  RadioState == Off
//   RadioState ∈ {StartingBle..Sending}                =>  Engine IDLE
//                                                        && ADC released
//                                                        && Excitation stopped
// 固件在 debug build（LCR_DEBUG_INVARIANTS）中轮询断言；host 状态机单测
// 直接驱动本模块验证互斥语义（ino/test/test_state_machines.cpp）。
// ============================================================================

#pragma once

#include <stdint.h>

// 测量引擎活动窗口：Prepare..ResultReady 之间为 true
void radioLockNotifyMeasurementActive(bool active);
// 射频活动窗口：RadioState != Off 时为 true
void radioLockNotifyRadioActive(bool active);

// invariant 是否成立（测量与射频不得同时活动）
bool radioLockInvariantOk();

// 测试辅助：复位
void radioLockReset();
